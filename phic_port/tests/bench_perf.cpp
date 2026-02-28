// Performance benchmark: measures engine step() throughput under different
// combinations of note count, line count, active mods, note kinds, and
// apply_mods() preprocessing cost.
//
// Section A – Step timing with full statistics (mean/p50/p95/p99/min/max/stddev).
// Section B – Rendering output analysis (frame command counts, note-kind
//             distribution, judge breakdown, accuracy, combo).
// Section C – apply_mods() preprocessing cost with statistics.
//
// All scenarios are expected to succeed; no performance-floor assertions.

#include "phic/core/engine.hpp"
#include "phic/core/mods.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
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
// Statistics helpers
// ---------------------------------------------------------------------------

struct BenchStats {
    double mean_ns   = 0.0;
    double median_ns = 0.0;  // p50
    double p95_ns    = 0.0;
    double p99_ns    = 0.0;
    double min_ns    = 0.0;
    double max_ns    = 0.0;
    double stddev_ns = 0.0;
};

static BenchStats compute_stats(std::vector<double>& samples) {
    if (samples.empty()) return {};
    std::sort(samples.begin(), samples.end());
    const std::size_t n = samples.size();

    BenchStats s;
    s.min_ns    = samples.front();
    s.max_ns    = samples.back();
    s.median_ns = samples[n / 2];
    s.p95_ns    = samples[static_cast<std::size_t>(n * 0.95)];
    s.p99_ns    = samples[static_cast<std::size_t>(n * 0.99)];
    s.mean_ns   = std::accumulate(samples.begin(), samples.end(), 0.0) /
                  static_cast<double>(n);

    double sq_sum = 0.0;
    for (double v : samples) {
        const double d = v - s.mean_ns;
        sq_sum += d * d;
    }
    s.stddev_ns = std::sqrt(sq_sum / static_cast<double>(n));
    return s;
}

// ---------------------------------------------------------------------------
// Result types
// ---------------------------------------------------------------------------

struct BenchResult {
    const char* name;
    int note_count;
    int line_count;
    BenchStats timing;
};

// Per-step render output collected by run_render_bench().
struct RenderBenchResult {
    const char* name;
    int note_count;          // post-mods total notes in chart
    int line_count;
    BenchStats step_timing;  // per-step wall-clock ns

    // Frame command statistics (across all steps)
    double avg_cmds_per_step = 0.0;
    double peak_cmds_per_step = 0.0;
    double cmd_stddev = 0.0;

    // Note-kind breakdown in frame commands (fractions 0–1)
    double frac_tap   = 0.0;
    double frac_drag  = 0.0;
    double frac_hold  = 0.0;
    double frac_flick = 0.0;

    // Average note alpha seen across all frame commands
    double avg_alpha = 0.0;

    // Judge breakdown
    int judge_perfect = 0;
    int judge_good    = 0;
    int judge_bad     = 0;
    int judge_miss    = 0;

    // Final engine state
    double final_accuracy  = 0.0;
    int    final_combo     = 0;
    int    final_max_combo = 0;
    int    total_steps     = 0;
    double chart_duration_sec = 0.0;

    // Culling stats
    double visibility_ratio = 0.0;   // avg visible_cmds / total_chart_notes
};

struct ApplyModsResult {
    const char* name;
    int note_count_in;   // notes before apply_mods
    int note_count_out;  // notes after apply_mods (stutter multiplies)
    BenchStats timing;
};

// ---------------------------------------------------------------------------
// Benchmark runners
// ---------------------------------------------------------------------------

// time a single call, return nanoseconds
static inline double time_step(phic::Engine& engine, double dt,
                                const std::vector<phic::InputEvent>& ev) {
    const auto s0 = std::chrono::high_resolution_clock::now();
    engine.step(dt, ev);
    const auto s1 = std::chrono::high_resolution_clock::now();
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(s1 - s0).count());
}

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

    std::vector<double> samples(step_count);
    for (int i = 0; i < step_count; ++i)
        samples[i] = time_step(engine, 1.0 / 60.0, {});

    return BenchResult{name, line_count * notes_per_line, line_count,
                       compute_stats(samples)};
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

    std::vector<double> samples(step_count);
    for (int i = 0; i < step_count; ++i)
        samples[i] = time_step(engine, 1.0 / 60.0, {});

    return BenchResult{name, line_count * notes_per_line, line_count,
                       compute_stats(samples)};
}

