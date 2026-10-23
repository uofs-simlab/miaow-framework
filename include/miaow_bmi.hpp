#pragma once

#include <cstddef>
#include <concepts>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
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

/// Storage format for external BMI forcing and runtime-managed output
enum class BmiDataFormat {
    /// Comma-separated values
    csv,
};

/// External forcing configuration for one BMI task
struct BmiForcingConfig {
    /// Forcing storage format
    BmiDataFormat format_{BmiDataFormat::csv};

    /// Forcing file path
    std::string file_;

    /// Map from BMI input variables to forcing source variables
    std::map<std::string, std::string> variables_names_map_;
};

/// Runtime-managed output configuration for one BMI task
struct BmiOutputConfig {
    /// Output storage format
    BmiDataFormat format_{BmiDataFormat::csv};

    /// BMI output variables written to the task artifact; empty selects all outputs
    std::vector<std::string> variables_;

    /// Positive model-time interval; absent writes the initial state and every update
    std::optional<double> interval_;
};

/**
 * @brief Input and output configuration for one statically typed BMI task.
 *
 * The model implementation is supplied by `StaticBmiSimulationUnit`; this
 * configuration provides only the model initialization, forcing, and output
 * settings used by the task.
 */
struct StaticBmiConfig {
    /// BMI initialization config path
    std::string init_config_;

    /// Absolute tolerance for model-time comparisons in seconds
    double time_step_tolerance_seconds_{default_bmi_time_step_tolerance_seconds};

    /// Optional MIAOW-managed forcing; absent delegates forcing to the model
    std::optional<BmiForcingConfig> forcing_;

    /// Optional MIAOW-managed output; absent delegates output to the model
    std::optional<BmiOutputConfig> output_;
};

/**
 * @brief Plugin, input, and output configuration for one dynamic BMI task.
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

    /// Optional MIAOW-managed forcing; absent delegates forcing to the model
    std::optional<BmiForcingConfig> forcing_;

    /// Optional MIAOW-managed output; absent delegates output to the model
    std::optional<BmiOutputConfig> output_;
};

/**
 * @brief Resolve the bundled subprocess executable that runs dynamic BMI tasks.
 * @return Existing configured build or installation path.
 * @throws std::runtime_error if neither configured executable exists.
 */
std::string bmi_subprocess_executable_path();

/**
 * @brief Build one task for a statically typed BMI model.
 * @param simulation_unit_id Registered static BMI simulation unit id.
 * @param config Static BMI task configuration.
 * @return `Task` ready to submit through `Runtime::submit_job()`.
 */
Task make_bmi_task(SimulationUnitId simulation_unit_id,
                   const StaticBmiConfig& config);

/**
 * @brief Build one task for a dynamically loaded BMI plugin.
 * @param simulation_unit_id Registered dynamic BMI simulation unit id.
 * @param config Dynamic BMI task configuration.
 * @return `Task` ready to submit through `Runtime::submit_job()`.
 */
Task make_bmi_task(SimulationUnitId simulation_unit_id,
                   const DynamicBmiConfig& config);

/**
 * @brief Build tasks that use the same registered BMI simulation unit.
 *
 * Internally, this function calls `make_bmi_task()` for each configuration.
 * @tparam BmiConfig `StaticBmiConfig` or `DynamicBmiConfig`.
 * @param simulation_unit_id Registered BMI simulation unit id.
 * @param configs BMI task configurations.
 * @return `Task`s ready to submit through `Runtime::submit_job()`.
 */
template <class BmiConfig>
    requires(std::same_as<BmiConfig, StaticBmiConfig> ||
             std::same_as<BmiConfig, DynamicBmiConfig>)
std::vector<Task> make_bmi_tasks(SimulationUnitId simulation_unit_id,
                                 const std::vector<BmiConfig>& configs) {
    std::vector<Task> tasks;
    tasks.reserve(configs.size());
    for (const BmiConfig& config : configs) {
        tasks.push_back(make_bmi_task(simulation_unit_id, config));
    }
    return tasks;
}

/**
 * @brief NextGen-style realization expanded into BMI tasks by feature.
 *
 * The constructor reads a
 * [NextGen-style](https://github.com/NOAA-OWP/ngen) global realization with
 * one `bmi_fortran` module and `CsvPerFeature` forcing, resolves relative
 * paths and expands sibling `catchments.geojson` features. One `Task` is built for each
 * feature (e.g., catchment or response unit) with a sequential pipeline containing the BMI
 * model and its associated forcing. The tasks can be executed concurrently by a
 * `DynamicBmiSimulationUnit`.
 */
