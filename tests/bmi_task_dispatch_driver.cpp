// Execute BMI operation sequences for validation through the saved job record
#include <algorithm>
#include <cstddef>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "bmi_heat.hxx"
#include "miaow.hpp"
#include "miaow_bmi.hpp"
#include "miaow_io.hpp"
#include "../examples/config.hpp"

namespace {

// One entry per model instance; method counts include calls that throw
struct ModelCalls {
    int updates{0};
    int queries{0};
    int finalizations{0};
    int destructions{0};
    bool exception_destroyed_before_model{false};
};

// Shared state remains valid even if the model is destroyed before the exception
class BmiCallError final : public std::runtime_error {
  public:
    BmiCallError(const std::string& message,
                 std::shared_ptr<std::vector<ModelCalls>> calls, std::size_t index)
        : std::runtime_error(message), calls_(std::move(calls)), index_(index) {}

    ~BmiCallError() override {
        ModelCalls& calls = calls_->at(index_);
        calls.exception_destroyed_before_model = calls.destructions == 0;
    }

  private:
    std::shared_ptr<std::vector<ModelCalls>> calls_;
    std::size_t index_;
};

// Only the first session throws; counts remain available after executor destruction
class ThrowingBmiHeat final : public BmiHeat {
  public:
    ThrowingBmiHeat(std::shared_ptr<std::vector<ModelCalls>> calls,
                    std::string failure_operation)
        : calls_(std::move(calls)), index_(calls_->size()),
          failure_operation_(std::move(failure_operation)) {
        calls_->emplace_back();
    }

    ~ThrowingBmiHeat() override {
        ++calls_->at(index_).destructions;
    }

    void Initialize(std::string config_file) override {
        if (index_ == 0 && failure_operation_ == "initialize") {
            throw BmiCallError("intentional initialization failure", calls_, index_);
        }
        BmiHeat::Initialize(config_file);
    }

    void Update() override {
        ++calls_->at(index_).updates;
        if (index_ == 0 && failure_operation_ == "update") {
            throw std::runtime_error("intentional update failure");
        }
        BmiHeat::Update();
    }

    double GetCurrentTime() override {
        ++calls_->at(index_).queries;
        if (index_ == 0 && failure_operation_ == "get_current_time") {
            throw std::runtime_error("intentional time query failure");
        }
        return BmiHeat::GetCurrentTime();
    }

    void Finalize() override {
        ++calls_->at(index_).finalizations;
        // Report the injected error after cleanup to exercise failure on session release
        BmiHeat::Finalize();
        if (index_ == 0 && failure_operation_ == "finalize") {
            throw BmiCallError("intentional finalization failure", calls_, index_);
        }
    }

  private:
    std::shared_ptr<std::vector<ModelCalls>> calls_;
    std::size_t index_;
    std::string failure_operation_;
};

// The hook runs after unit shutdown, so output includes finalization and destruction
class BmiFailureController final : public miaow::JobController {
  public:
    explicit BmiFailureController(std::shared_ptr<std::vector<ModelCalls>> calls)
        : calls_(std::move(calls)) {}

    caf::config_value finalize() override {
        caf::config_value::list models;
        for (const ModelCalls& calls : *calls_) {
            models.emplace_back(caf::config_value::dictionary{
                {"updates", caf::config_value{calls.updates}},
                {"queries", caf::config_value{calls.queries}},
                {"finalizations", caf::config_value{calls.finalizations}},
                {"destructions", caf::config_value{calls.destructions}},
                {"exception_destroyed_before_model",
                 caf::config_value{calls.exception_destroyed_before_model}}});
        }
        return caf::config_value{std::move(models)};
    }

