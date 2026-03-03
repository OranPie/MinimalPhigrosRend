#pragma once
#include "phigros/core/types.hpp"
#include <nlohmann/json.hpp>

namespace phigros::chart {
// Load Official format (AT.json formatVersion 1 or 3)
ChartData load_official(const nlohmann::json& data, int W, int H);
} // namespace phigros::chart
