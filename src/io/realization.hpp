#pragma once

#include <map>
#include <string>
#include <vector>

namespace miaow::io {

struct RealizationSubdomain {
    std::string subdomain_id_;
    std::string config_path_;
    std::string forcing_path_;
};

struct RealizationConfig {
    std::string realization_path_;
    double time_step_size_{0.0};
    std::string time_units_;
    std::map<std::string, std::string> variables_names_map_;
    std::vector<RealizationSubdomain> subdomains_;
};

RealizationConfig read_realization_config(const std::string& realization_path);

} // namespace miaow::io
