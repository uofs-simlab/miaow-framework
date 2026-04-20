#pragma once

#include <string>
#include <vector>

namespace miaow::io {

struct RealizationSubdomain {
    std::string subdomain_id_;
    std::string config_path_;
};

struct RealizationConfig {
    std::string realization_path_;
    std::vector<RealizationSubdomain> subdomains_;
};

RealizationConfig read_realization_config(const std::string& realization_path);

} // namespace miaow::io
