#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace miaow::io {

inline std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    const auto begin = std::find_if(value.begin(), value.end(), not_space);
    if (begin == value.end()) {
        return "";
    }
    const auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
    return std::string(begin, end);
}

} // namespace miaow::io
