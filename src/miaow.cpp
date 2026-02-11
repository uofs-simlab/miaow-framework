#include <chrono>
#include <mutex>
#include <string>

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

namespace {

std::once_flag caf_init_flag;

void ensure_caf_initialized() {
    caf::init_global_meta_objects<caf::id_block::core_module>();
}

caf::behavior greeter(caf::event_based_actor* self) {
    return {
        [=](caf::get_atom) -> std::string {
            return "Hello from miaow!";
        }
    };
}

} // namespace

void miaow_init() {
    std::call_once(caf_init_flag, ensure_caf_initialized);
}

std::string miaow_actor_hello() {
    miaow_init();
    caf::actor_system_config cfg;
    caf::actor_system system{cfg};
    auto greeter_actor = system.spawn(greeter);
    caf::scoped_actor self{system};
    std::string result;
    self->mail(caf::get_atom_v).request(greeter_actor, std::chrono::seconds(1)).receive(
        [&](const std::string& msg) {
            result = msg;
        },
        [&](const caf::error& err) {
            result = std::string("CAF error: ") + caf::to_string(err);
        }
    );
    return result;
}
