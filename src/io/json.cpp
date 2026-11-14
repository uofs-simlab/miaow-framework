#include "miaow_io.hpp"

#include <concepts>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace miaow {

nlohmann::json to_json(const caf::config_value& value) {
    return std::visit(
        [](const auto& item) -> nlohmann::json {
            using Value = std::remove_cvref_t<decltype(item)>;
            if constexpr (std::same_as<Value, caf::config_value::dictionary>) {
                nlohmann::json object = nlohmann::json::object();
                for (const auto& [key, child] : item) {
                    object[key] = to_json(child);
                }
                return object;
            } else if constexpr (std::same_as<Value, caf::config_value::list>) {
                nlohmann::json::array_t array;
                array.reserve(item.size());
                for (const caf::config_value& child : item) {
                    array.push_back(to_json(child));
                }
                return nlohmann::json(std::move(array));
            } else if constexpr (std::same_as<Value, caf::none_t>) {
                return nullptr;
            } else if constexpr (std::same_as<Value, caf::timespan>) {
                return std::to_string(item.count()) + "ns";
            } else if constexpr (std::same_as<Value, caf::uri>) {
                return caf::to_string(item);
            } else {
                return item;
            }
        },
        value.get_data());
}

} // namespace miaow
