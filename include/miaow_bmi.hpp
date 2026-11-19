#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <unordered_map>

#include "miaow.hpp"
#include "third_party/bmi/bmi.hxx"

namespace miaow {

// External BMI interface implemented by the model classes
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
 * @brief Resolve the bundled subprocess executable that runs dynamic BMI tasks.
 * @return Existing configured build or installation path.
 * @throws std::runtime_error if neither configured executable exists.
 */
std::string bmi_subprocess_executable_path();

/**
 * @brief Build consecutive BMI operations executed by one initialized unit.
 *
 * Read outputs are concatenated in operation order. Inputs follow the instruction
 * argument in the sequence task and can reference outputs of other tasks.
 */
class BmiSequence {
  public:
    /// Advance the model by one BMI time step.
    void update() {
        append("update");
    }
    /// Append the complete variable to the sequence result.
    void get_value(const std::string& name) {
        append("get_value", name);
    }
    /// Append selected elements in the supplied zero-based index order.
    void get_value_at_indices(const std::string& name, const std::vector<int>& indices) {
        append("get_value_at_indices", name, indices);
    }
    /// Set a complete variable from literal values using ordinary BMI conversions.
    template <class Value>
    void set_value(const std::string& name, const std::vector<Value>& values) {
        append("set_value", name, values);
    }
    /// Set a variable from a slice of the zero-based positional sequence input.
    void set_value_from_input(const std::string& name, std::size_t input,
                              std::size_t offset, std::size_t count) {
        append("set_value", name, input_slice(input, offset, count));
    }
    /// Read indices from a slice of a positional integer-array input.
    void get_value_at_indices_from_input(const std::string& name, std::size_t input,
                                         std::size_t offset, std::size_t count) {
        append("get_value_at_indices", name, input_slice(input, offset, count));
    }
    /// Move the encoded program into one task argument or a reusable task output.
    caf::config_value instructions() && {
        return caf::config_value{std::move(instructions_)};
    }

  private:
    static caf::config_value::dictionary input_slice(std::size_t input, std::size_t offset,
                                                     std::size_t count) {
        caf::config_value::dictionary slice;
        slice.insert("input", input);
        slice.insert("offset", offset);
        slice.insert("count", count);
        return slice;
    }
    template <class... Args> void append(Args... args) {
        caf::config_value::list instruction;
        (instruction.emplace_back(std::move(args)), ...);
        instructions_.emplace_back(std::move(instruction));
    }
    caf::config_value::list instructions_;
};

/**
 * @brief Common operation execution and model lifetime for BMI executors.
 *
 * Each unit retains its initialized model between operations. Complete `run` tasks
 * construct a fresh model and release it before returning their result.
 */
class BmiTaskExecutor : public TaskExecutor {
  public:
    /// Attempt finalization when an initialized model remains at unit shutdown
    ~BmiTaskExecutor() override;

    /// Execute a serial `sequence`, cache its instructions, or call a BMI operation.
    caf::result<TaskResult> execute(const Task& task) override;

  protected:
    using ModelFactory = std::function<std::unique_ptr<bmi_api::Bmi>(const Task&)>;

    /// Store a factory invoked only for initialization or a complete run
    explicit BmiTaskExecutor(ModelFactory model_factory);

  private:
    /// Execute one operation or a serial sequence using the retained model.
    TaskResult execute_operation(const Task& task);

    // Factory captures must outlive model finalization and destruction
    ModelFactory model_factory_;
    std::unique_ptr<bmi_api::Bmi> model_;
    bool model_failed_{false};

    // Programs belong to one initialized model; IDs are not reused after finalization
    std::unordered_map<std::int64_t, caf::config_value::list> cached_sequences_;
    std::int64_t next_sequence_id_{0};
};

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
class StaticBmiTaskExecutor : public BmiTaskExecutor {
  public:
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
        requires std::constructible_from<BmiModelType, const ModelConstructorArgs&...>
    explicit StaticBmiTaskExecutor(ModelConstructorArgs... model_constructor_args)
        : BmiTaskExecutor{[... args = std::move(model_constructor_args)](
                              const Task&) -> std::unique_ptr<bmi_api::Bmi> {
              return std::make_unique<BmiModelType>(args...);
          }} {}
};

/**
 * @brief Task executor that runs dynamically loaded BMI plugin tasks.
 *
 * Individual operations retain an initialized C++ or Fortran model and its plugin in
 * the current process. Complete `run` tasks use a fresh model. The bundled subprocess
 * executable hosts this executor through `TaskExecutorRegistry::add_subprocess()`.
 */
class DynamicBmiTaskExecutor : public BmiTaskExecutor {
  public:
    /// Construct a dynamic BMI task executor
    DynamicBmiTaskExecutor();
};

} // namespace miaow
