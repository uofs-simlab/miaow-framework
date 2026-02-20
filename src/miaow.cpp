#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

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

#include "miaow.hpp"
#include "third_party/bmi.hxx"

namespace miaow {

static std::once_flag caf_init_flag;

static void ensure_caf_initialized() {
    caf::init_global_meta_objects<caf::id_block::core_module>();
}

static void initialize_caf_once() {
    std::call_once(caf_init_flag, ensure_caf_initialized);
}

class DefaultSimulationUnit final : public SimulationUnit {
public:
    std::string run(const std::string& config_file) override {
        // This config path is what a BMI-backed simulation unit would pass to Initialize().
        return std::string("Hello from miaow! (config: ") + config_file + ")";
    }
};

// Leaf worker actor in the orchestrator -> job -> simulation unit hierarchy.
static caf::behavior simulation_unit_actor(caf::event_based_actor* /*self*/,
                                           std::string config_file,
                                           std::shared_ptr<SimulationUnit> simulation_unit_object) {
    return {
        // `caf::get_atom` is the message type this actor handles.
        [config_file, simulation_unit_object](caf::get_atom) -> std::string {
            return simulation_unit_object->run(config_file);
        }
    };
}

// Per-job supervisor that owns simulation unit actors.
static caf::behavior job_actor(caf::event_based_actor* self,
                               std::string config_file,
                               std::chrono::milliseconds request_timeout,
                               std::shared_ptr<SimulationUnit> simulation_unit_object) {
    caf::actor simulation_unit = self->spawn(simulation_unit_actor,
                                             std::move(config_file),
                                             std::move(simulation_unit_object));
    return {
        /* When the job_actor receives a message of type get_atom, forward to the
        simulation unit and return the result (in this case a string) back to the caller
        (i.e. the orchestrator). */
        [=](caf::get_atom) -> caf::result<std::string> {
            /* The promise lets the handler return immediately while the reply is
            delivered later, keeping job_actor responsive as it waits on the
            simulation unit. */
            caf::typed_response_promise<std::string> response_promise =
                self->make_response_promise<std::string>();
            /* Forward a request to the simulation unit and deliver the response back to
            the caller (i.e. the orchestrator) when it arrives. Timeout after 1 second if
            the simulation unit doesn't reply in time. */
            // `_v` is the singleton value of the atom type `caf::get_atom`.
            self->mail(caf::get_atom_v)
                .request(simulation_unit, request_timeout)
                .then(
                    // Forward the successful reply from simulation_unit back to the caller.
                    [response_promise](const std::string& message) mutable {
                        response_promise.deliver(message);
                    },
                    // Forward any error from simulation_unit back to the caller.
                    [response_promise](const caf::error& err) mutable {
                        response_promise.deliver(err);
                    });
            return response_promise;
        }
    };
}

// Top-level coordinator that owns job actors.
static caf::behavior orchestrator_actor(caf::event_based_actor* self,
                                        std::string config_file,
                                        std::chrono::milliseconds request_timeout,
                                        std::shared_ptr<SimulationUnit> simulation_unit_object) {
    caf::actor job = self->spawn(job_actor,
                                 std::move(config_file),
                                 request_timeout,
                                 std::move(simulation_unit_object));
    return {
        [=](caf::get_atom) -> caf::result<std::string> {
            /* Similarly to the job actor, the orchestrator uses the promise pattern to
            forward replies from job_actor. */
            caf::typed_response_promise<std::string> response_promise =
                self->make_response_promise<std::string>();
            /* Forward a request to the job_actor and deliver the response back to the
            caller when it arrives. Timeout after 1 second if the job_actor doesn't reply
            in time. Note that get_atom is the message type; _v gets the singleton value of
            that type. */
            self->mail(caf::get_atom_v).request(job, request_timeout)
                .then(
                    // Forward the successful reply from the job_actor back to the caller.
                    [response_promise](const std::string& message) mutable {
                        response_promise.deliver(message);
                    },
                    // Forward any error from the job_actor back to the caller.
                    [response_promise](const caf::error& err) mutable {
                        response_promise.deliver(err);
                    });
            return response_promise;
        }
    };
}

Runtime::Runtime(RuntimeOptions options)
    : Runtime(std::make_shared<DefaultSimulationUnit>(), std::move(options)) {
}

Runtime::Runtime(std::shared_ptr<SimulationUnit> simulation_unit, RuntimeOptions options)
    : simulation_unit_(std::move(simulation_unit)),
      runtime_options_(std::move(options)) {
    initialize_caf_once();
    if (!simulation_unit_) {
        simulation_unit_ = std::make_shared<DefaultSimulationUnit>();
    }
}

std::string Runtime::run_workflow(const std::string& config_file) const {
    caf::actor_system_config actor_system_config;
    // actor_system owns schedulers, mailboxes, and all local actors for this call.
    caf::actor_system system{actor_system_config};
    caf::actor orchestrator = system.spawn(orchestrator_actor,
                                           config_file,
                                           runtime_options_.request_timeout,
                                           simulation_unit_);
    caf::scoped_actor caller{system};

    std::string result;
    // Request the top-level actor and block in this scope until a reply or timeout.
    caller->mail(caf::get_atom_v)
        .request(orchestrator, runtime_options_.request_timeout)
        .receive(
            [&](const std::string& message) {
                result = message;
            },
            [&](const caf::error& err) {
                // Handles an error from orchestrator, job, or simulation unit actor.
                result = std::string("CAF error: ") + caf::to_string(err);
            });

    return result;
}

} // namespace miaow
