// Performance benchmark: measures engine step() throughput under different
// combinations of note count, line count, active mods, note kinds, and
// apply_mods() preprocessing cost.
// Reports "steps/sec" (max-FPS equivalent) and ns/step for each scenario.
// All scenarios are expected to succeed; no performance-floor assertions.

#include "phic/core/engine.hpp"
#include "phic/core/mods.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <vector>

// ---------------------------------------------------------------------------
// Chart builders
// ---------------------------------------------------------------------------

// Build a ChartData with `line_count` lines, `notes_per_line` notes each.
// Notes on each line are spaced `note_interval_sec` apart.
// `kind` sets all notes to that kind; hold notes get hold_end += 0.05.
static phic::ChartData make_chart(int line_count, int notes_per_line,
                                   double note_interval_sec = 0.1,
                                   phic::NoteKind kind = phic::NoteKind::Tap) {
    constexpr int kLaneCount = 8;
    phic::ChartData chart;
    chart.title = "bench";
    int next_id = 1;
    for (int l = 0; l < line_count; ++l) {
        chart.lines.push_back(phic::RuntimeLine{l});
        for (int n = 0; n < notes_per_line; ++n) {
            phic::RuntimeNote note;
            note.id        = next_id++;
            note.line_id   = l;
            note.lane      = n % kLaneCount;
            note.above     = true;
            note.fake      = false;
            note.t_hit     = (n + 1) * note_interval_sec;
            note.hold_end  = note.t_hit + (kind == phic::NoteKind::Hold ? 0.05 : 0.0);
            note.speed_mul = 1.0;
            note.alpha01   = 1.0;
            note.kind      = kind;
            chart.notes.push_back(note);
        }
    }
    std::sort(chart.notes.begin(), chart.notes.end(),
              [](const phic::RuntimeNote& a, const phic::RuntimeNote& b) {
                  return a.t_hit < b.t_hit;
              });
    return chart;
}

// Build a chart with notes cycling through all four NoteKinds.
static phic::ChartData make_mixed_chart(int line_count, int notes_per_line,
                                         double note_interval_sec = 0.1) {
    constexpr int kLaneCount = 8;
    const phic::NoteKind kKinds[] = {
        phic::NoteKind::Tap, phic::NoteKind::Drag,
        phic::NoteKind::Hold, phic::NoteKind::Flick};
    phic::ChartData chart;
    chart.title = "bench_mixed";
    int next_id = 1;
    for (int l = 0; l < line_count; ++l) {
        chart.lines.push_back(phic::RuntimeLine{l});
        for (int n = 0; n < notes_per_line; ++n) {
            phic::NoteKind k = kKinds[next_id % 4];
            phic::RuntimeNote note;
            note.id        = next_id++;
            note.line_id   = l;
            note.lane      = n % kLaneCount;
            note.above     = (n % 2 == 0);
            note.fake      = false;
            note.t_hit     = (n + 1) * note_interval_sec;
            note.hold_end  = note.t_hit + (k == phic::NoteKind::Hold ? 0.05 : 0.0);
            note.speed_mul = 1.0;
            note.alpha01   = 1.0;
            note.kind      = k;
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
// Benchmark runners
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
                              int step_count = 3000,
                              phic::NoteKind kind = phic::NoteKind::Tap) {
    phic::Engine engine(cfg);
    engine.load_chart(make_chart(line_count, notes_per_line, 0.1, kind));

    for (int i = 0; i < 20; ++i) engine.step(1.0 / 60.0, {});
    engine.reset();

    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < step_count; ++i) engine.step(1.0 / 60.0, {});
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double elapsed_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    const double ns_per_step   = elapsed_ns / static_cast<double>(step_count);
    const double steps_per_sec = 1e9 / ns_per_step;
    return BenchResult{name, line_count * notes_per_line, line_count,
                       steps_per_sec, ns_per_step};
}

static BenchResult run_bench_mixed(const char* name,
                                    int line_count,
                                    int notes_per_line,
                                    const phic::RenderConfig& cfg,
                                    int step_count = 3000) {
    phic::Engine engine(cfg);
    engine.load_chart(make_mixed_chart(line_count, notes_per_line));

    for (int i = 0; i < 20; ++i) engine.step(1.0 / 60.0, {});
    engine.reset();

    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < step_count; ++i) engine.step(1.0 / 60.0, {});
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double elapsed_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    const double ns_per_step   = elapsed_ns / static_cast<double>(step_count);
    const double steps_per_sec = 1e9 / ns_per_step;
    return BenchResult{name, line_count * notes_per_line, line_count,
                       steps_per_sec, ns_per_step};
}