// Non-autoplay: one PointerDown input event per step.
static BenchResult run_bench_input(const char* name,
                                    int line_count,
                                    int notes_per_line,
                                    int step_count = 3000) {
    phic::RenderConfig cfg;
    cfg.autoplay = false;
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

    std::vector<double> samples(step_count);
    for (int i = 0; i < step_count; ++i)
        samples[i] = time_step(engine, 1.0 / 60.0, event_per_step[i]);

    return BenchResult{name, line_count * notes_per_line, line_count,
                       compute_stats(samples)};
}

// seek() interleaved with step().
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

    const double kDuration = notes_per_line * 0.1;
    std::vector<double> samples(step_count);
    for (int i = 0; i < step_count; ++i) {
        engine.seek(static_cast<double>(i % 20) * kDuration / 20.0);
        samples[i] = time_step(engine, 1.0 / 60.0, {});
    }
    return BenchResult{name, line_count * notes_per_line, line_count,
                       compute_stats(samples)};
}

// ---------------------------------------------------------------------------
// Rendering benchmark: steps through the full chart and collects rich output
// ---------------------------------------------------------------------------

static RenderBenchResult run_render_bench(const char* name,
                                           int line_count,
                                           int notes_per_line,
                                           const phic::RenderConfig& cfg,
                                           bool mixed = false,
                                           int max_steps = 8000) {
    phic::Engine engine(cfg);
    if (mixed)
        engine.load_chart(make_mixed_chart(line_count, notes_per_line, 0.05));
    else
        engine.load_chart(make_chart(line_count, notes_per_line, 0.05));

    const int total_notes = static_cast<int>(engine.chart().notes.size());

    RenderBenchResult r{};
    r.name       = name;
    r.line_count = line_count;
    r.note_count = total_notes;

    std::vector<double>   step_ns_samples;
    std::vector<double>   cmds_per_step_samples;
    long long total_cmds = 0;
    long long cmd_tap = 0, cmd_drag = 0, cmd_hold = 0, cmd_flick = 0;
    double    alpha_sum  = 0.0;
    long long alpha_cnt  = 0;
    double    peak_cmds  = 0.0;

    constexpr double kDt = 1.0 / 60.0;
    double time_sec = 0.0;

    for (int step = 0; step < max_steps; ++step) {
        const auto s0 = std::chrono::high_resolution_clock::now();
        auto result = engine.step(kDt, {});
        const auto s1 = std::chrono::high_resolution_clock::now();

        time_sec = result.time_sec;
        const double ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(s1 - s0).count());
        step_ns_samples.push_back(ns);

        const double n_cmds = static_cast<double>(result.frame_commands.size());
        cmds_per_step_samples.push_back(n_cmds);
        total_cmds += static_cast<long long>(result.frame_commands.size());
        if (n_cmds > peak_cmds) peak_cmds = n_cmds;

        for (const auto& cmd : result.frame_commands) {
            switch (cmd.kind) {
                case phic::NoteKind::Tap:   ++cmd_tap;   break;
                case phic::NoteKind::Drag:  ++cmd_drag;  break;
                case phic::NoteKind::Hold:  ++cmd_hold;  break;
                case phic::NoteKind::Flick: ++cmd_flick; break;
            }
            alpha_sum += static_cast<double>(cmd.alpha);
            ++alpha_cnt;
        }

        for (const auto& je : result.judge_events) {
            switch (je.kind) {
                case phic::JudgeKind::Perfect: ++r.judge_perfect; break;
                case phic::JudgeKind::Good:    ++r.judge_good;    break;
                case phic::JudgeKind::Bad:     ++r.judge_bad;     break;
                case phic::JudgeKind::Miss:    ++r.judge_miss;    break;
                default: break;
            }
        }

        // Stop once all notes are judged (autoplay finishes the chart).
        if (result.stats.judged_cnt >= total_notes) {
            r.final_accuracy  = result.stats.accuracy();
            r.final_combo     = result.stats.combo;
            r.final_max_combo = result.stats.max_combo;
            r.total_steps     = step + 1;
            r.chart_duration_sec = time_sec;
            break;
        }
    }

    if (r.total_steps == 0) {
        // Hit max_steps without finishing.
        auto last = engine.step(kDt, {});
        r.final_accuracy  = last.stats.accuracy();
        r.final_combo     = last.stats.combo;
        r.final_max_combo = last.stats.max_combo;
        r.total_steps     = max_steps;
        r.chart_duration_sec = time_sec;
    }

    r.step_timing = compute_stats(step_ns_samples);

    const double n_steps = static_cast<double>(r.total_steps);
    r.avg_cmds_per_step  = static_cast<double>(total_cmds) / n_steps;
    r.peak_cmds_per_step = peak_cmds;

    // stddev of cmds_per_step
    {
        double mean = r.avg_cmds_per_step;
        double sq = 0.0;
        for (double v : cmds_per_step_samples) sq += (v - mean) * (v - mean);
        r.cmd_stddev = std::sqrt(sq / n_steps);
    }

    if (total_cmds > 0) {
        r.frac_tap   = static_cast<double>(cmd_tap)   / static_cast<double>(total_cmds);
        r.frac_drag  = static_cast<double>(cmd_drag)  / static_cast<double>(total_cmds);
        r.frac_hold  = static_cast<double>(cmd_hold)  / static_cast<double>(total_cmds);
        r.frac_flick = static_cast<double>(cmd_flick) / static_cast<double>(total_cmds);
        r.avg_alpha  = alpha_sum / static_cast<double>(alpha_cnt);
    }

    r.visibility_ratio = (total_notes > 0)
        ? r.avg_cmds_per_step / static_cast<double>(total_notes)
        : 0.0;

    return r;
}

