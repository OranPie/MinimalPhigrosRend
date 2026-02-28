#pragma once

#include "phic/core/types.hpp"

#include <string>

namespace phic {

struct ParseChartResult {
    ChartData chart;
    std::string error;
    bool ok = false;
};

ParseChartResult parse_chart_bytes(const std::string& payload, const std::string& format_hint);

}  // namespace phic