  private:
    std::shared_ptr<std::vector<ModelCalls>> calls_;
};

// Follow the configured operation with a complete run across all five execution paths
void setup_dispatch(miaow::Job& job, miaow::TaskExecutorRegistry& registry,
                    const caf::config_value::dictionary& model_config,
                    const std::string& operation) {
    const miaow::TaskExecutorId static_bmi =
        registry.add<miaow::StaticBmiTaskExecutor<BmiHeat>>();
    const miaow::TaskExecutorId dynamic_bmi = registry.add<miaow::DynamicBmiTaskExecutor>();
    const miaow::TaskExecutorId subprocess_bmi =
        registry.add_subprocess(miaow::bmi_subprocess_executable_path());

    const auto add_operation_pair = [&job, &operation](miaow::Task run_task) {
        // Reuse the unit's executor after either a rejected operation or a complete run
        miaow::Task first_task = run_task;
        first_task.operation_ = operation;
        job.add_task(std::move(first_task));
        job.add_task(std::move(run_task));
    };
    const miaow::SimulationUnitHandle static_unit = job.add_unit("static", static_bmi);
    add_operation_pair(miaow::Task{static_unit, "run", model_config});

    // Exercise both plugins in-process and in their unit-owned subprocesses
    std::vector<caf::config_value::dictionary> configs(2, model_config);
    configs[0].insert("backend", "cpp");
    configs[0].insert("plugin_path", MIAOW_CPP_HEAT_PLUGIN_PATH);
    configs[1].insert("backend", "fortran");
    configs[1].insert("plugin_path", MIAOW_FORTRAN_HEAT_PLUGIN_PATH);
    for (const miaow::TaskExecutorId executor : {dynamic_bmi, subprocess_bmi}) {
        const std::string mode = executor == dynamic_bmi ? "in_process_" : "subprocess_";
        for (std::size_t index = 0; index < configs.size(); ++index) {
            const miaow::SimulationUnitHandle unit =
                job.add_unit(mode + std::to_string(index), executor);
            add_operation_pair(miaow::Task{unit, "run", configs[index]});
        }
    }
}

// Check calls made in the wrong model state: update before initialization,
// and initialize/run while a model is already initialized
void setup_lifecycle(miaow::Job& job, miaow::TaskExecutorRegistry& registry,
                     const nlohmann::json& config,
                     caf::config_value::dictionary model_config) {
    const bool dynamic = config.at("scenario") == "dynamic_lifecycle";
    miaow::TaskExecutorId executor;
    if (dynamic) {
        executor = config.value("subprocess", false)
                       ? registry.add_subprocess(miaow::bmi_subprocess_executable_path())
                       : registry.add<miaow::DynamicBmiTaskExecutor>();
        const bool fortran = config.value("backend", "cpp") == "fortran";
        model_config.insert("backend", fortran ? "fortran" : "cpp");
        model_config.insert("plugin_path", fortran ? MIAOW_FORTRAN_HEAT_PLUGIN_PATH
                                                   : MIAOW_CPP_HEAT_PLUGIN_PATH);
    } else {
        executor = registry.add<miaow::StaticBmiTaskExecutor<BmiHeat>>();
    }
    const miaow::SimulationUnitHandle unit =
        job.add_unit(dynamic ? "dynamic" : "static", executor);

    // No model is initialized, so this update must fail
    job.add_task(unit, "update");

    // Start a valid session and advance its clock by one time step
    job.add_task(unit, "initialize", model_config);
    job.add_task(unit, "update");

    // Both calls must fail because the unit already has an initialized model
    job.add_task(unit, "initialize", model_config);
    job.add_task(unit, "run", model_config);

    // Rejected calls must preserve the active model and its current time
    job.add_task(unit, "get_current_time");

    // Finalization ends the session; another update must fail
    job.add_task(unit, "finalize");
    job.add_task(unit, "update");

    // A fresh session starts at time zero and can be finalized normally
    job.add_task(unit, "initialize", model_config);
    job.add_task(unit, "get_current_time");
    job.add_task(unit, "finalize");
}

// Fail one model call, clean up the session, and verify recovery with a fresh model
void setup_recovery(miaow::Job& job, miaow::TaskExecutorRegistry& registry,
                    const caf::config_value::dictionary& model_config,
                    const std::string& operation) {
    auto calls = std::make_shared<std::vector<ModelCalls>>();
    const miaow::TaskExecutorId throwing_bmi =
        registry.add<miaow::StaticBmiTaskExecutor<ThrowingBmiHeat>>(calls, operation);
    const miaow::SimulationUnitHandle unit = job.add_unit("throwing", throwing_bmi);
    job.set_controller(std::make_unique<BmiFailureController>(calls));

    // The first instance throws during initialization or the selected later call
    job.add_task(unit, "initialize", model_config);
    if (operation != "initialize") {
        job.add_task(unit, operation);
    }

    // Both operations must fail without reaching the model after the exception
    job.add_task(unit, "update");
    job.add_task(unit, "get_current_time");

    // Update/query failures permit cleanup. Initialize/finalize failures release
    // the model, so this finalize must report no initialized model.
    job.add_task(unit, "finalize");

    // The fresh instance must work. Leave it active so executor destruction finalizes
    // this model, without retrying finalization of the first instance.
    job.add_task(unit, "initialize", model_config);
    job.add_task(unit, "update");
    job.add_task(unit, "get_current_time");
}

// Finalize each model after its last value task is recorded, including failed tasks
class BmiValuesController final : public miaow::JobController {
  public:
    explicit BmiValuesController(std::vector<miaow::TaskHandle> terminal_tasks)
        : terminal_tasks_(std::move(terminal_tasks)) {}

    void callback(miaow::Job& job, std::span<const miaow::TaskRecord> completed_tasks,
                  std::span<const miaow::UnitFailure> failed_units) override {
        if (!failed_units.empty()) {
            job.fail(failed_units.front().reason_);
            return;
        }
        for (const miaow::TaskRecord& record : completed_tasks) {
            if (std::any_of(terminal_tasks_.begin(), terminal_tasks_.end(),
                            [&record](miaow::TaskHandle task) {
                                return task.task_id() == record.task_id_;
                            })) {
                job.add_task(record.unit_, "finalize");
            }
        }
    }