// Benchmark engine.step() with random input events (non-autoplay).
static BenchResult run_bench_input(const char* name,
                                    int line_count,
                                    int notes_per_line,
                                    int step_count = 3000) {
    phic::RenderConfig cfg;
    cfg.autoplay = false;

    // Pre-build input event list: one PointerDown per step at a cycling lane.
    std::vector<std::vector<phic::InputEvent>> event_per_step(step_count);
    for (int i = 0; i < step_count; ++i) {
        phic::InputEvent ev;
        ev.type       = phic::InputEvent::Type::PointerDown;
        ev.lane       = i % 8;
        ev.event_time = static_cast<double>(i) / 60.0;
        event_per_step[i].push_back(ev);
    }

    phic::Engine engine(cfg);
    engine.load_chart(make_chart(line_count, notes_per_line));

    for (int i = 0; i < 20; ++i) engine.step(1.0 / 60.0, {});
    engine.reset();

    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < step_count; ++i) engine.step(1.0 / 60.0, event_per_step[i]);
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double elapsed_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    const double ns_per_step   = elapsed_ns / static_cast<double>(step_count);
    const double steps_per_sec = 1e9 / ns_per_step;
    return BenchResult{name, line_count * notes_per_line, line_count,
                       steps_per_sec, ns_per_step};
}

// Benchmark engine.seek() interleaved with step().
static BenchResult run_bench_seek(const char* name,
                                   int line_count,
                                   int notes_per_line,
                                   int step_count = 3000) {
    phic::RenderConfig cfg;
    cfg.autoplay = true;
    phic::Engine engine(cfg);
    engine.load_chart(make_chart(line_count, notes_per_line));

    for (int i = 0; i < 20; ++i) engine.step(1.0 / 60.0, {});
    engine.reset();

    // Alternate seek positions to stress the seek/reset path.
    const double kDuration = notes_per_line * 0.1;
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < step_count; ++i) {
        engine.seek(static_cast<double>(i % 20) * kDuration / 20.0);
        engine.step(1.0 / 60.0, {});
    }
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double elapsed_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    const double ns_per_step   = elapsed_ns / static_cast<double>(step_count);
    const double steps_per_sec = 1e9 / ns_per_step;
    return BenchResult{name, line_count * notes_per_line, line_count,
                       steps_per_sec, ns_per_step};
}

// Benchmark apply_mods() preprocessing cost (reports ns/call instead of ns/step).
struct ApplyModsResult {
    const char* name;
    int note_count;
    double ns_per_call;
    double calls_per_sec;
};

static ApplyModsResult run_bench_apply_mods(const char* name,
                                             int line_count,
                                             int notes_per_line,
                                             const phic::ModConfig& mods,
                                             int call_count = 500) {
    // Warm-up.
    for (int i = 0; i < 5; ++i) {
        phic::ChartData chart = make_chart(line_count, notes_per_line);
        phic::apply_mods(chart, mods);
    }

    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < call_count; ++i) {
        phic::ChartData chart = make_chart(line_count, notes_per_line);
        phic::apply_mods(chart, mods);
    }
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double elapsed_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    const double ns_per_call   = elapsed_ns / static_cast<double>(call_count);
    const double calls_per_sec = 1e9 / ns_per_call;
    return ApplyModsResult{name, line_count * notes_per_line,
                           ns_per_call, calls_per_sec};
}

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

