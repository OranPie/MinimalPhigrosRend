#pragma once

#include "phigros/chart/chart_loader.hpp"
#include "phigros/chart/compiled_chart.hpp"
#include "phigros/chart/phbc_io.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/engine/hold_logic.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/simulateplay.hpp"
#include "phigros/engine/visibility.hpp"
#include "phigros/render/renderer.hpp"
#include <optional>
#include <string>
#include <vector>

namespace phigros::api {

struct PreparedChart {
    ChartData chart;
    config::RenderConfig config;
    int scoring_notes = 0;
    double simulation_end = 0.0;
};

struct AutoplayResult {
    engine::ScoreResult score;
    int judged_count = 0;
    int playable_count = 0;
    int max_combo = 0;
    std::vector<engine::SimHitEvent> hit_events;
    std::vector<render::FrameSnapshot> frames;
};

// Stateful frame evaluator: holds simulation state between build_frame() calls
// so that advancing time is O(new_steps) not O(total_steps_from_zero).
// Non-copyable; moving is allowed (C++ side) but Python bindings keep shared_ptr.
class FrameEvaluator {
public:
    explicit FrameEvaluator(const PreparedChart& prepared,
                            const std::string& mode = "aggressive",
                            int max_pointers = 2);

    // Build a single frame at time t.
    // Advances simulation forward if t > current sim head, or resets and replays
    // if t < current sim head (seeking backwards is permitted but costly).
    render::FrameSnapshot build_frame(double t,
                                      std::optional<config::RenderConfig> cfg_override = std::nullopt);

    // Batch frame build over an arbitrary set of times (need not be sorted).
    // More efficient than calling build_frame() in a loop because internal state
    // is shared across the sorted traversal.
    std::vector<render::FrameSnapshot> build_frames(
        const std::vector<double>& times,
        std::optional<config::RenderConfig> cfg_override = std::nullopt);

    // Reset simulation to chart start so the next build_frame() starts clean.
    void reset();

    // Read-only access to current accumulated judgment counters.
    const engine::Judge& judge() const { return judge_; }

    // Current simulation head (seconds).
    double sim_t() const { return sim_t_; }

private:
    const PreparedChart* prepared_;
    std::string mode_;
    int max_pointers_;

    std::vector<NoteState>      states_;
    engine::Judge               judge_;
    engine::SimulatePlayer      autoplay_;
    double                      sim_t_;

    static constexpr double SIM_DT = 1.0 / 240.0;

    void step(double t, const config::RenderConfig& cfg,
              std::vector<engine::SimHitEvent>* events = nullptr);
    void advance_to(double t, const config::RenderConfig& cfg);
    void reset_impl();
};

// ── Free functions ───────────────────────────────────────────────────────────

PreparedChart load_prepared_chart(const std::string& path,
                                  const config::RenderConfig& cfg,
                                  const std::string& password = "");

render::FrameSnapshot build_autoplay_frame(const PreparedChart& prepared,
                                           double t,
                                           std::optional<config::RenderConfig> cfg_override = std::nullopt);

std::vector<render::FrameSnapshot> build_autoplay_frames(
    const PreparedChart& prepared,
    const std::vector<double>& times,
    std::optional<config::RenderConfig> cfg_override = std::nullopt);

AutoplayResult simulate_autoplay(const PreparedChart& prepared,
                                 double fps = 240.0,
                                 const std::string& mode = "aggressive",
                                 int max_pointers = 2,
                                 std::optional<double> duration = std::nullopt);

chart::CompiledChartData compile_prepared_chart(const PreparedChart& prepared,
                                                float sample_rate = 240.0f);

chart::CompiledChartData read_phbc_file(const std::string& path,
                                        const std::string& password = "");

void write_phbc_file(const chart::CompiledChartData& compiled,
                     const std::string& path,
                     const chart::PhbcWriteOptions& opts);

} // namespace phigros::api
