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
#include "phigros/chart/chart_loader.hpp"
#include "phigros/chart/pbc.hpp"
#include "phigros/core/types.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>
#include <miniz.h>
#include <nlohmann/json.hpp>

using namespace phigros;
using json = nlohmann::json;
namespace fs = std::filesystem;

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

static void test_info_yml_format_priority() {
    std::cout << "  info.yml format priority\n";
    auto zip_path = fs::temp_directory_path() / "phigros_info_format_pbc.zip";
    fs::remove(zip_path);

    mz_zip_archive writer{};
    bool ok = mz_zip_writer_init_file(&writer, zip_path.string().c_str(), 0);
    const char info[] =
        "name: FormatPriority\n"
        "chart: chart.bin\n"
        "format: pbc\n"
        "music: music.ogg\n"
        "illustration: bg.png\n";
    const unsigned char chart_bytes[] = {0, 0, 0, 0, 0};
    ok = ok && mz_zip_writer_add_mem(&writer, "Pack/info.yml", info, sizeof(info) - 1, MZ_BEST_SPEED);
    ok = ok && mz_zip_writer_add_mem(&writer, "Pack/chart.bin", chart_bytes, sizeof(chart_bytes), MZ_BEST_SPEED);
    ok = ok && mz_zip_writer_finalize_archive(&writer);
    mz_zip_writer_end(&writer);
    CHECK(ok, "format_priority_zip_write");

    auto entries = chart::load_zip_chart(zip_path);
    CHECK(entries.size() == 1, "format_priority_entry_count");
    if (!entries.empty()) {
        CHECK(entries[0].format == chart::ChartFormat::Pbc, "info_yml_format_pbc_wins");
        CHECK(entries[0].metadata.format == "pbc", "info_yml_metadata_format");
        CHECK(entries[0].chart_path.find("Pack/chart.bin") != std::string::npos,
              "info_yml_chart_path_resolved");
    }
    fs::remove(zip_path);
}

static void append_u8(std::vector<uint8_t>& out, uint8_t v) {
    out.push_back(v);
}

static void append_i32(std::vector<uint8_t>& out, int32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
}

static void append_f32(std::vector<uint8_t>& out, float v) {
    uint8_t bytes[4];
    std::memcpy(bytes, &v, 4);
    out.insert(out.end(), bytes, bytes + 4);
}

static void append_uleb(std::vector<uint8_t>& out, uint64_t v) {
    do {
        uint8_t b = static_cast<uint8_t>(v & 0x7f);
        v >>= 7;
        if (v) b |= 0x80;
        out.push_back(b);
    } while (v);
}

static void append_empty_anim(std::vector<uint8_t>& out) {
    append_u8(out, 0);
}

static void append_empty_object(std::vector<uint8_t>& out) {
    for (int i = 0; i < 6; ++i) append_empty_anim(out);
}

static std::vector<uint8_t> make_minimal_pbc() {
    std::vector<uint8_t> out;
    append_f32(out, 0.0f); // offset
    append_uleb(out, 1);   // lines

    append_empty_object(out);
    append_u8(out, 0);     // normal line
    append_empty_anim(out); // height
    append_uleb(out, 1);   // notes

    append_empty_object(out);
    append_u8(out, 0);     // click/tap
    append_uleb(out, 1000); // time delta ms
    append_f32(out, 0.0f); // height
    append_u8(out, 0);     // speed default
    append_u8(out, 1);     // above
    append_u8(out, 0);     // fake

    append_empty_anim(out); // color
    append_uleb(out, 0);    // parent none
    append_u8(out, 1);      // show_below
    append_u8(out, 0);      // attach_ui
    append_u8(out, 8);      // ctrl object marker
    for (int i = 0; i < 4; ++i) append_empty_anim(out);
    append_empty_anim(out); // incline
    append_i32(out, 0);     // z_index

    append_u8(out, 0);      // pe_alpha_extension
    append_u8(out, 0);      // hold_partial_cover
    return out;
}

static void test_minimal_pbc() {
    std::cout << "  minimal PBC\n";
    auto bytes = make_minimal_pbc();
    CHECK(chart::detect_chart_format_bytes(bytes) == chart::ChartFormat::Pbc,
          "minimal_pbc_detect_binary");
    auto chart_data = chart::load_pbc_bytes(bytes, 1280, 720);
    CHECK(chart_data.metadata.format == "pbc", "minimal_pbc_metadata_format");
    CHECK(chart_data.lines.size() == 1, "minimal_pbc_line_count");
    CHECK(chart_data.notes.size() == 1, "minimal_pbc_note_count");
    if (!chart_data.notes.empty()) {
        CHECK(chart_data.notes[0].kind == 1, "minimal_pbc_note_kind");
        CHECK(std::abs(chart_data.notes[0].t_hit - 1.0) < 1e-6, "minimal_pbc_note_time");
    }
}

static std::string resolve_chart_path(const char* rel) {
    fs::path p(rel);
    if (fs::exists(p)) return p.string();
    fs::path from_cpp_build = fs::path("..") / ".." / rel;
    if (fs::exists(from_cpp_build)) return from_cpp_build.string();
    fs::path from_cpp_dir = fs::path("..") / rel;
    if (fs::exists(from_cpp_dir)) return from_cpp_dir.string();
    return p.string();
}

int main() {
    const int W = 1280, H = 720;

    std::cout << "=== Parser correctness tests ===\n\n";
    test_info_yml_format_priority();
    test_minimal_pbc();

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
        std::string path = resolve_chart_path(ref.path);
        std::string ref_path = ref.path;
        std::string name = ref_path.substr(7, ref_path.rfind('/') - 7); // charts/<name>

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
