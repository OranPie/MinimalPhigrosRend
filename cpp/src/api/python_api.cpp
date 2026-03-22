#include "phigros/api/python_api.hpp"
#include "phigros/chart/compiler.hpp"
#include "phigros/chart/parser.hpp"
#include "phigros/engine/hold_logic.hpp"
#include "phigros/engine/visibility.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace phigros::api {

namespace {

// ── Shared helpers ────────────────────────────────────────────────────────────

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
        if (bytes.empty())
            throw std::runtime_error("Failed to extract chart from zip: " + path);
        std::string text(bytes.begin(), bytes.end());
        try {
            auto j = json::parse(text);
            if (j.contains("META") || j.contains("BPMList"))
                return chart::parse_rpe(j, cfg.window_w, cfg.window_h, cfg.rpe_easing_shift);
            return chart::parse_official(j, cfg.window_w, cfg.window_h);
        } catch (const json::exception& e) {
            throw std::runtime_error("JSON parse error in zip entry '" + path + "': " +
                                     std::string(e.what()));
        }
    }

    if (fs::is_directory(path)) {
        auto entries = chart::load_folder_chart(path);
        if (entries.empty())
            throw std::runtime_error("No recognised chart files found in folder: " + path);
        const chart::ChartEntry* chosen = &entries.front();
        for (const auto& entry : entries) {
            if (entry.difficulty == "IN") { chosen = &entry; break; }
        }
        return load_chart_from_path(chosen->chart_path, cfg, password);
    }

    if (!fs::exists(path))
        throw std::runtime_error("Chart file not found: " + path);

    const std::string fmt = detect_format(path);
    if (fmt == "rpe" || fmt == "official") {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("Cannot open chart file: " + path);
        try {
            auto j = json::parse(f);
            if (fmt == "rpe")
                return chart::parse_rpe(j, cfg.window_w, cfg.window_h, cfg.rpe_easing_shift);
            return chart::parse_official(j, cfg.window_w, cfg.window_h);
        } catch (const json::exception& e) {
            throw std::runtime_error("JSON parse error in '" + path + "': " +
                                     std::string(e.what()));
        }
    }

    return chart::parse_pec(path, cfg.window_w, cfg.window_h);
}

engine::SimMode parse_sim_mode(const std::string& mode) {
    if (mode == "conservative") return engine::SimMode::Conservative;
    if (mode == "extreme")      return engine::SimMode::Extreme;
    if (mode == "aggressive")   return engine::SimMode::Aggressive;
    throw std::invalid_argument(
        "Unknown simulation mode '" + mode + "'. "
        "Valid values: \"conservative\", \"aggressive\", \"extreme\".");
}

// Finds the first non-judged note whose t_hit >= t - 0.5s.
// Used to bound the detect_misses / hold range scan.
int find_window_start(const std::vector<NoteState>& states, double t) {
    int lo = 0, hi = static_cast<int>(states.size());
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (states[mid].judged || states[mid].note->t_hit < t - 0.5) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

void step_engine(const PreparedChart& prepared,
                 std::vector<NoteState>& states,
                 engine::Judge& judge,
                 engine::SimulatePlayer& autoplay,
                 double t,
                 const config::RenderConfig& cfg,
                 std::vector<engine::SimHitEvent>* hit_events = nullptr) {
    autoplay.step(t,
                  prepared.chart.notes,
                  states,
                  prepared.chart.lines,
                  judge,
                  cfg.window_w,
                  cfg.window_h,
                  nullptr,
                  hit_events);
    const int idx = find_window_start(states, t);
    engine::detect_misses(states, idx, t, engine::Judge::BAD, judge);
    engine::hold_maintenance(states, idx, t, cfg.hold_tail_tol, judge);
    engine::hold_finalize(states, idx, t, cfg.hold_tail_tol, engine::Judge::BAD, judge);
}

// Initialises a flat NoteState vector with note pointers from prepared.chart.
std::vector<NoteState> make_states(const PreparedChart& prepared) {
    std::vector<NoteState> states(prepared.chart.notes.size());
    for (size_t i = 0; i < states.size(); ++i)
        states[i].note = &prepared.chart.notes[i];
    return states;
}

} // namespace

// ── FrameEvaluator ────────────────────────────────────────────────────────────

FrameEvaluator::FrameEvaluator(const PreparedChart& prepared,
                               const std::string& mode,
                               int max_pointers)
    : prepared_(&prepared)
    , mode_(mode)
    , max_pointers_(std::max(1, max_pointers))
    , states_(make_states(prepared))
    , autoplay_(parse_sim_mode(mode), max_pointers_)
    , sim_t_(prepared.chart.offset)
{}

void FrameEvaluator::reset_impl() {
    states_ = make_states(*prepared_);
    judge_  = engine::Judge{};
    autoplay_ = engine::SimulatePlayer(parse_sim_mode(mode_), max_pointers_);
    sim_t_  = prepared_->chart.offset;
}

void FrameEvaluator::reset() {
    reset_impl();
}

