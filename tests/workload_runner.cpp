#include <chrono>

#include "miaow.hpp"
#include <utility>
#include <nlohmann/json.hpp>

#if defined(MIAOW_TEST_HELLO)
#include "../examples/00_hello/hello.hpp"
#elif defined(MIAOW_TEST_HEAT)
#include "../examples/01_heat/heat.hpp"
#elif defined(MIAOW_TEST_DYNAMIC_BMI)
#include "../examples/03_dynamic_bmi/dynamic_bmi.hpp"
#elif defined(MIAOW_TEST_JOB_CONTROLLER)
#include "../examples/07_job_controller/job_controller.hpp"
#elif defined(MIAOW_TEST_BMI_OPERATIONS)
#include "../examples/09_bmi_operations/bmi_operations.hpp"
#elif defined(MIAOW_TEST_MODEL_COUPLING)
#include "../examples/10_model_coupling/model_coupling.hpp"
#elif defined(MIAOW_TEST_BMI_DISPATCH_DRIVER)
#include "bmi_task_dispatch.hpp"
#elif defined(MIAOW_TEST_SIMULATION_UNIT_LIFECYCLE_DRIVER)
#include "simulation_unit_lifecycle.hpp"
#endif

namespace miaow::tests {

// Each checker links the same model and plugin configuration as its example or driver
miaow::JobRecord run_workload(const nlohmann::json& config, miaow::RuntimeOptions options) {
    miaow::TaskExecutorRegistry registry;
#if defined(MIAOW_TEST_HELLO)
    miaow::Job job = miaow::examples::hello::make_job(registry, config);
#elif defined(MIAOW_TEST_HEAT)
    miaow::Job job = miaow::examples::heat::make_job(registry);
#elif defined(MIAOW_TEST_DYNAMIC_BMI)
    miaow::Job job = miaow::examples::dynamic_bmi::make_job(registry, config);
#elif defined(MIAOW_TEST_JOB_CONTROLLER)
    miaow::Job job = miaow::examples::controller::make_job(registry, config);
#elif defined(MIAOW_TEST_BMI_OPERATIONS)
    miaow::Job job = miaow::examples::bmi_operations::make_job(registry);
#elif defined(MIAOW_TEST_MODEL_COUPLING)
    std::vector<std::array<miaow::TaskId, 2>> temperature_queries;
    std::array<miaow::TaskId, 2> time_queries{};
    miaow::Job job =
        miaow::examples::model_coupling::make_job(registry, config, temperature_queries,
                                                  time_queries);
#elif defined(MIAOW_TEST_BMI_DISPATCH_DRIVER)
    miaow::Job job = miaow::tests::bmi_dispatch::make_job(registry, config);
#elif defined(MIAOW_TEST_SIMULATION_UNIT_LIFECYCLE_DRIVER)
    options.subprocess_shutdown_grace_period_ = std::chrono::milliseconds{
        config.value("shutdown_grace_period_ms",
                     options.subprocess_shutdown_grace_period_.count())};
    miaow::Job job = miaow::tests::unit_lifecycle::make_job(registry, config);
#endif
    miaow::Runtime runtime(registry, options);
    miaow::JobHandle job_handle = runtime.submit_job(std::move(job));
    return job_handle.get();
}

} // namespace miaow::tests
