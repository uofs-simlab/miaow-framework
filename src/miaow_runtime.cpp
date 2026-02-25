#include <algorithm>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <caf/all.hpp>
#include <caf/ipv4_address.hpp>
#include <caf/ipv4_endpoint.hpp>
#include <caf/ipv4_subnet.hpp>
#include <caf/ipv6_address.hpp>
#include <caf/ipv6_endpoint.hpp>
#include <caf/ipv6_subnet.hpp>
#include <caf/json_array.hpp>
#include <caf/json_object.hpp>
#include <caf/json_value.hpp>
#include <caf/type_id.hpp>

#include "miaow_internal.hpp"

namespace miaow {

static std::once_flag caf_init_flag;

/* Initializes CAF global meta objects exactly once in a thread-safe way, so that
multiple Runtime instances can be created across threads without race conditions */
static void initialize_caf_once() {
    std::call_once(caf_init_flag, [] {
        caf::init_global_meta_objects<caf::id_block::core_module>();
        caf::init_global_meta_objects<caf::id_block::miaow_module>();
    });
}

// Constructor for the MIAOW runtime, which initializes CAF and checks the simulation unit
Runtime::Runtime(SimulationUnitExecutor simulation_unit_executor, RuntimeOptions options)
    : simulation_unit_executor_(std::move(simulation_unit_executor)),
      runtime_options_(std::move(options)) {
    initialize_caf_once();
    if (!simulation_unit_executor_) {
        throw std::invalid_argument("Runtime requires a non-null SimulationUnitExecutor");
    }
}

// Running a single-task workflow is just a wrapper around the multi-task version
std::string Runtime::run_workflow(const std::string& config_file) const {
    const std::vector<std::string> results =
        run_workflow(std::vector<std::string>{config_file});
    if (results.empty()) {
        return "No workflow results";
    }
    return results.front();
}

std::vector<std::string> Runtime::run_workflow(const std::vector<std::string>& config_files) const {
    if (config_files.empty()) {
        return {};
    }

    // actor_system owns schedulers, mailboxes, and all local actors for this call
    caf::actor_system_config actor_system_config;
    caf::actor_system system{actor_system_config};

    /* Create the top-level orchestrator actor for this workflow, which creates a job
    actor, which creates worker actors that run simulation units. The workflow config files
    and runtime options are passed to all actors in the hierarchy so they can be used for
    dispatching work and configuring timeouts. The orchestrator forwards the final results
    back to this function, which returns them to the caller */
    caf::actor orchestrator = system.spawn(internal::orchestrator_actor,
                                           config_files,
                                           runtime_options_,
                                           simulation_unit_executor_);
    caf::scoped_actor caller{system};

    std::vector<std::string> task_results(config_files.size());
    // Request the top-level actor and block in this scope until a reply or timeout
    caller->mail(caf::get_atom_v)
        .request(orchestrator, runtime_options_.request_timeout_)
        .receive(
            [&](const std::vector<std::string>& messages) {
                task_results = messages;
            },
            [&](const caf::error& err) {
                // Handles an error from orchestrator, job, or simulation unit actor
                std::fill(task_results.begin(),
                          task_results.end(),
                          std::string("CAF error: ") + caf::to_string(err));
            });

    return task_results;
}

} // namespace miaow