void FrameEvaluator::step(double t,
                          const config::RenderConfig& cfg,
                          std::vector<engine::SimHitEvent>* events) {
    step_engine(*prepared_, states_, judge_, autoplay_, t, cfg, events);
}

void FrameEvaluator::advance_to(double t, const config::RenderConfig& cfg) {
    if (t < sim_t_ - 1e-9) {
        // Backwards seek: reset then replay forward. Costly but correct.
        reset_impl();
    }
    while (sim_t_ + 1e-9 < t) {
        step(sim_t_, cfg);
        sim_t_ += SIM_DT;
    }
    step(t, cfg);
    sim_t_ = std::max(sim_t_, t + SIM_DT);
}

render::FrameSnapshot FrameEvaluator::build_frame(
    double t, std::optional<config::RenderConfig> cfg_override) {
    const config::RenderConfig& cfg = cfg_override ? *cfg_override : prepared_->config;
    advance_to(t, cfg);
    return render::build_frame(t, prepared_->chart, states_, judge_, cfg);
}

std::vector<render::FrameSnapshot> FrameEvaluator::build_frames(
    const std::vector<double>& times,
    std::optional<config::RenderConfig> cfg_override) {
    if (times.empty()) return {};
    const config::RenderConfig& cfg = cfg_override ? *cfg_override : prepared_->config;

    // Sort by time to maximise cache reuse, then write results back in original order.
    std::vector<std::pair<size_t, double>> order;
    order.reserve(times.size());
    for (size_t i = 0; i < times.size(); ++i) order.push_back({i, times[i]});
    std::sort(order.begin(), order.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    std::vector<render::FrameSnapshot> out(times.size());
    for (const auto& [idx, t] : order) {
        advance_to(t, cfg);
        out[idx] = render::build_frame(t, prepared_->chart, states_, judge_, cfg);
    }
    return out;
}

// ── Free functions ────────────────────────────────────────────────────────────

PreparedChart load_prepared_chart(const std::string& path,
                                  const config::RenderConfig& cfg,
                                  const std::string& password) {
    PreparedChart prepared;
    prepared.config = cfg;
    prepared.chart  = load_chart_from_path(path, cfg, password);
    prepared.chart.finalize();
    if (!prepared.chart.is_compiled) {
        engine::precompute_t_enter(prepared.chart.lines, prepared.chart.notes,
                                   cfg.window_w, cfg.window_h,
                                   cfg.expand_factor, cfg.note_scale_x, cfg.note_scale_y);
        prepared.chart.build_early_notes_index();
    }
    prepared.chart.build_notes_by_enter_index();
    prepared.scoring_notes  = prepared.chart.playable_count;
    prepared.simulation_end = prepared.chart.chart_end_t;
    return prepared;
}

std::vector<render::FrameSnapshot> build_autoplay_frames(
    const PreparedChart& prepared,
    const std::vector<double>& times,
    std::optional<config::RenderConfig> cfg_override) {
    FrameEvaluator ev(prepared, "aggressive", 2);
    return ev.build_frames(times, std::move(cfg_override));
}

render::FrameSnapshot build_autoplay_frame(const PreparedChart& prepared,
                                           double t,
                                           std::optional<config::RenderConfig> cfg_override) {
    FrameEvaluator ev(prepared, "aggressive", 2);
    return ev.build_frame(t, std::move(cfg_override));
}

AutoplayResult simulate_autoplay(const PreparedChart& prepared,
                                 double fps,
                                 const std::string& mode,
                                 int max_pointers,
                                 std::optional<double> duration) {
    if (fps < 1.0)
        throw std::invalid_argument("fps must be >= 1.0 (got " + std::to_string(fps) + ")");
    if (max_pointers < 1 || max_pointers > 10)
        throw std::invalid_argument("max_pointers must be 1..10");

    const double sim_dt = 1.0 / fps;
    const double end_t  = duration
        ? std::min(*duration, prepared.simulation_end)
        : prepared.simulation_end;

    auto states   = make_states(prepared);
    engine::Judge judge;
    engine::SimulatePlayer autoplay(parse_sim_mode(mode), max_pointers);

    AutoplayResult result;
    result.playable_count = prepared.scoring_notes;

    for (double t = prepared.chart.offset; t <= end_t + 1e-9; t += sim_dt) {
        step_engine(prepared, states, judge, autoplay, t, prepared.config, &result.hit_events);
    }

    result.score       = engine::compute_score(judge.acc_sum, judge.max_combo, prepared.scoring_notes);
    result.judged_count = judge.judged_cnt;
    result.max_combo   = judge.max_combo;
    return result;
}

chart::CompiledChartData compile_prepared_chart(const PreparedChart& prepared,
                                                float sample_rate) {
    if (sample_rate < 1.0f || sample_rate > 10000.0f)
        throw std::invalid_argument("sample_rate must be in [1, 10000]");
    return chart::compile_chart(prepared.chart, sample_rate);
}

chart::CompiledChartData read_phbc_file(const std::string& path,
                                        const std::string& password) {
    if (!fs::exists(path))
        throw std::runtime_error("PHBC file not found: " + path);
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
