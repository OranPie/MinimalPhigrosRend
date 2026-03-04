// Parser correctness tests (6A1).
// Loads each of the 6 local charts and asserts structural properties:
//   - correct note count, line count, offset
//   - notes sorted by t_hit
//   - all note kinds in [1,4] and line_ids in valid range
//   - format detection returns expected result
//
// Usage: test_parser (no arguments; chart paths are hardcoded relative to cwd)

#include "phigros/chart/official.hpp"
#include "phigros/chart/rpe.hpp"
#include "phigros/chart/pec.hpp"
#include "phigros/core/types.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <algorithm>
#include <nlohmann/json.hpp>

using namespace phigros;
using json = nlohmann::json;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { ++g_fail; std::cerr << "  FAIL: " << (msg) << "\n"; } \
    else { ++g_pass; } \
} while(0)

// ---- Format detection (shared logic) ----
static std::string detect_format(const std::string& path) {
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".pec")
        return "pec";
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::string text((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    size_t pos = text.find_first_not_of(" \t\r\n");
    if (pos == std::string::npos || text[pos] != '{') return "pec_text";
    try {
        auto j = json::parse(text);
        if (j.contains("META") || j.contains("BPMList")) return "rpe";
        if (j.contains("judgeLineList") || j.contains("formatVersion")) return "official";
    } catch (...) { return "pec_text"; }
    return "official";
}

static ChartData load_chart(const std::string& path, int W, int H) {
    std::string fmt = detect_format(path);
    if (fmt == "pec")       return chart::load_pec(path, W, H);
    if (fmt == "pec_text") {
        std::ifstream f(path);
        std::string t((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return chart::load_pec_text(t, W, H);
    }
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);
    auto j = json::parse(f);
    if (fmt == "official") return chart::load_official(j, W, H);
    if (fmt == "rpe")      return chart::load_rpe(j, W, H);
    throw std::runtime_error("Unknown format");
}

// ---- Structural invariant checks (format-agnostic) ----
static void check_chart_invariants(const ChartData& chart,
                                   int expected_lines, int expected_notes,
                                   double expected_offset, const std::string& name) {
    std::cout << "  " << name << ": lines=" << chart.lines.size()
              << " notes=" << chart.notes.size()
              << " offset=" << chart.offset << "\n";

    // Note count
    int playable = 0;
    for (auto& n : chart.notes) if (!n.fake) ++playable;
    CHECK(playable == expected_notes,
          name + ": note count=" + std::to_string(expected_notes)
          + " (got " + std::to_string(playable) + ")");

    // Line count
    CHECK(static_cast<int>(chart.lines.size()) == expected_lines,
          name + ": line count=" + std::to_string(expected_lines)
          + " (got " + std::to_string(chart.lines.size()) + ")");

    // Offset
    CHECK(std::abs(chart.offset - expected_offset) < 1e-9,
          name + ": offset=" + std::to_string(expected_offset)
          + " (got " + std::to_string(chart.offset) + ")");

    // Notes sorted by t_hit
    bool sorted = std::is_sorted(chart.notes.begin(), chart.notes.end(),
        [](const Note& a, const Note& b) { return a.t_hit < b.t_hit; });
    CHECK(sorted, name + ": notes sorted by t_hit");

    // All note kinds valid [1,4] and line_ids in range
    int bad_kinds = 0, bad_lines = 0;
    for (auto& n : chart.notes) {
        if (n.fake) continue;
        if (n.kind < 1 || n.kind > 4) ++bad_kinds;
        if (n.line_id < 0 || n.line_id >= static_cast<int>(chart.lines.size()))
            ++bad_lines;
    }
    CHECK(bad_kinds == 0, name + ": all note kinds in [1,4] (" + std::to_string(bad_kinds) + " bad)");
    CHECK(bad_lines == 0, name + ": all line_ids valid (" + std::to_string(bad_lines) + " bad)");

    // All t_hit values positive
    int bad_thit = 0;
    for (auto& n : chart.notes)
        if (!n.fake && n.t_hit < -1.0) ++bad_thit;
    CHECK(bad_thit == 0, name + ": all t_hit >= -1.0 (" + std::to_string(bad_thit) + " bad)");

    // All t_end >= t_hit
    int bad_tend = 0;
    for (auto& n : chart.notes)
        if (!n.fake && n.t_end < n.t_hit - 1e-6) ++bad_tend;
    CHECK(bad_tend == 0, name + ": all t_end >= t_hit (" + std::to_string(bad_tend) + " bad)");
}

int main() {
    const int W = 1280, H = 720;

    std::cout << "=== Parser correctness tests ===\n\n";

    // Struct: {path, expected_lines, expected_notes, expected_offset, format}
    struct ChartRef {
        const char* path;
        int lines, notes;
        double offset;
        const char* expected_fmt;
    };

    ChartRef refs[] = {
        {"charts/AbsoluTedisoRdeR.AcuteDisarray/IN.json", 24, 1600, 0.0, "official"},
        {"charts/Radiance.Nhato/IN.json",                 24,  667, 0.0, "official"},
        {"charts/Aleph0.LeaF/IN.json",                    20,  885, 0.0, "official"},
        {"charts/ATHAZA.LeaF/IN.json",                    24, 1137, 0.0, "official"},
        {"charts/BetterGraphicAnimation.\xe3\x83\xab\xe3\x82\xbc/IN.json",
                                                          11,  616, 0.0, "official"},
        {"charts/Rrharil.TeamGrimoire/IN.json",           18, 1300, 0.0, "official"},
    };

    for (auto& ref : refs) {
        std::string path = ref.path;
        std::string name = path.substr(7, path.rfind('/') - 7); // charts/<name>

        // Format detection
        std::string fmt = detect_format(path);
        CHECK(fmt == ref.expected_fmt,
              name + ": format=" + std::string(ref.expected_fmt) + " (got " + fmt + ")");

        // Load and check
        try {
            ChartData chart = load_chart(path, W, H);
            check_chart_invariants(chart, ref.lines, ref.notes, ref.offset, name);
        } catch (const std::exception& e) {
            ++g_fail;
            std::cerr << "  FAIL: " << name << " load exception: " << e.what() << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "=== Total checks: " << g_pass << " passed, "
              << g_fail << " failed ===\n";
    return g_fail > 0 ? 1 : 0;
}