  private:
    std::vector<miaow::TaskHandle> terminal_tasks_;
};

// Transfer double snapshots into C++ and Fortran models through every execution path
void setup_values(miaow::Job& job, miaow::TaskExecutorRegistry& registry,
                  caf::config_value::dictionary model_config) {
    const std::string temperature = "plate_surface__temperature";
    const miaow::TaskExecutorId static_bmi =
        registry.add<miaow::StaticBmiTaskExecutor<BmiHeat>>();
    const miaow::TaskExecutorId dynamic_bmi = registry.add<miaow::DynamicBmiTaskExecutor>();
    const miaow::TaskExecutorId subprocess_bmi =
        registry.add_subprocess(miaow::bmi_subprocess_executable_path());
    const miaow::SimulationUnitHandle source = job.add_unit("static", static_bmi);
    const miaow::TaskHandle initialized = job.add_task(source, "initialize", model_config);
    caf::config_value::list field;
    for (std::size_t index = 0; index < 25; ++index) {
        field.emplace_back(0.1 * static_cast<double>(index));
    }
    const miaow::TaskHandle written =
        job.add_task(source, initialized, "set_value", temperature, std::move(field));
    const miaow::TaskHandle snapshot =
        job.add_task(source, written, "get_value", temperature);

    // Reject a short buffer and block its consumer, then explicitly release the model
    const miaow::TaskHandle wrong_size =
        job.add_task(source, snapshot, "set_value", temperature,
                     caf::config_value::list{caf::config_value{1.0}});
    std::vector<miaow::TaskHandle> terminal_tasks{
        job.add_task(source, wrong_size, "get_value", temperature)};

    for (miaow::TaskExecutorId executor : {dynamic_bmi, subprocess_bmi}) {
        for (const std::string backend : {"cpp", "fortran"}) {
            const bool fortran = backend == "fortran";
            const std::string mode = executor == dynamic_bmi ? "dynamic_" : "subprocess_";
            const miaow::SimulationUnitHandle unit = job.add_unit(mode + backend, executor);
            model_config.insert_or_assign("backend", backend);
            model_config.insert_or_assign("plugin_path",
                                          fortran ? MIAOW_FORTRAN_HEAT_PLUGIN_PATH
                                                  : MIAOW_CPP_HEAT_PLUGIN_PATH);
            const miaow::TaskHandle ready = job.add_task(unit, "initialize", model_config);
            const miaow::TaskHandle transferred =
                job.add_task(unit, std::vector<miaow::TaskHandle>{ready, snapshot},
                             "set_value", temperature, miaow::output_of(snapshot));
            miaow::TaskHandle last =
                job.add_task(unit, transferred, "get_value", temperature);
            if (fortran) {
                const std::string identification = "model__identification_number";
                last = job.add_task(unit, last, "set_value", identification,
                                    caf::config_value::list{caf::config_value{42}});
                last = job.add_task(unit, last, "get_value", identification);
                last = job.add_task(
                    unit, last, "set_value", identification,
                    caf::config_value::list{caf::config_value{
                        static_cast<std::int64_t>(std::numeric_limits<int>::max()) + 1}});
                last = job.add_task(unit, last, "get_value", identification);
            }
            terminal_tasks.push_back(last);
        }
    }
    job.set_controller(std::make_unique<BmiValuesController>(std::move(terminal_tasks)));
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        miaow::RuntimeOptions runtime_options;
        const nlohmann::json config = read_example_config(argc, argv, runtime_options);

        // Resolve shared input defaults before selecting the test workflow
        const std::string operation = config.value("operation", "unknown");
        const std::string scenario = config.value("scenario", "dispatch");
        const std::string init_config =
            config.value("init_config", "./data/configs/config_dynamic_heat.txt");
        caf::config_value::dictionary model_config;
        model_config.insert("init_config", init_config);

        miaow::TaskExecutorRegistry registry;
        miaow::Job job;
        if (scenario == "values") {
            setup_values(job, registry, model_config);
        } else if (scenario == "failure_recovery") {
            setup_recovery(job, registry, model_config, operation);
        } else if (scenario == "static_lifecycle" || scenario == "dynamic_lifecycle") {
            setup_lifecycle(job, registry, config, model_config);
        } else {
            setup_dispatch(job, registry, model_config, operation);
        }

        // Runtime saves the job record for the shared regression checker
        miaow::Runtime runtime(registry, runtime_options);
        const miaow::JobRecord record = runtime.submit_job(std::move(job)).get();
        miaow::print_job_summary(record);
        // The case JSON declares whether failed task results and exit code 1 are expected
        return miaow::all_tasks_succeeded(record) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
