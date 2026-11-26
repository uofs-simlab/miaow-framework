#define BOOST_TEST_MODULE miaow_workflow_record_checks
#include <boost/test/unit_test.hpp>
#include <nlohmann/json.hpp>

#include "job_record_checker.hpp"
#include "workflow_record_checker.hpp"
#include "../examples/04_workflow/workflow.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace {

// Hold the only scheduler thread so subsequent root submissions remain queued
class GatedWorkflowController final : public miaow::WorkflowController {
  public:
    GatedWorkflowController(std::promise<void>& entered, std::future<void> release)
        : entered_(entered), release_(std::move(release)) {}

    void initialize(miaow::Workflow&) override {
        entered_.set_value();
        release_.wait();
    }

  private:
    std::promise<void>& entered_;
    std::future<void> release_;
};

// Complete only after the nested controller returns its workflow record
class ParentWorkflowController final : public miaow::WorkflowController {
  public:
    explicit ParentWorkflowController(std::unique_ptr<miaow::WorkflowController> child)
        : child_(std::move(child)) {}

    void initialize(miaow::Workflow& workflow) override {
        workflow.submit_workflow(std::move(child_));
    }

  private:
    std::unique_ptr<miaow::WorkflowController> child_;
};

class SingleJobWorkflowController final : public miaow::WorkflowController {
  public:
    explicit SingleJobWorkflowController(
        miaow::Job job, std::unique_ptr<miaow::WorkflowController> child = {})
        : job_(std::move(job)), child_(std::move(child)) {}

    void initialize(miaow::Workflow& workflow) override {
        workflow.submit_job(std::move(job_));
        if (child_) {
            workflow.submit_workflow(std::move(child_));
        }
    }

  private:
    miaow::Job job_;
    std::unique_ptr<miaow::WorkflowController> child_;
};

// Keep the sibling job active until the test releases it
class GatedTaskExecutor final : public miaow::TaskExecutor {
  public:
    GatedTaskExecutor(std::promise<void>* entered, std::shared_future<void> release)
        : entered_(entered), release_(std::move(release)) {}

    caf::result<miaow::TaskResult> execute(const miaow::Task&) override {
        entered_->set_value();
        release_.wait();
        return miaow::TaskResult{miaow::TaskStatus::succeeded, "sibling completed"};
    }

  private:
    std::promise<void>* entered_;
    std::shared_future<void> release_;
};

class FailingWorkflowController final : public miaow::WorkflowController {
  public:
    FailingWorkflowController(std::future<void> sibling_entered,
                              std::promise<void>& failing)
        : sibling_entered_(std::move(sibling_entered)), failing_(failing) {}

    void initialize(miaow::Workflow&) override {
        sibling_entered_.wait();
        failing_.set_value();
        throw std::runtime_error("intentional child workflow failure");
    }

  private:
    std::future<void> sibling_entered_;
    std::promise<void>& failing_;
};

} // namespace

