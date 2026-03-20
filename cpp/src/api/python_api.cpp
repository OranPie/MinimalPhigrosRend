#include "phigros/api/python_api.hpp"
#include "phigros/chart/compiler.hpp"
#include "phigros/chart/parser.hpp"
#include "phigros/engine/hold_logic.hpp"
#include "phigros/engine/visibility.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace phigros::api {

namespace {

std::string detect_format(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";

    std::string first;
    std::getline(f, first);
    if (!first.empty() && (first[0] == 'b' || first[0] == 'c' || first[0] == 'n' ||
        first[0] == '#' || (first[0] >= '0' && first[0] <= '9'))) {
        return "pec";
    }

    f.clear();
    f.seekg(0);
    try {
        json j = json::parse(f);
        if (j.contains("META") || j.contains("BPMList")) return "rpe";
        return "official";
    } catch (...) {
        return "pec";
    }
}

ChartData load_chart_from_path(const std::string& path,
                               const config::RenderConfig& cfg,
                               const std::string& password) {
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".phbc") {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open .phbc file: " + path);
        return chart::read_phbc(f, password).to_chart_data();
    }

    if (chart::is_zip_path(path)) {
        auto parts = chart::split_zip_path(path);
        auto bytes = chart::extract_zip_file(parts.first, parts.second);
        if (bytes.empty()) throw std::runtime_error("Failed to extract chart from zip: " + path);
        std::string text(bytes.begin(), bytes.end());
        auto j = json::parse(text);
        if (j.contains("META") || j.contains("BPMList")) {
            return chart::parse_rpe(j, cfg.window_w, cfg.window_h, cfg.rpe_easing_shift);
        }
        return chart::parse_official(j, cfg.window_w, cfg.window_h);
    }

    if (fs::is_directory(path)) {
        auto entries = chart::load_folder_chart(path);
        if (entries.empty()) throw std::runtime_error("No charts found in folder: " + path);
        const chart::ChartEntry* chosen = &entries.front();
        for (const auto& entry : entries) {
            if (entry.difficulty == "IN") {
                chosen = &entry;
                break;
            }
        }
        return load_chart_from_path(chosen->chart_path, cfg, password);
    }

    const std::string fmt = detect_format(path);
    if (fmt == "rpe" || fmt == "official") {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("Cannot open chart file: " + path);
        auto j = json::parse(f);
        if (fmt == "rpe") {
            return chart::parse_rpe(j, cfg.window_w, cfg.window_h, cfg.rpe_easing_shift);
        }
        return chart::parse_official(j, cfg.window_w, cfg.window_h);
    }

    return chart::parse_pec(path, cfg.window_w, cfg.window_h);
}

engine::SimMode parse_sim_mode(const std::string& mode) {
    if (mode == "conservative") return engine::SimMode::Conservative;
    if (mode == "extreme") return engine::SimMode::Extreme;
    return engine::SimMode::Aggressive;
}

struct SimulationState {
    std::vector<NoteState> states;
    engine::Judge judge;
    engine::SimulatePlayer autoplay;

    SimulationState(const PreparedChart& prepared,
                    const std::string& mode,
                    int max_pointers)
        : states(prepared.chart.notes.size()),
          autoplay(parse_sim_mode(mode), max_pointers) {
        for (size_t i = 0; i < states.size(); ++i) states[i].note = &prepared.chart.notes[i];
    }
};

int find_next_index(const std::vector<NoteState>& states, double t) {
    int lo = 0;
    int hi = static_cast<int>(states.size());
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (states[mid].judged || states[mid].note->t_hit < t - 0.5) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

void step_engine(const PreparedChart& prepared,
                 SimulationState& sim,
                 double t,
                 const config::RenderConfig& runtime_cfg,
                 std::vector<engine::SimHitEvent>* hit_events = nullptr) {
    sim.autoplay.step(t,
                      prepared.chart.notes,
                      sim.states,
                      prepared.chart.lines,
                      sim.judge,
                      runtime_cfg.window_w,
                      runtime_cfg.window_h,
                      nullptr,
                      hit_events);
    const int idx = find_next_index(sim.states, t);
    engine::detect_misses(sim.states, idx, t, engine::Judge::BAD, sim.judge);
    engine::hold_maintenance(sim.states, idx, t, runtime_cfg.hold_tail_tol, sim.judge);
    engine::hold_finalize(sim.states, idx, t, runtime_cfg.hold_tail_tol, engine::Judge::BAD, sim.judge);
}

} // namespace

