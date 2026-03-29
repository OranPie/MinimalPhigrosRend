// dump_frame — headless chart state probe tool (no SDL required).
// Simulates autoplay to a given time and dumps all line/hold/note render state as JSON.
//
// Usage: dump_frame <chart_path> <t_seconds> [W] [H]
//   chart_path : path to RPE/official/PEC JSON, .phbc, or zip reference (chart.zip:file.json)
//   t_seconds  : chart time to probe (seconds from chart start)
//   W H        : virtual render resolution (default 1280 720)
//
// Output: JSON to stdout with keys:
//   t, W, H, lines[], frame_notes[], all_holds[]

#include "phigros/core/logger.hpp"
#include "phigros/chart/parser.hpp"
#include "phigros/chart/chart_loader.hpp"
#include "phigros/chart/phbc_io.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/visibility.hpp"
#include "phigros/engine/hold_logic.hpp"
#include "phigros/engine/exact_autoplay.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/render/renderer.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

using namespace phigros;
using namespace phigros::render;
using namespace phigros::engine;
using json = nlohmann::json;
namespace fs = std::filesystem;

static std::string detect_format_text(const std::string& text) {
    size_t pos = text.find_first_not_of(" \t\r\n");
    if (pos == std::string::npos) return "";
    char c = text[pos];
    if (c == 'b' || c == 'c' || c == 'n' || c == '#' || (c >= '0' && c <= '9'))
        return "pec";
    try {
        auto j = json::parse(text);
        if (j.contains("META") || j.contains("BPMList")) return "rpe";
        return "official";
    } catch (...) { return "pec"; }
}