class PartitionedBmiRealization {
  public:
    /// Construct an empty realization
    PartitionedBmiRealization() = default;

    /**
     * @brief Partition a NextGen-style realization file by feature.
     * @param realization_path Path to the NextGen-style realization file.
     * @throws std::runtime_error if the realization is malformed or references
     * missing or unusable files.
     * @throws std::filesystem::filesystem_error if a referenced path cannot be
     * resolved or inspected.
     */
    explicit PartitionedBmiRealization(const std::string& realization_path);

  private:
    // Configuration and identifier for one realization feature
    struct FeatureConfig {
        DynamicBmiConfig config_;
        std::string feature_id_;
    };

    // Resolved configuration and identifier for each feature
    std::vector<FeatureConfig> feature_configs_;

    friend std::vector<Task> make_bmi_tasks(SimulationUnitId simulation_unit_id,
                                            const PartitionedBmiRealization& realization);
};

/**
 * @brief Build one dynamic BMI task per realization feature.
 * @param simulation_unit_id Registered dynamic BMI simulation unit id.
 * @param realization Parsed BMI realization.
 * @return `Task`s ready to submit through `Runtime::submit_job()`.
 */
std::vector<Task> make_bmi_tasks(SimulationUnitId simulation_unit_id,
                                 const PartitionedBmiRealization& realization);

namespace static_bmi_execution {

// Execute a statically supplied BMI model through the internal pipeline
TaskResult execute_task(const Task& task, std::unique_ptr<bmi_api::Bmi> model);

} // namespace static_bmi_execution

/**
 * @brief Simulation unit for a statically typed BMI model.
 *
 * A fresh model is constructed for every task and run in the parent process.
 *
 * @tparam BmiModelType BMI model type derived from `bmi_api::Bmi`.
 */
template <class BmiModelType>
    requires std::derived_from<BmiModelType, bmi_api::Bmi>
class StaticBmiSimulationUnit : public SimulationUnit {
  public:
    /**
     * @brief Construct a BMI simulation unit.
     *
     * Default-constructible model types store a factory for fresh
     * per-task model instances.
     */
    StaticBmiSimulationUnit()
        requires(std::default_initializable<BmiModelType>)
    {
        // Default-constructible models use the simplest per-task factory
        model_factory_ = []() -> std::unique_ptr<BmiModelType> {
            return std::make_unique<BmiModelType>();
        };
    }

    /**
     * @brief Construct a model simulation unit from model constructor arguments.
     *
     * MIAOW stores these arguments and reuses them to construct a fresh model
     * instance for each task.
     *
     * @tparam ModelConstructorArgs Constructor argument types for `BmiModelType`.
     * @param model_constructor_args Arguments forwarded to the BMI model constructor.
     */
    template <class... ModelConstructorArgs>
        requires(std::constructible_from<BmiModelType,
                                         const std::decay_t<ModelConstructorArgs>&...>)
    explicit StaticBmiSimulationUnit(ModelConstructorArgs... model_constructor_args) {
        // Constructor arguments are replayed for every fresh linked model
        model_factory_ = make_model_factory(std::move(model_constructor_args)...);
    }

    /**
     * @brief Execute a BMI model instance for a single task.
     * @param task `Task` built from `StaticBmiConfig`.
     * @return Immediate result for this BMI task execution.
     */
    caf::result<TaskResult> execute(const Task& task) override {
        try {
            // Pass one fresh static model through the internal execution bridge
            return static_bmi_execution::execute_task(task, model_factory_());
        } catch (...) {
            // Exceptions become failed task results so the job can keep running
            return make_failed_task_result(std::current_exception(), "BMI error");
        }
    }

  private:
    // Static model units create one BMI model object per task
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
};

/**
 * @brief Simulation unit that runs dynamically loaded BMI plugin tasks.
 *
 * `DynamicBmiSimulationUnit` executes the internal BMI pipeline in the current process.
 * To run BMI tasks in a separate process, register the bundled subprocess executable with
 * `SimulationUnitRegistry::add_subprocess()` instead.
 */
class DynamicBmiSimulationUnit : public SimulationUnit {
  public:
    /// Construct a dynamic BMI simulation unit
    DynamicBmiSimulationUnit() = default;

    /**
     * @brief Execute a dynamically loaded BMI plugin task.
     * @param task `Task` built from `DynamicBmiConfig`.
     * @return Immediate result for this BMI pipeline task execution.
     */
    caf::result<TaskResult> execute(const Task& task) override;
};

} // namespace miaow