BOOST_AUTO_TEST_CASE(workflow) {
    const auto& arguments = boost::unit_test::framework::master_test_suite();
    const std::filesystem::path output_root{arguments.argv[1]};
    miaow::RuntimeOptions options{1};
    options.set_max_total_workers(1);
    options.output_root_ = output_root.string();

    miaow::TaskExecutorRegistry registry;
    const miaow::TaskExecutorId executor_id =
        registry.add<miaow::examples::workflow::SleepTaskExecutor>(
            std::chrono::steady_clock::now());
    miaow::tests::WorkflowRecordChecker checker;
    miaow::Runtime runtime(registry, options);
    miaow::WorkflowHandle workflow_handle = runtime.submit_workflow(checker.observe(
        std::make_unique<miaow::examples::workflow::TwoJobsController>(executor_id)));
    const miaow::WorkflowRecord record = workflow_handle.get();

    checker.check(record);
    BOOST_REQUIRE_EQUAL(checker.jobs.size(), 2);
    BOOST_TEST(checker.jobs[0].first != checker.jobs[1].first);
    for (const auto& [child_id, result] : checker.jobs) {
        BOOST_TEST_CONTEXT("child " << child_id) {
            const miaow::JobRecord& job_record = std::get<miaow::JobRecord>(result);
            miaow::tests::check_job_record(
                job_record, output_root / "run_0" /
                                ("job_" + std::to_string(job_record.job_id_) + ".json"));
            miaow::tests::check_successful_tasks(job_record);
            BOOST_REQUIRE_EQUAL(job_record.task_results_.size(), 6);
            for (std::size_t id = 1; id < job_record.task_timings_.size(); ++id) {
                BOOST_TEST(job_record.task_timings_[id].dispatch_offset_ns_ >=
                           job_record.task_timings_[id - 1].completion_offset_ns_);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(workflow_shutdown_with_active_and_queued_roots) {
    miaow::RuntimeOptions options{1};
    options.caf_scheduler_threads_ = 1;
    miaow::TaskExecutorRegistry registry;
    registry.add<miaow::examples::workflow::SleepTaskExecutor>(
        std::chrono::steady_clock::now());

    std::promise<void> entered;
    std::promise<void> release;
    std::future<void> entered_future = entered.get_future();
    auto runtime = std::make_unique<miaow::Runtime>(registry, options);
    miaow::WorkflowHandle active_workflow = runtime->submit_workflow(
        std::make_unique<GatedWorkflowController>(entered, release.get_future()));
    entered_future.wait();
    miaow::JobHandle queued_job = runtime->submit_job(miaow::Job{});
    miaow::WorkflowHandle queued_workflow =
        runtime->submit_workflow(std::make_unique<miaow::WorkflowController>());

    // All member calls have finished before another thread takes ownership for teardown
    std::thread shutdown(
        [owned_runtime = std::move(runtime)]() mutable { owned_runtime.reset(); });
    const auto is_shutdown_error = [](const std::runtime_error& error) {
        return std::string{error.what()} == "Runtime shutdown";
    };
    BOOST_CHECK_EXCEPTION(active_workflow.get(), std::runtime_error, is_shutdown_error);
    BOOST_CHECK_EXCEPTION(queued_job.get(), std::runtime_error, is_shutdown_error);
    BOOST_CHECK_EXCEPTION(queued_workflow.get(), std::runtime_error, is_shutdown_error);

    // Application futures must settle before the blocked actor can finish cleanup
    release.set_value();
    shutdown.join();
}

BOOST_AUTO_TEST_CASE(workflow_nested_completion) {
    miaow::TaskExecutorRegistry registry;
    registry.add<miaow::examples::workflow::SleepTaskExecutor>(
        std::chrono::steady_clock::now());
    miaow::tests::WorkflowRecordChecker parent_checker;
    miaow::tests::WorkflowRecordChecker child_checker;
    miaow::Runtime runtime(registry);
    miaow::WorkflowHandle handle = runtime.submit_workflow(
        parent_checker.observe(std::make_unique<ParentWorkflowController>(
            child_checker.observe(std::make_unique<miaow::WorkflowController>()))));
    const miaow::WorkflowRecord record = handle.get();

    parent_checker.check(record);
    BOOST_REQUIRE_EQUAL(parent_checker.workflows.size(), 1);
    child_checker.check(
        std::get<miaow::WorkflowRecord>(parent_checker.workflows[0].second));
}

BOOST_AUTO_TEST_CASE(workflow_nested_failure) {
    const auto& arguments = boost::unit_test::framework::master_test_suite();
    const std::filesystem::path output_root{arguments.argv[1]};
    miaow::RuntimeOptions options{1};
    options.caf_scheduler_threads_ = 2;
    options.output_root_ = output_root.string();

    std::promise<void> sibling_entered;
    std::promise<void> release_sibling;
    std::promise<void> child_failing;
    miaow::TaskExecutorRegistry registry;
    const miaow::TaskExecutorId executor =
        registry.add<GatedTaskExecutor>(&sibling_entered,
                                        release_sibling.get_future().share());
    miaow::Job sibling;
    const miaow::SimulationUnitId unit = sibling.add_unit("sibling", executor);
    sibling.add_task(unit, "wait");

    miaow::Runtime runtime(registry, options);
    miaow::WorkflowHandle handle =
        runtime.submit_workflow(std::make_unique<SingleJobWorkflowController>(
            std::move(sibling),
            std::make_unique<FailingWorkflowController>(sibling_entered.get_future(),
                                                        child_failing)));

    // Give the failure a chance to propagate while the sibling is still blocked
    child_failing.get_future().wait();
    const std::future_status status = handle.wait_for(std::chrono::milliseconds{100});
    release_sibling.set_value();
    BOOST_TEST((status == std::future_status::timeout));
    BOOST_CHECK_EXCEPTION((void)handle.get(), std::runtime_error,
                          [](const std::runtime_error& error) {
                              return std::string{error.what()}.find(
                                         "intentional child workflow failure") !=
                                     std::string::npos;
                          });

    std::ifstream sibling_record{output_root / "run_0" / "job_0.json"};
    const nlohmann::json summary = nlohmann::json::parse(sibling_record).at("summary");
    BOOST_TEST(summary.at("task_count") == 1);
    BOOST_TEST(summary.at("failed_task_count") == 0);
}

BOOST_AUTO_TEST_CASE(workflow_output_retention) {
    const auto& arguments = boost::unit_test::framework::master_test_suite();
    const std::filesystem::path output_root{arguments.argv[1]};
    miaow::RuntimeOptions options{1};
    options.output_root_ = output_root.string();
    miaow::TaskExecutorRegistry registry;
    registry.add<miaow::examples::workflow::SleepTaskExecutor>(
        std::chrono::steady_clock::now());

    const auto run_workflow = [&] {
        miaow::Runtime runtime(registry, options);
        miaow::WorkflowHandle handle = runtime.submit_workflow(
            std::make_unique<SingleJobWorkflowController>(miaow::Job{}));
        (void)handle.get();
    };

    const std::filesystem::path first_path = output_root / "run_0" / "job_0.json";
    run_workflow();
    std::ifstream first{first_path};
    const nlohmann::json original = nlohmann::json::parse(first);

    run_workflow();
    BOOST_TEST(std::filesystem::is_regular_file(output_root / "run_1" / "job_0.json"));
    std::ifstream preserved{first_path};
    BOOST_TEST(nlohmann::json::parse(preserved) == original);
}
