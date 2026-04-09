#pragma once

#include <cstdint>
#include <vector>

#include "miaow.hpp"

namespace miaow::io {

void write_job_metadata(std::uint64_t run_id,
                        JobId job_id,
                        const RuntimeOptions& runtime_options,
                        const JobResult& job_result);

} // namespace miaow::io
