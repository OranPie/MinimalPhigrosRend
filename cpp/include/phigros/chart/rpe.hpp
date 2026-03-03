#pragma once
#include "phigros/core/types.hpp"
#include <nlohmann/json.hpp>

namespace phigros::chart {
// Load RPE format chart
ChartData load_rpe(const nlohmann::json& data, int W, int H, int rpe_easing_shift = 0);
} // namespace phigros::chart
