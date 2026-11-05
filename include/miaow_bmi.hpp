#pragma once

#include <cstddef>
#include <concepts>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "miaow.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow {

// Alias to avoid confusion with the `miaow::bmi` namespace
namespace bmi_api = ::bmi;

/**
 * @brief Plugin interface used to load and call a dynamic BMI plugin.
 *
 * Both interfaces follow the dynamic BMI plugin conventions used by
 * [NextGen](https://github.com/NOAA-OWP/ngen). The C++ interface uses
 * `bmi_model_create` and `bmi_model_destroy`, while the Fortran interface uses
 * the [procedural BMI bridge](https://github.com/NOAA-OWP/iso_c_fortran_bmi).
 */
enum class DynamicBmiBackend {
    /// C++ `bmi_api::Bmi` object interface
    cpp,

    /// Procedural Fortran BMI bridge
    fortran,
};

/// Default absolute tolerance for BMI model time comparisons in seconds
inline constexpr double default_bmi_time_step_tolerance_seconds = 1e-4;

/**
 * @brief Initialization and clock configuration for one statically typed BMI task.
 *
 * The model implementation is supplied by `StaticBmiTaskExecutor`; this
 * configuration provides the initialization path and clock tolerance for the task.
 */
struct StaticBmiConfig {
    /// BMI initialization config path
    std::string init_config_;

    /// Absolute tolerance for model-time comparisons in seconds
    double time_step_tolerance_seconds_{default_bmi_time_step_tolerance_seconds};
};

/**
 * @brief Plugin, initialization, and clock configuration for one dynamic BMI task.
 *
 * `backend_` selects the plugin interface used to load `plugin_path_`.
 */
struct DynamicBmiConfig {
    /// Backend implemented by the plugin
    DynamicBmiBackend backend_{DynamicBmiBackend::cpp};

    /// Dynamic plugin library path
    std::string plugin_path_;

    /// Optional BMI initialization config path
    std::string init_config_;

    /// Absolute tolerance for model-time comparisons in seconds
    double time_step_tolerance_seconds_{default_bmi_time_step_tolerance_seconds};
};

/**
 * @brief Resolve the bundled subprocess executable that runs dynamic BMI tasks.
 * @return Existing configured build or installation path.
 * @throws std::runtime_error if neither configured executable exists.
 */
std::string bmi_subprocess_executable_path();

/**
 * @brief Build one task for a statically typed BMI model.
 * @param unit Unit declared with a static BMI executor registration.
 * @param config Static BMI task configuration.
 * @return Complete model-run task to append to its owning `Job`.
 */
Task make_bmi_task(SimulationUnitHandle unit, const StaticBmiConfig& config);

/**
 * @brief Build one task for a dynamically loaded BMI plugin.
 * @param unit Unit declared with a dynamic BMI executor registration.
 * @param config Dynamic BMI task configuration.
 * @return Complete model-run task to append to its owning `Job`.
 */
Task make_bmi_task(SimulationUnitHandle unit, const DynamicBmiConfig& config);

namespace bmi {

// Execute a complete run of one statically supplied BMI model
TaskResult execute_task(const Task& task, std::unique_ptr<bmi_api::Bmi> model);

} // namespace bmi

/**
 * @brief Task executor for a statically typed BMI model.
 *
 * `run` constructs a fresh model for a complete run in the parent process.
 * Individual operations retain an initialized model until `finalize`.
 *
 * @tparam BmiModelType BMI model type derived from `bmi_api::Bmi`.
 */
template <class BmiModelType>
    requires std::derived_from<BmiModelType, bmi_api::Bmi>
class StaticBmiTaskExecutor : public TaskExecutor {
  public:
    /**
     * @brief Construct a BMI task executor.
     *
     * Default-constructible model types store a factory for fresh model instances.
     */
    StaticBmiTaskExecutor()
        requires(std::default_initializable<BmiModelType>)
    {
        // Construct a fresh model for initialization or a complete run
        model_factory_ = []() -> std::unique_ptr<BmiModelType> {
            return std::make_unique<BmiModelType>();
        };
    }

    /**
     * @brief Construct a model task executor from model constructor arguments.
     *
     * MIAOW stores these arguments and reuses them to construct a fresh model
     * instance for each initialization or complete run.
     *
     * @tparam ModelConstructorArgs Constructor argument types for `BmiModelType`.
     * @param model_constructor_args Arguments forwarded to the BMI model constructor.
     */
    template <class... ModelConstructorArgs>
        requires(std::constructible_from<BmiModelType,
                                         const std::decay_t<ModelConstructorArgs>&...>)
    explicit StaticBmiTaskExecutor(ModelConstructorArgs... model_constructor_args) {
        // Constructor arguments are replayed for every fresh linked model
        model_factory_ = make_model_factory(std::move(model_constructor_args)...);
    }