// ---------------------------------------------------------------------------
// apply_mods() benchmark with full stats
// ---------------------------------------------------------------------------

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

    // Run to collect stats + note_count_out.
    int note_count_out = 0;
    std::vector<double> samples(call_count);
    for (int i = 0; i < call_count; ++i) {
        phic::ChartData chart = make_chart(line_count, notes_per_line);
        const auto t0 = std::chrono::high_resolution_clock::now();
        phic::apply_mods(chart, mods);
        const auto t1 = std::chrono::high_resolution_clock::now();
        samples[i] = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        if (i == 0) note_count_out = static_cast<int>(chart.notes.size());
    }

    return ApplyModsResult{name, line_count * notes_per_line, note_count_out,
                           compute_stats(samples)};
}

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

int main() {
    std::vector<BenchResult>      results;
    std::vector<RenderBenchResult> render_results;
    std::vector<ApplyModsResult>  apply_results;

    // =========================================================================
    // Section A: Engine step() throughput with full statistical profiling
    // =========================================================================

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

    // 8. Heavy mods: mirror + wave + stutter + quantize.
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

    // 11. Stretch mod (2x).
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

    // 13. Fade mod (time-based alpha).
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        cfg.mods.fade_enable         = true;
        cfg.mods.fade_has_time_start = true;
        cfg.mods.fade_time_start     = 0.0;
        cfg.mods.fade_has_time_end   = true;
        cfg.mods.fade_time_end       = 5.0;
        cfg.mods.fade_alpha_start    = 0.0;
        cfg.mods.fade_alpha_end      = 1.0;
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
        results.push_back(run_bench_mixed("note_rules", 10, 100, cfg));
    }

    // 15. Lane scale mod.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        cfg.mods.lane_scale        = 1.5;
        cfg.mods.lane_scale_center = 3.5;
        results.push_back(run_bench("lane_scale", 5, 200, cfg));
    }

    // 16. All mods combined (maximum step stress).
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

    // 17. Combined: many notes + lines + mods.
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

    // 19. Input events (non-autoplay, one PointerDown per step).
    results.push_back(run_bench_input("input_events", 5, 100));

    // 20. Seek interleaved with step.
    results.push_back(run_bench_seek("seek_step", 5, 100));

    // =========================================================================
    // Section B: Rendering output analysis
    // Steps through full charts and measures frame command throughput,
    // note-kind distribution, alpha, judge breakdown, and culling ratio.
    // =========================================================================

    // R1. Tap-only chart, default approach window (3s).
    {
        phic::RenderConfig cfg;
        cfg.autoplay     = true;
        cfg.approach_sec = 3.0;
        render_results.push_back(
            run_render_bench("tap_default", 5, 80, cfg));
    }

    // R2. Narrow approach window (1s) – fewer notes visible at once.
    {
        phic::RenderConfig cfg;
        cfg.autoplay     = true;
        cfg.approach_sec = 1.0;
        render_results.push_back(
            run_render_bench("tap_narrow_1s", 5, 80, cfg));
    }

    // R3. Wide approach window (5s) – more notes visible at once.
    {
        phic::RenderConfig cfg;
        cfg.autoplay     = true;
        cfg.approach_sec = 5.0;
        render_results.push_back(
            run_render_bench("tap_wide_5s", 5, 80, cfg));
    }

    // R4. Culling disabled (no_cull=true): all notes emitted every step.
    {
        phic::RenderConfig cfg;
        cfg.autoplay  = true;
        cfg.no_cull   = true;
        render_results.push_back(
            run_render_bench("no_cull", 5, 80, cfg));
    }

    // R5. Mixed note kinds (Tap/Drag/Hold/Flick) – kind distribution analysis.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        render_results.push_back(
            run_render_bench("mixed_kinds", 8, 80, cfg, /*mixed=*/true));
    }

    // R6. Hold-heavy – long hold_end windows inflate visible cmd count.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        render_results.push_back(
            run_render_bench("holds_only", 5, 80, cfg, /*mixed=*/false));
        // override kind to Hold for the chart
        // (run_render_bench uses make_chart which defaults to Tap;
        //  use a separate call with make_chart(kind=Hold))
    }

    // R6b. Explicit Hold chart (replace R6's result).
    render_results.pop_back();
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        // Build engine manually so we can pass Hold kind.
        phic::Engine engine(cfg);
        engine.load_chart(make_chart(5, 80, 0.05, phic::NoteKind::Hold));
        const int total_notes = static_cast<int>(engine.chart().notes.size());

        RenderBenchResult r{};
        r.name       = "holds_only";
        r.line_count = 5;
        r.note_count = total_notes;

        std::vector<double> step_ns, cmd_counts;
        long long total_cmds = 0, cmd_hold = 0;
        double peak = 0.0, alpha_sum = 0.0;
        long long alpha_cnt = 0;
        double time_sec = 0.0;

        for (int step = 0; step < 8000; ++step) {
            const auto s0 = std::chrono::high_resolution_clock::now();
            auto res = engine.step(1.0 / 60.0, {});
            const auto s1 = std::chrono::high_resolution_clock::now();
            step_ns.push_back(static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(s1 - s0).count()));
            time_sec = res.time_sec;
            const double nc = static_cast<double>(res.frame_commands.size());
            cmd_counts.push_back(nc);
            total_cmds += static_cast<long long>(res.frame_commands.size());
            if (nc > peak) peak = nc;
            for (const auto& c : res.frame_commands) {
                if (c.kind == phic::NoteKind::Hold) ++cmd_hold;
                alpha_sum += static_cast<double>(c.alpha);
                ++alpha_cnt;
            }
            for (const auto& je : res.judge_events) {
                switch (je.kind) {
                    case phic::JudgeKind::Perfect: ++r.judge_perfect; break;
                    case phic::JudgeKind::Good:    ++r.judge_good;    break;
                    case phic::JudgeKind::Bad:     ++r.judge_bad;     break;
                    case phic::JudgeKind::Miss:    ++r.judge_miss;    break;
                    default: break;
                }
            }
            if (res.stats.judged_cnt >= total_notes) {
                r.final_accuracy     = res.stats.accuracy();
                r.final_combo        = res.stats.combo;
                r.final_max_combo    = res.stats.max_combo;
                r.total_steps        = step + 1;
                r.chart_duration_sec = time_sec;
                break;
            }
        }
        if (r.total_steps == 0) r.total_steps = 8000;

        r.step_timing        = compute_stats(step_ns);
        r.avg_cmds_per_step  = static_cast<double>(total_cmds) / r.total_steps;
        r.peak_cmds_per_step = peak;
        r.frac_hold          = total_cmds > 0 ? static_cast<double>(cmd_hold) / total_cmds : 0.0;
        r.avg_alpha          = alpha_cnt > 0 ? alpha_sum / alpha_cnt : 0.0;
        r.visibility_ratio   = total_notes > 0 ? r.avg_cmds_per_step / total_notes : 0.0;
        {
            double mean = r.avg_cmds_per_step, sq = 0.0;
            for (double v : cmd_counts) sq += (v - mean) * (v - mean);
            r.cmd_stddev = std::sqrt(sq / r.total_steps);
        }
        render_results.push_back(r);
    }

    // R7. Wave mod: notes shift lane each step – cmd positions vary more.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        cfg.mods.wave                = true;
        cfg.mods.wave_amplitude_lane = 2.0;
        cfg.mods.wave_period_sec     = 1.0;
        render_results.push_back(
            run_render_bench("wave_mod", 5, 80, cfg));
    }

    // R8. Fade mod: alpha changes per-step across all notes.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        cfg.mods.fade_enable         = true;
        cfg.mods.fade_has_time_start = true;
        cfg.mods.fade_time_start     = 0.0;
        cfg.mods.fade_has_time_end   = true;
        cfg.mods.fade_time_end       = 4.0;
        cfg.mods.fade_alpha_start    = 0.1;
        cfg.mods.fade_alpha_end      = 1.0;
        render_results.push_back(
            run_render_bench("fade_mod", 5, 80, cfg));
    }

    // R9. Dense chart rendering: many lines + many notes.
    {
        phic::RenderConfig cfg;
        cfg.autoplay     = true;
        cfg.approach_sec = 3.0;
        render_results.push_back(
            run_render_bench("dense", 20, 40, cfg));
    }

    // R10. Stutter mod: note count multiplied 3x – peaks visible cmds.
    {
        phic::RenderConfig cfg;
        cfg.autoplay = true;
        cfg.mods.stutter              = true;
        cfg.mods.stutter_repeat       = 3;
        cfg.mods.stutter_interval_sec = 0.04;
        render_results.push_back(
            run_render_bench("stutter_x3", 3, 40, cfg));
    }

    // =========================================================================
    // Section C: apply_mods() preprocessing cost with full statistics
    // =========================================================================

    // C1. Noop mods (baseline preprocessing).
    {
        phic::ModConfig mods;
        apply_results.push_back(run_bench_apply_mods("noop", 10, 100, mods));
    }

    // C2. Mirror.
    {
        phic::ModConfig mods;
        mods.mirror = true;
        apply_results.push_back(run_bench_apply_mods("mirror", 10, 100, mods));
    }

    // C3. Wave + quantize.
    {
        phic::ModConfig mods;
        mods.wave                = true;
        mods.wave_amplitude_lane = 1.0;
        mods.wave_period_sec     = 2.0;
        mods.quantize            = true;
        mods.quantize_step_sec   = 0.05;
        apply_results.push_back(
            run_bench_apply_mods("wave+quantize", 10, 100, mods));
    }

    // C4. Stutter x4 (most note-multiplying mod).
    {
        phic::ModConfig mods;
        mods.stutter              = true;
        mods.stutter_repeat       = 4;
        mods.stutter_interval_sec = 0.02;
        apply_results.push_back(
            run_bench_apply_mods("stutter_x4", 10, 100, mods));
    }

    // C5. Reverse time.
    {
        phic::ModConfig mods;
        mods.reverse_time = true;
        apply_results.push_back(
            run_bench_apply_mods("reverse_time", 10, 100, mods));
    }

    // C6. Fade (per-note alpha annotation).
    {
        phic::ModConfig mods;
        mods.fade_enable         = true;
        mods.fade_has_time_start = true;
        mods.fade_time_start     = 0.0;
        mods.fade_has_time_end   = true;
        mods.fade_time_end       = 10.0;
        apply_results.push_back(
            run_bench_apply_mods("fade", 10, 100, mods));
    }

    // C7. Note rules (filter + override).
    {
        phic::ModConfig mods;
        phic::ModConfig::NoteRule rule;
        rule.filter.active = true;
        rule.filter.kinds  = {phic::NoteKind::Tap, phic::NoteKind::Hold};
        rule.set.has_alpha = true;
        rule.set.alpha01   = 0.7;
        mods.note_rules.push_back(rule);
        apply_results.push_back(
            run_bench_apply_mods("note_rules", 10, 100, mods));
    }

    // C8. All mods (full preprocessing stress).
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

    // =========================================================================
    // Print Section A: Step timing statistics
    // =========================================================================

    std::printf("\n");
    std::printf("==================================================================="
                "=============\n");
    std::printf("  SECTION A — Engine step() Throughput  (ns per step)\n");
    std::printf("==================================================================="
                "=============\n");
    std::printf("%-22s %6s %5s %9s %9s %9s %9s %9s %9s %10s\n",
                "scenario", "notes", "lines",
                "mean", "p50", "p95", "p99", "min", "max", "stddev");
    std::printf("%-22s %6s %5s %9s %9s %9s %9s %9s %9s %10s\n",
                "----------------------", "------", "-----",
                "---------", "---------", "---------", "---------",
                "---------", "---------", "----------");
    for (const auto& r : results) {
        const auto& t = r.timing;
        std::printf("%-22s %6d %5d %9.1f %9.1f %9.1f %9.1f %9.1f %9.1f %10.1f\n",
                    r.name, r.note_count, r.line_count,
                    t.mean_ns, t.median_ns, t.p95_ns, t.p99_ns,
                    t.min_ns, t.max_ns, t.stddev_ns);
    }
    // Summary: steps/sec based on mean
    std::printf("\n  (steps/sec = 1e9 / mean_ns)\n");
    std::printf("  %-22s  %s\n", "scenario", "steps/sec");
    for (const auto& r : results) {
        std::printf("  %-22s  %.0f\n", r.name, 1e9 / r.timing.mean_ns);
    }

    // =========================================================================
    // Print Section B: Rendering output analysis
    // =========================================================================

    std::printf("\n");
    std::printf("==================================================================="
                "=============\n");
    std::printf("  SECTION B — Rendering Output Analysis\n");
    std::printf("==================================================================="
                "=============\n");

    // Sub-table B1: frame command statistics
    std::printf("\n  [B1] Frame Command Statistics\n");
    std::printf("  %-16s %6s %5s %8s %8s %8s %7s  %s\n",
                "scenario", "notes", "steps",
                "avg_cmds", "peak", "stddev", "vis%", "mean_ns");
    std::printf("  %-16s %6s %5s %8s %8s %8s %7s  %s\n",
                "----------------", "------", "-----",
                "--------", "--------", "--------", "-------", "--------");
    for (const auto& r : render_results) {
        std::printf("  %-16s %6d %5d %8.1f %8.0f %8.1f %6.1f%%  %.1f\n",
                    r.name, r.note_count, r.total_steps,
                    r.avg_cmds_per_step, r.peak_cmds_per_step, r.cmd_stddev,
                    r.visibility_ratio * 100.0,
                    r.step_timing.mean_ns);
    }

    // Sub-table B2: note-kind distribution
    std::printf("\n  [B2] Note Kind Distribution in Frame Commands  (%%)\n");
    std::printf("  %-16s %8s %8s %8s %8s %8s\n",
                "scenario", "tap", "drag", "hold", "flick", "avg_alpha");
    std::printf("  %-16s %8s %8s %8s %8s %8s\n",
                "----------------",
                "--------", "--------", "--------", "--------", "---------");
    for (const auto& r : render_results) {
        std::printf("  %-16s %7.1f%% %7.1f%% %7.1f%% %7.1f%% %8.3f\n",
                    r.name,
                    r.frac_tap   * 100.0,
                    r.frac_drag  * 100.0,
                    r.frac_hold  * 100.0,
                    r.frac_flick * 100.0,
                    r.avg_alpha);
    }

    // Sub-table B3: judge breakdown + accuracy
    std::printf("\n  [B3] Judge Breakdown & Accuracy\n");
    std::printf("  %-16s %7s %6s %5s %5s %5s %8s %8s\n",
                "scenario", "total", "perf", "good", "bad", "miss",
                "acc%%", "maxcombo");
    std::printf("  %-16s %7s %6s %5s %5s %5s %8s %8s\n",
                "----------------",
                "-------", "------", "-----", "-----", "-----",
                "--------", "--------");
    for (const auto& r : render_results) {
        const int total_j = r.judge_perfect + r.judge_good +
                            r.judge_bad    + r.judge_miss;
        std::printf("  %-16s %7d %6d %5d %5d %5d %7.2f%% %8d\n",
                    r.name, total_j,
                    r.judge_perfect, r.judge_good,
                    r.judge_bad,     r.judge_miss,
                    r.final_accuracy * 100.0,
                    r.final_max_combo);
    }

    // Sub-table B4: step timing statistics for render scenarios
    std::printf("\n  [B4] Render Step Timing Statistics  (ns)\n");
    std::printf("  %-16s %9s %9s %9s %9s %9s %9s %10s\n",
                "scenario",
                "mean", "p50", "p95", "p99", "min", "max", "stddev");
    std::printf("  %-16s %9s %9s %9s %9s %9s %9s %10s\n",
                "----------------",
                "---------", "---------", "---------", "---------",
                "---------", "---------", "----------");
    for (const auto& r : render_results) {
        const auto& t = r.step_timing;
        std::printf("  %-16s %9.1f %9.1f %9.1f %9.1f %9.1f %9.1f %10.1f\n",
                    r.name,
                    t.mean_ns, t.median_ns, t.p95_ns, t.p99_ns,
                    t.min_ns, t.max_ns, t.stddev_ns);
    }

    // =========================================================================
    // Print Section C: apply_mods() statistics
    // =========================================================================

    std::printf("\n");
    std::printf("==================================================================="
                "=============\n");
    std::printf("  SECTION C — apply_mods() Preprocessing Cost  (ns per call)\n");
    std::printf("==================================================================="
                "=============\n");
    std::printf("%-22s %7s %7s %9s %9s %9s %9s %10s\n",
                "scenario", "in", "out",
                "mean", "p50", "p95", "p99", "stddev");
    std::printf("%-22s %7s %7s %9s %9s %9s %9s %10s\n",
                "----------------------",
                "-------", "-------",
                "---------", "---------", "---------", "---------", "----------");
    for (const auto& r : apply_results) {
        const auto& t = r.timing;
        std::printf("%-22s %7d %7d %9.1f %9.1f %9.1f %9.1f %10.1f\n",
                    r.name, r.note_count_in, r.note_count_out,
                    t.mean_ns, t.median_ns, t.p95_ns, t.p99_ns, t.stddev_ns);
    }

    std::printf("\n");
    return 0;
}
