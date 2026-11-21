#define BOOST_TEST_MODULE miaow_workflow_record_checks
#include <boost/test/unit_test.hpp>

#include "job_record_checker.hpp"
#include "workflow_record_checker.hpp"
#include "../examples/04_workflow/workflow.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <variant>

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