    /// Attempt finalization when an initialized model remains at unit shutdown
    ~StaticBmiTaskExecutor() override {
        if (model_) {
            try {
                model_->Finalize();
            } catch (...) {
                // Explicit finalize tasks report errors; destruction only releases state
            }
        }
    }

    /**
     * @brief Execute a complete run or one operation on the retained model.
     * @param task `run` from `StaticBmiConfig`, `initialize` with a config-path string,
     * or an argument-free `update`, `get_current_time`, or `finalize` operation.
     * @return Immediate result for this BMI task execution.
     */
    caf::result<TaskResult> execute(const Task& task) override {
        try {
            if (task.operation_ == "run") {
                if (model_) {
                    return make_failed_task_result("model is already initialized",
                                                   "BMI error");
                }
                return bmi::execute_task(task, model_factory_());
            }

            if (task.operation_ == "initialize") {
                if (model_) {
                    return make_failed_task_result("model is already initialized",
                                                   "BMI error");
                }
                const std::string* init_config = task.string_arg(0);
                if (init_config == nullptr) {
                    return make_failed_task_result(
                        "initialize requires a configuration-path string", "BMI error");
                }
                std::unique_ptr<BmiModelType> model = model_factory_();
                model->Initialize(*init_config);
                model_ = std::move(model);
                return TaskResult{TaskStatus::succeeded, "BMI initialize complete"};
            }

            if (task.operation_ == "update") {
                if (!model_) {
                    return make_failed_task_result("model is not initialized", "BMI error");
                }
                if (model_failed_) {
                    return make_failed_task_result(
                        "model must be finalized after a failed BMI call", "BMI error");
                }
                model_->Update();
                return TaskResult{TaskStatus::succeeded, "BMI update complete"};
            }

            if (task.operation_ == "get_current_time") {
                if (!model_) {
                    return make_failed_task_result("model is not initialized", "BMI error");
                }
                if (model_failed_) {
                    return make_failed_task_result(
                        "model must be finalized after a failed BMI call", "BMI error");
                }
                return TaskResult{TaskStatus::succeeded, "BMI current time",
                                  caf::config_value{model_->GetCurrentTime()}};
            }

            if (task.operation_ == "finalize") {
                if (!model_) {
                    return make_failed_task_result("model is not initialized", "BMI error");
                }
                // Release ownership before calling Finalize so failure is not retried
                std::unique_ptr<BmiModelType> model = std::move(model_);
                model_failed_ = false;
                model->Finalize();
                return TaskResult{TaskStatus::succeeded, "BMI finalize complete"};
            }

            return make_failed_task_result("unsupported operation '" + task.operation_ +
                                               "'",
                                           "BMI error");
        } catch (...) {
            // A failed model call permits cleanup but no further updates or queries
            model_failed_ = model_ != nullptr;
            return make_failed_task_result(std::current_exception(), "BMI error");
        }
    }

  private:
    // Reuse the constructor arguments for each fresh model instance
    using ModelFactory = std::function<std::unique_ptr<BmiModelType>()>;

    template <class... ModelConstructorArgs>
    static ModelFactory make_model_factory(ModelConstructorArgs... model_constructor_args) {
        // Store constructor arguments outside the task payload
        std::tuple<std::decay_t<ModelConstructorArgs>...> stored_constructor_args(
            std::move(model_constructor_args)...);

        return [stored_constructor_args =
                    std::move(stored_constructor_args)]() -> std::unique_ptr<BmiModelType> {
            // Reconstruct the concrete linked model from the captured arguments
            return std::apply(
                [](const std::decay_t<ModelConstructorArgs>&... args)
                    -> std::unique_ptr<BmiModelType> {
                    return std::make_unique<BmiModelType>(args...);
                },
                stored_constructor_args);
        };
    }

    ModelFactory model_factory_;
    std::unique_ptr<BmiModelType> model_;
    bool model_failed_{false};
};

/**
 * @brief Task executor that runs dynamically loaded BMI plugin tasks.
 *
 * `DynamicBmiTaskExecutor` runs one fresh BMI model per task in the current process.
 * To run BMI tasks in a separate process, register the bundled subprocess executable with
 * `TaskExecutorRegistry::add_subprocess()` instead.
 */
class DynamicBmiTaskExecutor : public TaskExecutor {
  public:
    /// Construct a dynamic BMI task executor
    DynamicBmiTaskExecutor() = default;

    /**
     * @brief Execute a dynamically loaded BMI plugin task.
     * @param task `Task` built from `DynamicBmiConfig`.
     * @return Immediate result for this BMI task execution.
     */
    caf::result<TaskResult> execute(const Task& task) override;
};

} // namespace miaow
