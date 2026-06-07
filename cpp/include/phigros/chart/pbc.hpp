#pragma once
#include "phigros/core/types.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace phigros::chart {

// Load Phira Binary Chart (PBC) bytes.
// The binary layout follows TeamFlos/prpr src/bin.rs.
ChartData load_pbc_bytes(const std::vector<uint8_t>& data, int W, int H);
ChartData load_pbc(const std::string& path, int W, int H);

} // namespace phigros::chart
