#pragma once

#include "phigros/chart/chart_loader.hpp"
#include "phigros/chart/compiled_chart.hpp"
#include "phigros/chart/phbc_io.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/simulateplay.hpp"
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
