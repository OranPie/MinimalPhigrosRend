// Performance benchmark: measures engine step() throughput under different
// combinations of note count, line count, and active mods.
// Reports "steps/sec" (max-FPS equivalent) and ns/step for each scenario.
// All scenarios are expected to succeed; no performance-floor assertions.

#include "phic/core/engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

// ---------------------------------------------------------------------------
// Chart builder
// ---------------------------------------------------------------------------

// Build a ChartData with `line_count` lines, `notes_per_line` notes each.
// Notes on each line are spaced `note_interval_sec` apart.
static phic::ChartData make_chart(int line_count, int notes_per_line,
                                   double note_interval_sec = 0.1) {
    constexpr int kLaneCount = 8;  // matches the default ModConfig::lane_count
    phic::ChartData chart;
    chart.title = "bench";
    int next_id = 1;
    for (int l = 0; l < line_count; ++l) {
        chart.lines.push_back(phic::RuntimeLine{l});
        for (int n = 0; n < notes_per_line; ++n) {
            phic::RuntimeNote note;
            note.id       = next_id++;
            note.line_id  = l;
            note.lane     = n % kLaneCount;
            note.above    = true;
            note.fake     = false;
            note.t_hit    = (n + 1) * note_interval_sec;
            note.hold_end = note.t_hit;
            note.speed_mul = 1.0;
            note.alpha01  = 1.0;
            note.kind     = phic::NoteKind::Tap;
            chart.notes.push_back(note);
        }
    }
    std::sort(chart.notes.begin(), chart.notes.end(),
              [](const phic::RuntimeNote& a, const phic::RuntimeNote& b) {
                  return a.t_hit < b.t_hit;
              });
    return chart;
}

// ---------------------------------------------------------------------------
// Benchmark runner
// ---------------------------------------------------------------------------

struct BenchResult {
    const char* name;
    int note_count;
    int line_count;
    double steps_per_sec;
    double ns_per_step;
};

static BenchResult run_bench(const char* name,
                              int line_count,
                              int notes_per_line,
                              const phic::RenderConfig& cfg,
                              int step_count = 3000) {
    phic::Engine engine(cfg);
    engine.load_chart(make_chart(line_count, notes_per_line));

    // Warm-up: a few steps so instruction caches / branch predictors settle.
    for (int i = 0; i < 20; ++i) {
        engine.step(1.0 / 60.0, {});
    }
    engine.reset();

    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < step_count; ++i) {
        engine.step(1.0 / 60.0, {});
    }
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double elapsed_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    const double ns_per_step  = elapsed_ns / static_cast<double>(step_count);
    const double steps_per_sec = 1e9 / ns_per_step;

    return BenchResult{name, line_count * notes_per_line, line_count,
                       steps_per_sec, ns_per_step};
}

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

int main() {
    std::vector<BenchResult> results;

    // 1. Baseline: few notes, single line, no mods.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        results.push_back(run_bench("light", 1, 20, cfg));
    }

    // 2. Heavy notes: many notes on a single line, no mods.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        results.push_back(run_bench("heavy_notes", 1, 1000, cfg));
    }

    // 3. Heavy lines: many lines with moderate notes each, no mods.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        results.push_back(run_bench("heavy_lines", 50, 20, cfg));
    }

    // 4. Heavy mods: moderate note count with mirror + wave + stutter + quantize.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        cfg.mods.mirror                = true;
        cfg.mods.wave                  = true;
        cfg.mods.wave_amplitude_lane   = 1.0;
        cfg.mods.wave_period_sec       = 2.0;
        cfg.mods.stutter               = true;
        cfg.mods.stutter_repeat        = 3;
        cfg.mods.stutter_interval_sec  = 0.05;
        cfg.mods.quantize              = true;
        cfg.mods.quantize_step_sec     = 0.1;
        results.push_back(run_bench("heavy_mods", 1, 200, cfg));
    }

    // 5. Combined: many notes across many lines with mods.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        cfg.mods.mirror               = true;
        cfg.mods.wave                 = true;
        cfg.mods.wave_amplitude_lane  = 0.5;
        cfg.mods.wave_period_sec      = 1.0;
        cfg.mods.stutter              = true;
        cfg.mods.stutter_repeat       = 2;
        cfg.mods.stutter_interval_sec = 0.05;
        results.push_back(run_bench("combined", 20, 50, cfg));
    }

    // Print results table.
    std::printf("\n--- Performance Benchmark Results ---\n");
    std::printf("%-20s %8s %8s %14s %14s\n",
                "scenario", "notes", "lines", "steps/sec", "ns/step");
    std::printf("%-20s %8s %8s %14s %14s\n",
                "--------------------", "--------", "--------",
                "--------------", "--------------");
    for (const auto& r : results) {
        std::printf("%-20s %8d %8d %14.0f %14.1f\n",
                    r.name, r.note_count, r.line_count,
                    r.steps_per_sec, r.ns_per_step);
    }
    std::printf("\n");

    return 0;
}