static ChartData load_chart_auto(const std::string& path, int W, int H) {
    // Zip reference: "archive.zip:file.json"
    if (chart::is_zip_path(path)) {
        auto [zip_path, file_in_zip] = chart::split_zip_path(path);
        auto data = chart::extract_zip_file(zip_path, file_in_zip);
        if (data.empty()) throw std::runtime_error("Failed to extract from zip: " + path);
        std::string ext = fs::path(file_in_zip).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".phbc") {
            std::string blob(data.begin(), data.end());
            std::istringstream in(blob, std::ios::in | std::ios::binary);
            return chart::read_phbc(in).to_chart_data();
        }
        std::string text(data.begin(), data.end());
        std::string fmt = detect_format_text(text);
        if (fmt == "rpe") return chart::parse_rpe(json::parse(text), W, H);
        if (fmt == "official") return chart::parse_official(json::parse(text), W, H);
        return chart::load_pec_text(text, W, H);
    }
    // Resolve chart directory entry
    if (auto resolved = chart::resolve_chart_entry(path))
        return load_chart_auto(resolved->chart_path, W, H);
    // Direct file
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".phbc") {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open: " + path);
        return chart::read_phbc(f).to_chart_data();
    }
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open: " + path);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string fmt = detect_format_text(text);
    if (fmt == "rpe") return chart::parse_rpe(json::parse(text), W, H);
    if (fmt == "official") return chart::parse_official(json::parse(text), W, H);
    return chart::parse_pec(path, W, H);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: dump_frame <chart_path> <t_seconds> [W] [H]\n");
        return 1;
    }

    // Suppress all log output — stdout is reserved for JSON
    phigros::core::Logger::get().min_level = phigros::core::LogLevel::Off;

    std::string chart_path = argv[1];
    double probe_t = std::atof(argv[2]);
    int W = (argc > 3) ? std::atoi(argv[3]) : 1280;
    int H = (argc > 4) ? std::atoi(argv[4]) : 720;

    // Load chart
    ChartData chart;
    try {
        chart = load_chart_auto(chart_path, W, H);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Load error: %s\n", e.what());
        return 1;
    }

    // Default config
    config::RenderConfig cfg;
    cfg.window_w = W;
    cfg.window_h = H;

    // Precompute t_enter
    if (!chart.is_compiled) {
        precompute_t_enter(chart.lines, chart.notes, W, H,
                           cfg.expand_factor, cfg.note_scale_x, cfg.note_scale_y);
    }
    chart.build_notes_by_enter_index();

    // Simulate autoplay from chart.offset to probe_t
    constexpr double SIM_DT  = 1.0 / 240.0;
    const double HOLD_TOL    = cfg.hold_tail_tol;

    std::vector<NoteState> st(chart.notes.size());
    for (size_t i = 0; i < st.size(); ++i) st[i].note = &chart.notes[i];
    Judge j;

    double prev_tc = chart.offset - SIM_DT;
    for (double tc = chart.offset; tc <= probe_t + SIM_DT * 0.5; tc += SIM_DT) {
        exact_autoplay_step(prev_tc, tc, chart.notes, st, chart.lines, j, W, H);
        // find next-note index (sorted by t_hit)
        int inx = 0;
        for (size_t k = 0; k < st.size(); ++k) {
            if (st[k].judged || chart.notes[k].t_hit < tc - 0.5) inx = (int)k + 1;
        }
        detect_misses(st, inx, tc, Judge::BAD, j);
        hold_maintenance(st, inx, tc, HOLD_TOL, j);
        hold_finalize(st, inx, tc, HOLD_TOL, Judge::BAD, j);
        prev_tc = tc;
    }

    // Build render frame at probe_t
    FrameSnapshot frame = build_frame(probe_t, chart, st, j, cfg);

    // ── JSON output ──────────────────────────────────────────────────────────

    printf("{\n");
    printf("  \"t\": %.6f,\n", probe_t);
    printf("  \"chart_offset\": %.6f,\n", chart.offset);
    printf("  \"W\": %d, \"H\": %d,\n", W, H);

    // Lines
    printf("  \"lines\": [\n");
    for (size_t li = 0; li < frame.lines.size(); ++li) {
        const auto& ls = frame.lines[li];
        printf("    {\"lid\":%d, \"x\":%.2f, \"y\":%.2f, \"rot_deg\":%.3f,"
               " \"alpha\":%.3f, \"scroll\":%.2f}%s\n",
            ls.lid, ls.x, ls.y, ls.rot * 180.0 / 3.14159265358979,
            ls.alpha01, ls.scroll,
            li + 1 < frame.lines.size() ? "," : "");
    }
    printf("  ],\n");

    // Notes visible in this frame
    printf("  \"frame_notes\": [\n");
    for (size_t ni = 0; ni < frame.notes.size(); ++ni) {
        const auto& ns = frame.notes[ni];
        printf("    {\"nid\":%d, \"kind\":%d,"
               " \"wx\":%.1f, \"wy\":%.1f, \"wx_tail\":%.1f, \"wy_tail\":%.1f,"
               " \"body_px\":%.1f,"
               " \"judged\":%s, \"miss\":%s, \"holding\":%s,"
               " \"hold_hit_failed\":%s, \"alpha\":%.3f}%s\n",
            ns.nid, ns.kind,
            ns.wx, ns.wy, ns.wx_tail, ns.wy_tail,
            std::hypot(ns.wx_tail - ns.wx, ns.wy_tail - ns.wy),
            ns.judged   ? "true" : "false",
            ns.miss     ? "true" : "false",
            ns.holding  ? "true" : "false",
            ns.hold_hit_failed ? "true" : "false",
            ns.alpha,
            ni + 1 < frame.notes.size() ? "," : "");
    }
    printf("  ],\n");

    // All holds in ±12s window with full engine + kinematics state
    printf("  \"all_holds\": [\n");
    bool first = true;
    const size_t n_lines = chart.lines.size();
    for (size_t i = 0; i < chart.notes.size(); ++i) {
        const auto& note = chart.notes[i];
        if (note.kind != 3) continue;
        if (note.t_end < probe_t - 12.0 || note.t_hit > probe_t + 12.0) continue;

        const auto& ns   = st[i];
        double scroll_now = 0.0, lx = 0.0, ly = 0.0, lrot = 0.0;
        if (note.line_id >= 0 && note.line_id < (int)n_lines) {
            auto& ln = chart.lines[note.line_id];
            scroll_now = ln.scroll_fn
                ? ln.scroll_fn(probe_t)
                : ln.scroll_px.integral(probe_t);
            lx   = ln.pos_x(probe_t);
            ly   = ln.pos_y(probe_t);
            lrot = ln.rot(probe_t);
        }

        double dy_head = (note.scroll_hit  - scroll_now) * cfg.note_flow_speed_multiplier;
        double dy_tail = (note.scroll_end  - scroll_now) * cfg.note_flow_speed_multiplier
                         * std::max(0.0, note.speed_mul);
        double body_scroll_px = note.scroll_end - note.scroll_hit;

        // Is it in the frame?
        bool in_frame = false;
        for (auto& fn : frame.notes) if (fn.nid == note.nid) { in_frame = true; break; }

        // Culled by emit_note logic?
        bool cull_finalized = (note.kind == 3 && ns.hold_finalized && !ns.miss);
        bool cull_missed_scrolled = (note.kind == 3 && !ns.hit && scroll_now >= note.scroll_end);

        if (!first) printf(",\n");
        first = false;

        printf("    {"
               "\"nid\":%d, \"line_id\":%d, \"fake\":%s,\n"
               "     \"t_hit\":%.4f, \"t_end\":%.4f, \"dur\":%.4f,\n"
               "     \"scroll_hit\":%.2f, \"scroll_end\":%.2f, \"scroll_now\":%.2f,\n"
               "     \"body_scroll_px\":%.2f,\n"
               "     \"dy_head\":%.2f, \"dy_tail\":%.2f, \"rendered_body_px\":%.2f,\n"
               "     \"speed_mul\":%.3f, \"x_local_px\":%.1f,\n"
               "     \"lx\":%.2f, \"ly\":%.2f, \"lrot_deg\":%.3f,\n"
               "     \"hit\":%s, \"judged\":%s, \"miss\":%s,\n"
               "     \"holding\":%s, \"hold_finalized\":%s,\n"
               "     \"cull_finalized\":%s, \"cull_missed_scrolled\":%s,\n"
               "     \"in_frame\":%s}",
            note.nid, note.line_id, note.fake ? "true" : "false",
            note.t_hit, note.t_end, note.t_end - note.t_hit,
            note.scroll_hit, note.scroll_end, scroll_now,
            body_scroll_px,
            dy_head, dy_tail, dy_tail - dy_head,
            note.speed_mul, note.x_local_px,
            lx, ly, lrot * 180.0 / 3.14159265358979,
            ns.hit          ? "true" : "false",
            ns.judged       ? "true" : "false",
            ns.miss         ? "true" : "false",
            ns.holding      ? "true" : "false",
            ns.hold_finalized ? "true" : "false",
            cull_finalized  ? "true" : "false",
            cull_missed_scrolled ? "true" : "false",
            in_frame        ? "true" : "false"
        );
    }
    printf("\n  ]\n}\n");

    return 0;
}
