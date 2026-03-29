#pragma once
// Canonical chart format detection from raw text content.
// All chart loaders (main app, dump_frame, Python API) share this single implementation.
#include <string>
#include <nlohmann/json.hpp>

namespace phigros::chart {

// Inspect raw text and return its chart format: "rpe", "official", "pec", or "" (unknown).
// PEC is detected by leading character (b/c/n/#/digit); JSON is parsed to distinguish RPE vs official.
inline std::string detect_format_text(const std::string& text) {
    size_t pos = text.find_first_not_of(" \t\r\n");
    if (pos == std::string::npos) return "";
    char c = text[pos];
    if (c == 'b' || c == 'c' || c == 'n' || c == '#' || (c >= '0' && c <= '9'))
        return "pec";
    try {
        auto j = nlohmann::json::parse(text);
        if (j.contains("META") || j.contains("BPMList")) return "rpe";
        return "official";
    } catch (...) {
        return "pec";
    }
}

} // namespace phigros::chart