PreparedChart load_prepared_chart(const std::string& path,
                                  const config::RenderConfig& cfg,
                                  const std::string& password) {
    PreparedChart prepared;
    prepared.config = cfg;
    prepared.chart = load_chart_from_path(path, cfg, password);
    prepared.chart.finalize();
    if (!prepared.chart.is_compiled) {
        engine::precompute_t_enter(prepared.chart.lines, prepared.chart.notes,
                                   cfg.window_w, cfg.window_h,
                                   cfg.expand_factor, cfg.note_scale_x, cfg.note_scale_y);
    }
    prepared.chart.build_notes_by_enter_index();
    prepared.scoring_notes = prepared.chart.playable_count;
    prepared.simulation_end = prepared.chart.chart_end_t;
    return prepared;
}

std::vector<render::FrameSnapshot> build_autoplay_frames(
    const PreparedChart& prepared,
    const std::vector<double>& times,
    std::optional<config::RenderConfig> cfg_override) {
    if (times.empty()) return {};

    const config::RenderConfig& frame_cfg = cfg_override ? *cfg_override : prepared.config;
    std::vector<std::pair<size_t, double>> sorted_times;
    sorted_times.reserve(times.size());
    for (size_t i = 0; i < times.size(); ++i) sorted_times.push_back({i, times[i]});
    std::sort(sorted_times.begin(), sorted_times.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    SimulationState sim(prepared, "aggressive", 2);
    std::vector<render::FrameSnapshot> frames(times.size());

    constexpr double SIM_DT = 1.0 / 240.0;
    double sim_t = prepared.chart.offset;

    for (const auto& item : sorted_times) {
        const size_t out_idx = item.first;
        const double target_t = item.second;
        if (target_t < prepared.chart.offset) {
            frames[out_idx] = render::build_frame(target_t, prepared.chart, sim.states, sim.judge, frame_cfg);
            continue;
        }
        while (sim_t + 1e-9 < target_t) {
            step_engine(prepared, sim, sim_t, frame_cfg);
            sim_t += SIM_DT;
        }
        step_engine(prepared, sim, target_t, frame_cfg);
        sim_t = std::max(sim_t, target_t + SIM_DT);
        frames[out_idx] = render::build_frame(target_t, prepared.chart, sim.states, sim.judge, frame_cfg);
    }

    return frames;
}

render::FrameSnapshot build_autoplay_frame(const PreparedChart& prepared,
                                           double t,
                                           std::optional<config::RenderConfig> cfg_override) {
    auto frames = build_autoplay_frames(prepared, std::vector<double>{t}, std::move(cfg_override));
    if (frames.empty()) throw std::runtime_error("Failed to build frame");
    return std::move(frames.front());
}

AutoplayResult simulate_autoplay(const PreparedChart& prepared,
                                 double fps,
                                 const std::string& mode,
                                 int max_pointers,
                                 std::optional<double> duration) {
    const double sim_dt = 1.0 / std::max(1.0, fps);
    const double end_t = duration ? std::min(*duration, prepared.simulation_end) : prepared.simulation_end;

    SimulationState sim(prepared, mode, max_pointers);
    AutoplayResult result;
    result.playable_count = prepared.scoring_notes;

    for (double t = prepared.chart.offset; t <= end_t + 1e-9; t += sim_dt) {
        step_engine(prepared, sim, t, prepared.config, &result.hit_events);
    }

    result.score = engine::compute_score(sim.judge.acc_sum, sim.judge.max_combo, prepared.scoring_notes);
    result.judged_count = sim.judge.judged_cnt;
    result.max_combo = sim.judge.max_combo;
    return result;
}

chart::CompiledChartData compile_prepared_chart(const PreparedChart& prepared,
                                                float sample_rate) {
    return chart::compile_chart(prepared.chart, sample_rate);
}

chart::CompiledChartData read_phbc_file(const std::string& path,
                                        const std::string& password) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open .phbc file: " + path);
    return chart::read_phbc(f, password);
}

void write_phbc_file(const chart::CompiledChartData& compiled,
                     const std::string& path,
                     const chart::PhbcWriteOptions& opts) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot write .phbc file: " + path);
    if (opts.compress || opts.encrypt) chart::write_phbc(compiled, f, opts);
    else chart::write_phbc(compiled, f);
}

} // namespace phigros::api
