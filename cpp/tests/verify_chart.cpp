// Integration test: dump per-frame line states and note positions to JSON
// for comparison with Python renderer output.
//
// Usage: verify_chart <chart_path> [width] [height] [fps] [duration]
//   Outputs JSON to stdout with line/note states at each frame.

#include "phigros/chart/official.hpp"
#include "phigros/chart/rpe.hpp"
#include "phigros/chart/pec.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/render/renderer.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/hud/hud.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <nlohmann/json.hpp>

using namespace phigros;
using json = nlohmann::json;

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

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: verify_chart <chart_path> [W] [H] [fps] [max_time]\n";
        return 1;
    }

    std::string path = argv[1];
    int W = argc >= 3 ? std::atoi(argv[2]) : 1280;
    int H = argc >= 4 ? std::atoi(argv[3]) : 720;
    double fps = argc >= 5 ? std::atof(argv[4]) : 60.0;
    double max_time = argc >= 6 ? std::atof(argv[5]) : -1.0;

    std::string fmt = detect_format(path);
    if (fmt.empty()) { std::cerr << "Cannot open: " << path << "\n"; return 1; }

    // Load chart
    ChartData chart;
    try {
        if (fmt == "official") {
            std::ifstream f(path);
            auto j = json::parse(f);
            chart = chart::load_official(j, W, H);
        } else if (fmt == "rpe") {
            std::ifstream f(path);
            auto j = json::parse(f);
            chart = chart::load_rpe(j, W, H);
        } else if (fmt == "pec") {
            chart = chart::load_pec(path, W, H);
        } else {
            std::ifstream f(path);
            std::string text((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
            chart = chart::load_pec_text(text, W, H);
        }
    } catch (const std::exception& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        return 1;
    }

    // Find chart duration
    double chart_end = 0.0;
    int playable = 0;
    for (auto& n : chart.notes) {
        chart_end = std::max(chart_end, n.t_end);
        if (!n.fake) ++playable;
    }
    if (max_time > 0.0) chart_end = std::min(chart_end, max_time);

    // Init states + judge
    std::vector<NoteState> states(chart.notes.size());
    for (size_t i = 0; i < chart.notes.size(); ++i)
        states[i].note = &chart.notes[i];

    engine::Judge judge;

    // Config
    config::RenderConfig cfg;
    cfg.window_w = W;
    cfg.window_h = H;

    // Autoplay simulation
    double dt = 1.0 / fps;
    int frame_count = static_cast<int>(std::ceil((chart_end + 0.5) / dt));

    auto t_start = std::chrono::high_resolution_clock::now();

    json output;
    output["format"] = fmt;
    output["resolution"] = {W, H};
    output["fps"] = fps;
    output["offset"] = chart.offset;
    output["lines_count"] = chart.lines.size();
    output["notes_count"] = chart.notes.size();
    output["playable"] = playable;

    json frames_json = json::array();

    for (int fi = 0; fi < frame_count; ++fi) {
        double t = fi * dt + chart.offset;

        // Autoplay: judge notes at their exact hit time
        for (size_t i = 0; i < chart.notes.size(); ++i) {
            auto& note = chart.notes[i];
            auto& ns = states[i];
            if (ns.judged) continue;
            if (note.fake) { ns.judged = true; continue; }
            // Perfect autoplay
            if (t >= note.t_hit) {
                judge.bump();
                ns.judged = true;
                ns.hit = true;
                judge.acc_sum += 1.0;
                ++judge.judged_cnt;
            }
        }

        // Build frame snapshot
        auto frame = render::build_frame(t, chart, states, judge, cfg);

        // Serialize only every Nth frame to keep output size manageable
        // Default: every frame for short charts, every 10th for long
        int sample_rate = (chart_end > 30.0) ? 10 : 1;
        if (fi % sample_rate != 0 && fi != frame_count - 1) continue;

        json fj;
        fj["t"] = t;
        fj["frame"] = fi;

        // Line states
        json lines_arr = json::array();
        for (auto& ls : frame.lines) {
            lines_arr.push_back({
                {"lid", ls.lid},
                {"x", std::round(ls.x * 1000.0) / 1000.0},
                {"y", std::round(ls.y * 1000.0) / 1000.0},
                {"rot", std::round(ls.rot * 100000.0) / 100000.0},
                {"alpha", std::round(ls.alpha01 * 10000.0) / 10000.0},
                {"scroll", std::round(ls.scroll * 100.0) / 100.0},
            });
        }
        fj["lines"] = lines_arr;

        // Note positions (visible notes only)
        json notes_arr = json::array();
        for (auto& ns : frame.notes) {
            notes_arr.push_back({
                {"nid", ns.nid},
                {"kind", ns.kind},
                {"wx", std::round(ns.wx * 100.0) / 100.0},
                {"wy", std::round(ns.wy * 100.0) / 100.0},
            });
        }
        fj["notes"] = notes_arr;

        // HUD
        fj["score"] = frame.hud.score;
        fj["combo"] = frame.hud.combo;
        fj["accuracy"] = std::round(frame.hud.accuracy * 10000.0) / 10000.0;
        fj["progress"] = std::round(frame.hud.progress * 10000.0) / 10000.0;

        frames_json.push_back(fj);
    }

    auto t_end_clock = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t_end_clock - t_start).count();

    output["frames"] = frames_json;
    output["frame_count"] = frame_count;
    output["sampled_frames"] = frames_json.size();
    output["elapsed_ms"] = ms;

    // Final score
    auto sr = engine::compute_score(judge.acc_sum, judge.max_combo, playable);
    output["final_score"] = sr.score;
    output["final_accuracy"] = sr.acc_ratio;
    output["final_combo_ratio"] = sr.combo_ratio;
    output["max_combo"] = judge.max_combo;

    std::cout << output.dump(2) << std::endl;
    return 0;
}
