#pragma once

namespace miaow {
struct RuntimeOptions;
}

// Run both forcing formats with the same model settings and compare their CSV outputs
int compare_forcing(const miaow::RuntimeOptions& runtime_options);