int main() {
    std::vector<BenchResult> results;
    std::vector<ApplyModsResult> apply_results;

    // --- Engine step() benchmarks ---

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

    // 4. Hold-heavy: single line, all Hold notes.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        results.push_back(
            run_bench("hold_heavy", 1, 500, cfg, 3000, phic::NoteKind::Hold));
    }

    // 5. Flick-heavy: single line, all Flick notes.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        results.push_back(
            run_bench("flick_heavy", 1, 500, cfg, 3000, phic::NoteKind::Flick));
    }

    // 6. Drag-heavy: single line, all Drag notes.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        results.push_back(
            run_bench("drag_heavy", 1, 500, cfg, 3000, phic::NoteKind::Drag));
    }

    // 7. Mixed kinds: cycling Tap/Drag/Hold/Flick across multiple lines.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        results.push_back(run_bench_mixed("mixed_kinds", 10, 100, cfg));
    }

    // 8. Heavy mods: moderate note count with mirror + wave + stutter + quantize.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        cfg.mods.mirror               = true;
        cfg.mods.wave                 = true;
        cfg.mods.wave_amplitude_lane  = 1.0;
        cfg.mods.wave_period_sec      = 2.0;
        cfg.mods.stutter              = true;
        cfg.mods.stutter_repeat       = 3;
        cfg.mods.stutter_interval_sec = 0.05;
        cfg.mods.quantize             = true;
        cfg.mods.quantize_step_sec    = 0.1;
        results.push_back(run_bench("heavy_mods", 1, 200, cfg));
    }

    // 9. Reverse time mod.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        cfg.mods.reverse_time = true;
        results.push_back(run_bench("reverse_time", 5, 100, cfg));
    }

    // 10. Full blue mod.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        cfg.mods.full_blue = true;
        results.push_back(run_bench("full_blue", 5, 100, cfg));
    }

    // 11. Stretch mod (2x stretch).
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        cfg.mods.stretch_factor     = 2.0;
        cfg.mods.stretch_anchor_sec = 0.0;
        results.push_back(run_bench("stretch_2x", 5, 100, cfg));
    }

    // 12. Thin-out mod (keep every 2nd note).
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        cfg.mods.thin_out_every = 2;
        results.push_back(run_bench("thin_out_2", 5, 200, cfg));
    }

    // 13. Fade mod (time-based alpha fade).
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        cfg.mods.fade_enable        = true;
        cfg.mods.fade_has_time_start = true;
        cfg.mods.fade_time_start    = 0.0;
        cfg.mods.fade_has_time_end  = true;
        cfg.mods.fade_time_end      = 5.0;
        cfg.mods.fade_alpha_start   = 0.0;
        cfg.mods.fade_alpha_end     = 1.0;
        results.push_back(run_bench("fade_time", 5, 100, cfg));
    }

    // 14. Note rules: filter + override alpha for Hold notes.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        phic::ModConfig::NoteRule rule;
        rule.filter.active = true;
        rule.filter.kinds  = {phic::NoteKind::Hold};
        rule.set.has_alpha = true;
        rule.set.alpha01   = 0.5;
        cfg.mods.note_rules.push_back(rule);
        results.push_back(
            run_bench_mixed("note_rules", 10, 100, cfg));
    }

    // 15. Lane scale mod.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        cfg.mods.lane_scale        = 1.5;
        cfg.mods.lane_scale_center = 3.5;
        results.push_back(run_bench("lane_scale", 5, 200, cfg));
    }

    // 16. All mods combined (maximum stress).
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
        cfg.mods.quantize             = true;
        cfg.mods.quantize_step_sec    = 0.05;
        cfg.mods.reverse_time         = false;  // reverse_time+stutter conflict
        cfg.mods.full_blue            = true;
        cfg.mods.stretch_factor       = 1.5;
        cfg.mods.lane_scale           = 1.2;
        cfg.mods.fade_enable          = true;
        cfg.mods.fade_has_time_start  = true;
        cfg.mods.fade_time_start      = 0.0;
        cfg.mods.fade_has_time_end    = true;
        cfg.mods.fade_time_end        = 10.0;
        results.push_back(run_bench("all_mods", 20, 50, cfg));
    }

    // 17. Combined baseline (original scenario 5).
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

    // 18. Ultra-dense: extreme note count, no mods.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        results.push_back(run_bench("ultra_dense", 100, 50, cfg, 1000));
    }

    // 19. Input events (non-autoplay, one event per step).
    results.push_back(run_bench_input("input_events", 5, 100));

    // 20. Seek interleaved with step.
    results.push_back(run_bench_seek("seek_step", 5, 100));

    // --- apply_mods() preprocessing benchmarks ---

    // A. apply_mods: no-op mods.
    {
        phic::ModConfig mods;
        apply_results.push_back(
            run_bench_apply_mods("noop", 10, 100, mods));
    }

    // B. apply_mods: mirror.
    {
        phic::ModConfig mods;
        mods.mirror = true;
        apply_results.push_back(
            run_bench_apply_mods("mirror", 10, 100, mods));
    }

    // C. apply_mods: wave + quantize.
    {
        phic::ModConfig mods;
        mods.wave              = true;
        mods.wave_amplitude_lane = 1.0;
        mods.wave_period_sec   = 2.0;
        mods.quantize          = true;
        mods.quantize_step_sec = 0.05;
        apply_results.push_back(
            run_bench_apply_mods("wave+quantize", 10, 100, mods));
    }

    // D. apply_mods: stutter (most note-multiplying mod).
    {
        phic::ModConfig mods;
        mods.stutter              = true;
        mods.stutter_repeat       = 4;
        mods.stutter_interval_sec = 0.02;
        apply_results.push_back(
            run_bench_apply_mods("stutter_x4", 10, 100, mods));
    }

    // E. apply_mods: all mods (full preprocessing stress).
    {
        phic::ModConfig mods;
        mods.mirror               = true;
        mods.full_blue            = true;
        mods.wave                 = true;
        mods.wave_amplitude_lane  = 0.5;
        mods.wave_period_sec      = 1.0;
        mods.stutter              = true;
        mods.stutter_repeat       = 2;
        mods.stutter_interval_sec = 0.05;
        mods.quantize             = true;
        mods.quantize_step_sec    = 0.05;
        mods.stretch_factor       = 1.5;
        mods.lane_scale           = 1.2;
        mods.fade_enable          = true;
        apply_results.push_back(
            run_bench_apply_mods("all_mods", 10, 100, mods));
    }

    // --- Print results ---

    std::printf("\n=== Engine step() Benchmark Results ===\n");
    std::printf("%-22s %8s %8s %14s %14s\n",
                "scenario", "notes", "lines", "steps/sec", "ns/step");
    std::printf("%-22s %8s %8s %14s %14s\n",
                "----------------------", "--------", "--------",
                "--------------", "--------------");
    for (const auto& r : results) {
        std::printf("%-22s %8d %8d %14.0f %14.1f\n",
                    r.name, r.note_count, r.line_count,
                    r.steps_per_sec, r.ns_per_step);
    }

    std::printf("\n=== apply_mods() Preprocessing Benchmark Results ===\n");
    std::printf("%-22s %8s %14s %14s\n",
                "scenario", "notes", "calls/sec", "ns/call");
    std::printf("%-22s %8s %14s %14s\n",
                "----------------------", "--------",
                "--------------", "--------------");
    for (const auto& r : apply_results) {
        std::printf("%-22s %8d %14.0f %14.1f\n",
                    r.name, r.note_count,
                    r.calls_per_sec, r.ns_per_call);
    }
    std::printf("\n");

    return 0;
}
