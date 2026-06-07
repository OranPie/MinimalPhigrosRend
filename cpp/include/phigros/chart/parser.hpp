#pragma once
// Convenience multi-include: pull in all chart format parsers at once.
#include "phigros/chart/official.hpp"
#include "phigros/chart/rpe.hpp"
#include "phigros/chart/pec.hpp"
#include "phigros/chart/pbc.hpp"

namespace phigros::chart {
    // Aliases already in phigros::chart namespace from individual headers
    inline ChartData parse_official(const nlohmann::json& j, int W, int H) {
        return load_official(j, W, H);
    }
    inline ChartData parse_rpe(const nlohmann::json& j, int W, int H, int shift = 0) {
        return load_rpe(j, W, H, shift);
    }
    inline ChartData parse_pec(const std::string& path, int W, int H) {
        return load_pec(path, W, H);
    }
    inline ChartData parse_pbc(const std::vector<uint8_t>& data, int W, int H) {
        return load_pbc_bytes(data, W, H);
    }
}
