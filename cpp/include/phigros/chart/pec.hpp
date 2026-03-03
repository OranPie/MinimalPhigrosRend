#pragma once
#include "phigros/core/types.hpp"
#include <string>

namespace phigros::chart {
// Load PEC format chart from text content
ChartData load_pec_text(const std::string& text, int W, int H);
// Load PEC from file path
ChartData load_pec(const std::string& path, int W, int H);
} // namespace phigros::chart
