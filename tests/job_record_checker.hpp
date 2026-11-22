#pragma once

#include <boost/test/unit_test.hpp>
#include <nlohmann/json.hpp>

#include "miaow.hpp"
#include "miaow_io.hpp"
#include "core/build_info.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace miaow::tests {

inline nlohmann::json check_job_record(const miaow::JobRecord& record,
                                       const std::filesystem::path& record_path) {
    // Check the report schema and its correspondence with the returned record
    std::ifstream input{record_path};
    const nlohmann::json job_record = nlohmann::json::parse(input);
    BOOST_TEST(job_record.at("job_id").is_number_unsigned());
    BOOST_TEST(job_record.at("miaow_version").get<std::string>() ==
               miaow::core::build_version);
    BOOST_TEST(job_record.at("summary").at("task_count") == record.task_results_.size());
    const auto& profile = job_record.at("profile");
    const std::vector<std::string> columns{"task_id",
                                           "unit_id",
                                           "operation_id",
                                           "status",
                                           "dispatch_offset_ns",
                                           "completion_offset_ns"};
    BOOST_TEST(profile.at("columns") == columns);
    BOOST_REQUIRE(profile.at("tasks").is_array());
    BOOST_REQUIRE_EQUAL(profile.at("tasks").size(), record.task_results_.size());
    nlohmann::json failures = nlohmann::json::array();
    nlohmann::json artifacts = nlohmann::json::array();
    for (std::size_t id = 0; id < record.task_results_.size(); ++id) {
        const miaow::TaskResult& result = *record.task_results_.at(id);
        const auto& row = profile.at("tasks").at(id);
        BOOST_TEST(row.at(0).is_number_unsigned());
        BOOST_TEST(row.at(0).get<std::size_t>() == id);
        BOOST_TEST(row.at(1).is_number_unsigned());
        BOOST_TEST(row.at(3) == miaow::task_status_to_string(result.status_));
        const miaow::TaskTiming& timing = record.task_timings_.at(id);
        BOOST_TEST(row.at(4) == (timing.dispatch_offset_ns_ < 0
                                     ? nlohmann::json(nullptr)
                                     : nlohmann::json(timing.dispatch_offset_ns_)));
        BOOST_TEST(row.at(5) == (timing.completion_offset_ns_ < 0
                                     ? nlohmann::json(nullptr)
                                     : nlohmann::json(timing.completion_offset_ns_)));
        if (result.status_ == miaow::TaskStatus::failed) {
            failures.push_back({{"task_id", id}, {"message", result.message_}});
        }
        if (!result.artifact_paths_.empty()) {
            artifacts.push_back({{"task_id", id}, {"paths", result.artifact_paths_}});
        }
    }
    BOOST_TEST(job_record.at("failures") == failures);
    BOOST_TEST(job_record.at("artifacts") == artifacts);
    BOOST_TEST(job_record.at("summary").at("failed_task_count") == failures.size());
    return job_record;
}

inline void check_successful_tasks(const miaow::JobRecord& record) {
    for (std::size_t id = 0; id < record.task_results_.size(); ++id) {
        BOOST_TEST_CONTEXT("task " << id) {
            BOOST_TEST(
                (record.task_results_.at(id)->status_ == miaow::TaskStatus::succeeded));
            BOOST_TEST(record.task_timings_.at(id).dispatch_offset_ns_ >= 0);
        }
    }
}

} // namespace miaow::tests
