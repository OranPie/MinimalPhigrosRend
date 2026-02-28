// Performance benchmark for the phic engine.
// Outputs a complete Markdown report to stdout; redirect to bench_results.md.
//
// Sections:
//   A – Engine step() throughput (min 500 notes / 20 lines, up to 1M / 200 lines)
//   B – Loading cost & memory (load_chart() time + RSS delta + data bytes)
//   C – Event processing throughput (up to 1e8 events)
//   D – Rendering output analysis (frame commands, judges, alpha, culling)
//   E – apply_mods() preprocessing cost (500 → 1M notes)

#include "phic/core/engine.hpp"
#include "phic/core/mods.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Hardware / system info helpers
// ---------------------------------------------------------------------------

static std::string read_proc_field(const char* path, const char* key) {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.find(key) == 0) {
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string v = line.substr(colon + 1);
                // trim leading whitespace + trailing newline/tab
                std::size_t s = v.find_first_not_of(" \t");
                std::size_t e = v.find_last_not_of(" \t\r\n");
                if (s != std::string::npos) return v.substr(s, e - s + 1);
            }
        }
    }
    return "unknown";
}

static long get_rss_kb() {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long rss = -1;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "VmRSS: %ld kB", &rss) == 1) break;
    }
    fclose(f);
    return rss;
}

static std::string compiler_id() {
#if defined(__clang__)
    char buf[64];
    snprintf(buf, sizeof(buf), "Clang %d.%d.%d",
             __clang_major__, __clang_minor__, __clang_patchlevel__);
    return buf;
#elif defined(__GNUC__)
    char buf[64];
    snprintf(buf, sizeof(buf), "GCC %d.%d.%d",
             __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
    return buf;
#else
    return "unknown";
#endif
}

// ---------------------------------------------------------------------------
// Chart builders
// ---------------------------------------------------------------------------

static phic::ChartData make_chart(int line_count, int notes_per_line,
                                   double note_interval_sec = 0.05,
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
            note.hold_end  = note.t_hit + (kind == phic::NoteKind::Hold ? 0.08 : 0.0);
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

static phic::ChartData make_mixed_chart(int line_count, int notes_per_line,
                                         double note_interval_sec = 0.05) {
    constexpr int kLaneCount = 8;
    const phic::NoteKind kKinds[4] = {
        phic::NoteKind::Tap,  phic::NoteKind::Drag,
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
            note.hold_end  = note.t_hit + (k == phic::NoteKind::Hold ? 0.08 : 0.0);
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

// Analytical estimate of chart + engine data bytes.
static std::size_t engine_data_bytes(int note_count, int line_count) {
    constexpr std::size_t kNoteBytes     = sizeof(phic::RuntimeNote);
    constexpr std::size_t kNoteStateSize = 3; // 3 bool fields, typically 3 bytes
    constexpr std::size_t kIdxSize       = sizeof(std::size_t);
    // chart notes + lines + note_states + lane indices (per-note) + vec overhead
    return static_cast<std::size_t>(note_count) * (kNoteBytes + kNoteStateSize + kIdxSize)
         + static_cast<std::size_t>(line_count) * sizeof(phic::RuntimeLine)
         + 256; // misc overhead
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

struct BenchStats {
    double mean_ns   = 0.0;
    double median_ns = 0.0;
    double p95_ns    = 0.0;
    double p99_ns    = 0.0;
    double min_ns    = 0.0;
    double max_ns    = 0.0;
    double stddev_ns = 0.0;
};

static BenchStats compute_stats(std::vector<double>& s) {
    if (s.empty()) return {};
    std::sort(s.begin(), s.end());
    const std::size_t n = s.size();
    BenchStats r;
    r.min_ns    = s.front();
    r.max_ns    = s.back();
    r.median_ns = s[n / 2];
    r.p95_ns    = s[static_cast<std::size_t>(n * 0.95)];
    r.p99_ns    = s[static_cast<std::size_t>(n * 0.99)];
    r.mean_ns   = std::accumulate(s.begin(), s.end(), 0.0) / n;
    double sq = 0.0;
    for (double v : s) { double d = v - r.mean_ns; sq += d * d; }
    r.stddev_ns = std::sqrt(sq / n);
    return r;
}

// ---------------------------------------------------------------------------
// Result types
// ---------------------------------------------------------------------------

struct BenchResult {
    const char* name;
    int         note_count;
    int         line_count;
    int         step_count;
    BenchStats  timing;
};

struct LoadingResult {
    const char* name;
    int         note_count_in;
    int         note_count_out;
    int         line_count;
    BenchStats  timing;       // ns per load_chart() call
    long        rss_delta_kb; // process RSS increase
    std::size_t data_bytes;   // analytical data size estimate
};

struct EventsResult {
    const char*  name;
    int          notes;
    int          events_per_step;
    long long    total_events;
    int          step_count;
    BenchStats   timing;      // ns per step (including event dispatch)
    double       ns_per_event;
};

struct RenderBenchResult {
    const char* name;
    int note_count;
    int line_count;
    BenchStats step_timing;
    double avg_cmds_per_step;
    double peak_cmds_per_step;
    double cmd_stddev;
    double frac_tap, frac_drag, frac_hold, frac_flick;
    double avg_alpha;
    int judge_perfect, judge_good, judge_bad, judge_miss;
    double final_accuracy;
    int final_combo, final_max_combo;
    int total_steps;
    double chart_duration_sec;
    double visibility_ratio;
};

struct ApplyModsResult {
    const char* name;
    int         note_count_in;
    int         note_count_out;
    BenchStats  timing;
};



static inline double time_step_ns(phic::Engine& e, double dt,
                                   const std::vector<phic::InputEvent>& ev) {
    const auto t0 = std::chrono::high_resolution_clock::now();
    e.step(dt, ev);
    const auto t1 = std::chrono::high_resolution_clock::now();
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
}

static BenchResult run_bench(const char* name,
                              int line_count,
                              int notes_per_line,
                              const phic::RenderConfig& cfg,
                              int step_count = 3000,
                              phic::NoteKind kind = phic::NoteKind::Tap,
                              double note_interval = 0.05) {
    phic::Engine engine(cfg);
    engine.load_chart(make_chart(line_count, notes_per_line, note_interval, kind));
    for (int i = 0; i < 20; ++i) engine.step(1.0 / 60.0, {});
    engine.reset();
    std::vector<double> samples(step_count);
    for (int i = 0; i < step_count; ++i)
        samples[i] = time_step_ns(engine, 1.0 / 60.0, {});
    return BenchResult{name, line_count * notes_per_line, line_count,
                       step_count, compute_stats(samples)};
}

static BenchResult run_bench_mixed(const char* name,
                                    int line_count,
                                    int notes_per_line,
                                    const phic::RenderConfig& cfg,
                                    int step_count = 3000,
                                    double note_interval = 0.05) {
    phic::Engine engine(cfg);
    engine.load_chart(make_mixed_chart(line_count, notes_per_line, note_interval));
    for (int i = 0; i < 20; ++i) engine.step(1.0 / 60.0, {});
    engine.reset();
    std::vector<double> samples(step_count);
    for (int i = 0; i < step_count; ++i)
        samples[i] = time_step_ns(engine, 1.0 / 60.0, {});
    return BenchResult{name, line_count * notes_per_line, line_count,
                       step_count, compute_stats(samples)};
}

static BenchResult run_bench_seek(const char* name,
                                   int line_count,
                                   int notes_per_line,
                                   int step_count = 2000) {
    phic::RenderConfig cfg;
    cfg.autoplay = true;
    phic::Engine engine(cfg);
    engine.load_chart(make_chart(line_count, notes_per_line));
    for (int i = 0; i < 20; ++i) engine.step(1.0 / 60.0, {});
    engine.reset();
    const double kDuration = notes_per_line * 0.05;
    std::vector<double> samples(step_count);
    for (int i = 0; i < step_count; ++i) {
        engine.seek(static_cast<double>(i % 30) * kDuration / 30.0);
        samples[i] = time_step_ns(engine, 1.0 / 60.0, {});
    }
    return BenchResult{name, line_count * notes_per_line, line_count,
                       step_count, compute_stats(samples)};
}

// ---------------------------------------------------------------------------
// Runner – Section B: loading cost + memory
// ---------------------------------------------------------------------------

static LoadingResult run_loading_bench(const char* name,
                                        int line_count,
                                        int notes_per_line,
                                        const phic::ModConfig& mods,
                                        int call_count = 200) {
    // warm-up
    for (int i = 0; i < 3; ++i) {
        phic::RenderConfig cfg;
        cfg.mods = mods;
        phic::Engine e(cfg);
        e.load_chart(make_chart(line_count, notes_per_line));
    }

    int note_count_out = 0;
    long rss_before = get_rss_kb();
    std::vector<double> samples;
    samples.reserve(call_count);
    for (int i = 0; i < call_count; ++i) {
        phic::RenderConfig cfg;
        cfg.mods = mods;
        phic::Engine e(cfg);
        auto chart = make_chart(line_count, notes_per_line);
        const auto t0 = std::chrono::high_resolution_clock::now();
        e.load_chart(std::move(chart));
        const auto t1 = std::chrono::high_resolution_clock::now();
        samples.push_back(static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
        if (i == 0) note_count_out = static_cast<int>(e.chart().notes.size());
    }
    long rss_after = get_rss_kb();

    return LoadingResult{name, line_count * notes_per_line, note_count_out,
                         line_count, compute_stats(samples),
                         std::max(0L, rss_after - rss_before),
                         engine_data_bytes(note_count_out, line_count)};
}

// ---------------------------------------------------------------------------
// Runner – Section C: event processing throughput
// ---------------------------------------------------------------------------

static EventsResult run_events_bench(const char* name,
                                      int line_count,
                                      int notes_per_line,
                                      int events_per_step,
                                      int step_count = 2000) {
    phic::RenderConfig cfg;
    cfg.autoplay = false;   // events drive judgment

    phic::Engine engine(cfg);
    // Dense chart so notes are available throughout the run window.
    engine.load_chart(make_chart(line_count, notes_per_line, 0.05));

    // Warm-up (autoplay to exhaust notes, then reset).
    phic::RenderConfig ap_cfg;
    ap_cfg.autoplay = true;
    phic::Engine warmup_engine(ap_cfg);
    warmup_engine.load_chart(make_chart(line_count, std::min(notes_per_line, 50), 0.05));
    for (int i = 0; i < 100; ++i) warmup_engine.step(1.0 / 60.0, {});

    std::vector<phic::InputEvent> step_events;
    step_events.reserve(static_cast<std::size_t>(events_per_step));

    std::vector<double> samples(step_count);
    for (int i = 0; i < step_count; ++i) {
        step_events.clear();
        for (int j = 0; j < events_per_step; ++j) {
            phic::InputEvent ev;
            ev.type       = phic::InputEvent::Type::PointerDown;
            ev.lane       = (j * 7 + i * 3) % 8;  // pseudo-random lanes
            ev.event_time = static_cast<double>(i) / 60.0;
            step_events.push_back(ev);
        }
        const auto t0 = std::chrono::high_resolution_clock::now();
        engine.step(1.0 / 60.0, step_events);
        const auto t1 = std::chrono::high_resolution_clock::now();
        samples[i] = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    const long long total_events = static_cast<long long>(events_per_step) * step_count;
    BenchStats st = compute_stats(samples);
    const double ns_per_event = (events_per_step > 0)
        ? st.mean_ns / static_cast<double>(events_per_step) : 0.0;

    return EventsResult{name, line_count * notes_per_line,
                        events_per_step, total_events, step_count,
                        st, ns_per_event};
}

// ---------------------------------------------------------------------------
// Runner – Section D: rendering output analysis
// ---------------------------------------------------------------------------

static RenderBenchResult run_render_bench(const char* name,
                                           int line_count,
                                           int notes_per_line,
                                           const phic::RenderConfig& cfg,
                                           phic::NoteKind kind = phic::NoteKind::Tap,
                                           bool mixed = false,
                                           int max_steps = 12000) {
    phic::Engine engine(cfg);
    if (mixed)
        engine.load_chart(make_mixed_chart(line_count, notes_per_line, 0.05));
    else
        engine.load_chart(make_chart(line_count, notes_per_line, 0.05, kind));

    const int total_notes = static_cast<int>(engine.chart().notes.size());

    RenderBenchResult r{};
    r.name       = name;
    r.line_count = line_count;
    r.note_count = total_notes;

    std::vector<double> step_ns, cmd_cnts;
    long long total_cmds = 0, cmd_tap = 0, cmd_drag = 0, cmd_hold = 0, cmd_flick = 0;
    double alpha_sum = 0.0;
    long long alpha_cnt = 0;
    double peak = 0.0;
    double time_sec = 0.0;
    constexpr double kDt = 1.0 / 60.0;

    for (int step = 0; step < max_steps; ++step) {
        const auto s0 = std::chrono::high_resolution_clock::now();
        auto res = engine.step(kDt, {});
        const auto s1 = std::chrono::high_resolution_clock::now();
        time_sec = res.time_sec;
        const double dns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(s1 - s0).count());
        step_ns.push_back(dns);
        const double nc = static_cast<double>(res.frame_commands.size());
        cmd_cnts.push_back(nc);
        if (nc > peak) peak = nc;
        total_cmds += static_cast<long long>(res.frame_commands.size());
        for (const auto& c : res.frame_commands) {
            switch (c.kind) {
                case phic::NoteKind::Tap:   ++cmd_tap;   break;
                case phic::NoteKind::Drag:  ++cmd_drag;  break;
                case phic::NoteKind::Hold:  ++cmd_hold;  break;
                case phic::NoteKind::Flick: ++cmd_flick; break;
            }
            alpha_sum += c.alpha; ++alpha_cnt;
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
            r.final_accuracy  = res.stats.accuracy();
            r.final_combo     = res.stats.combo;
            r.final_max_combo = res.stats.max_combo;
            r.total_steps     = step + 1;
            r.chart_duration_sec = time_sec;
            break;
        }
    }
    if (r.total_steps == 0) {
        auto last = engine.step(kDt, {});
        r.final_accuracy     = last.stats.accuracy();
        r.final_combo        = last.stats.combo;
        r.final_max_combo    = last.stats.max_combo;
        r.total_steps        = max_steps;
        r.chart_duration_sec = time_sec;
    }

    r.step_timing        = compute_stats(step_ns);
    const double n_steps = static_cast<double>(r.total_steps);
    r.avg_cmds_per_step  = static_cast<double>(total_cmds) / n_steps;
    r.peak_cmds_per_step = peak;
    double sq = 0.0;
    for (double v : cmd_cnts) { double d = v - r.avg_cmds_per_step; sq += d * d; }
    r.cmd_stddev = std::sqrt(sq / n_steps);
    if (total_cmds > 0) {
        double tc = static_cast<double>(total_cmds);
        r.frac_tap   = cmd_tap   / tc;
        r.frac_drag  = cmd_drag  / tc;
        r.frac_hold  = cmd_hold  / tc;
        r.frac_flick = cmd_flick / tc;
        r.avg_alpha  = alpha_cnt > 0 ? alpha_sum / alpha_cnt : 0.0;
    }
    r.visibility_ratio = total_notes > 0
        ? r.avg_cmds_per_step / static_cast<double>(total_notes) : 0.0;
    return r;
}

// ---------------------------------------------------------------------------
// Runner – Section E: apply_mods() preprocessing
// ---------------------------------------------------------------------------

static ApplyModsResult run_bench_apply_mods(const char* name,
                                             int line_count,
                                             int notes_per_line,
                                             const phic::ModConfig& mods,
                                             int call_count = 300) {
    for (int i = 0; i < 5; ++i) {
        auto c = make_chart(line_count, notes_per_line);
        phic::apply_mods(c, mods);
    }
    int note_count_out = 0;
    std::vector<double> s(call_count);
    for (int i = 0; i < call_count; ++i) {
        auto c = make_chart(line_count, notes_per_line);
        const auto t0 = std::chrono::high_resolution_clock::now();
        phic::apply_mods(c, mods);
        const auto t1 = std::chrono::high_resolution_clock::now();
        s[i] = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        if (i == 0) note_count_out = static_cast<int>(c.notes.size());
    }
    return ApplyModsResult{name, line_count * notes_per_line,
                           note_count_out, compute_stats(s)};
}

// ---------------------------------------------------------------------------
// Chart builder helpers with synthetic line events (Section F)
// ---------------------------------------------------------------------------

// Build a single EasedTrack with `seg_count` evenly spaced segments over `duration` seconds.
static phic::EasedTrack make_eased_track(int seg_count, double duration,
                                          double v_lo, double v_hi,
                                          int easing_type = 2) {
    phic::EasedTrack trk;
    trk.default_val = v_lo;
    const double dt = duration / std::max(1, seg_count);
    for (int i = 0; i < seg_count; ++i) {
        phic::EasedSeg s;
        s.t0 = i * dt;  s.t1 = (i + 1) * dt;
        s.v0 = (i % 2 == 0) ? v_lo : v_hi;
        s.v1 = (i % 2 == 0) ? v_hi : v_lo;
        s.L = 0.0; s.R = 1.0;
        s.easing_type = easing_type;
        trk.segs.push_back(s);
    }
    return trk;
}

// Build a single IntegralTrack with `seg_count` speed segments.
static phic::IntegralTrack make_integral_track(int seg_count, double duration) {
    phic::IntegralTrack trk;
    if (seg_count <= 0) return trk;
    const double dt = duration / seg_count;
    double prefix = 0.0;
    for (int i = 0; i < seg_count; ++i) {
        phic::Seg1D s;
        s.t0 = i * dt; s.t1 = (i + 1) * dt;
        s.v0 = 1.0 + 0.5 * (i % 3);
        s.v1 = s.v0;
        s.prefix = prefix;
        prefix += s.v0 * dt;
        trk.segs.push_back(s);
    }
    return trk;
}

// Build a SumTrack with `layer_count` EasedTrack layers, each with `segs_per_layer` segments.
static phic::SumTrack make_sum_track_anim(int layer_count, int segs_per_layer,
                                           double duration, double v_lo, double v_hi,
                                           double default_val = 0.0) {
    phic::SumTrack st;
    st.default_val = default_val;
    for (int l = 0; l < layer_count; ++l) {
        st.layers.push_back(
            make_eased_track(segs_per_layer, duration, v_lo, v_hi, 2 + l % 28));
    }
    return st;
}

// Build a chart where each line has synthetic animation tracks.
static phic::ChartData make_chart_with_anims(int line_count, int notes_per_line,
                                              int segs_per_channel, int speed_segs,
                                              int layers = 1,
                                              double note_interval_sec = 0.05) {
    phic::ChartData chart = make_chart(line_count, notes_per_line, note_interval_sec);
    const double duration = notes_per_line * note_interval_sec + 2.0;
    for (auto& ln : chart.lines) {
        ln.anim.pos_x      = make_sum_track_anim(layers, segs_per_channel, duration, -200.0, 200.0);
        ln.anim.pos_y      = make_sum_track_anim(layers, segs_per_channel, duration, -100.0, 100.0);
        ln.anim.rot_rad    = make_sum_track_anim(layers, segs_per_channel, duration, -0.5, 0.5);
        ln.anim.alpha_raw  = make_sum_track_anim(layers, segs_per_channel, duration, 100.0, 255.0, 255.0);
        ln.anim.scroll_px  = make_integral_track(speed_segs, duration);
        ln.anim.total_event_segs = layers * segs_per_channel * 4 + speed_segs;
    }
    return chart;
}

struct LineAnimResult {
    const char* name;
    int  line_count;
    int  segs_per_channel;
    int  layers;
    int  total_event_segs;
    BenchStats step_timing;
    double ns_per_line_step;
};

static LineAnimResult run_line_anim_bench(const char* name,
                                           int line_count, int notes_per_line,
                                           int segs_per_channel, int speed_segs,
                                           int layers = 1,
                                           int step_count = 2000) {
    phic::RenderConfig cfg; cfg.autoplay = true;
    phic::Engine engine(cfg);
    engine.load_chart(
        make_chart_with_anims(line_count, notes_per_line, segs_per_channel, speed_segs, layers));
    for (int i = 0; i < 20; ++i) engine.step(1.0 / 60.0, {});
    engine.reset();
    std::vector<double> samples(step_count);
    for (int i = 0; i < step_count; ++i)
        samples[i] = time_step_ns(engine, 1.0 / 60.0, {});
    BenchStats st = compute_stats(samples);
    LineAnimResult r;
    r.name              = name;
    r.line_count        = line_count;
    r.segs_per_channel  = segs_per_channel;
    r.layers            = layers;
    r.total_event_segs  = layers * segs_per_channel * 4 + speed_segs;
    r.step_timing       = st;
    r.ns_per_line_step  = (line_count > 0) ? st.mean_ns / line_count : 0.0;
    return r;
}

// ---------------------------------------------------------------------------
// Markdown helpers
// ---------------------------------------------------------------------------

static void md_hr()    { std::printf("\n---\n\n"); }
static void md_h1(const char* s) { std::printf("# %s\n\n", s); }
static void md_h2(const char* s) { std::printf("## %s\n\n", s); }
static void md_h3(const char* s) { std::printf("### %s\n\n", s); }
static void md_note(const char* s) { std::printf("> %s\n\n", s); }
static void md_kv(const char* k, const char* v) {
    std::printf("| %-30s | %-45s |\n", k, v);
}
static void md_kv_s(const char* k, const std::string& v) { md_kv(k, v.c_str()); }

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    // =========================================================================
    // Header
    // =========================================================================
    md_h1("PhiC Render Engine \xe2\x80\x94 Performance Benchmark Report");

    // =========================================================================
    // Hardware
    // =========================================================================
    md_h2("Testing Environment");
    std::printf("| %-30s | %-45s |\n", "Field", "Value");
    std::printf("|%s|%s|\n",
                "--------------------------------",
                "-----------------------------------------------");

    std::string cpu_model = read_proc_field("/proc/cpuinfo", "model name");
    std::string cpu_freq  = read_proc_field("/proc/cpuinfo", "cpu MHz");
    std::string mem_total = read_proc_field("/proc/meminfo", "MemTotal");
    std::string cache_sz  = read_proc_field("/proc/cpuinfo", "cache size");

    // Count logical CPUs
    int cpu_count = 0;
    { std::ifstream f("/proc/cpuinfo");
      std::string ln;
      while (std::getline(f, ln))
          if (ln.find("processor") == 0) ++cpu_count; }

    // Kernel version
    char kver[256] = "unknown";
    { FILE* f = popen("uname -r", "r");
      if (f) { fgets(kver, sizeof(kver), f); pclose(f);
        kver[strcspn(kver, "\n")] = 0; } }

    char cpu_line[256];
    snprintf(cpu_line, sizeof(cpu_line), "%s (%d logical cores)",
             cpu_model.c_str(), cpu_count);

    // Format mem in GiB
    long mem_kb = 0;
    sscanf(mem_total.c_str(), "%ld kB", &mem_kb);
    char mem_line[64];
    snprintf(mem_line, sizeof(mem_line), "%.1f GiB (%s)",
             mem_kb / 1048576.0, mem_total.c_str());

    md_kv("CPU", cpu_line);
    md_kv("CPU Freq (approx)", cpu_freq.c_str());
    md_kv("L2/L3 Cache", cache_sz.c_str());
    md_kv("RAM", mem_line);
    md_kv("Kernel", kver);
    md_kv_s("Compiler", compiler_id());
    md_kv("Build Type", "Release (-O2)");
    md_kv("sizeof(RuntimeNote)", ([]() -> std::string {
        char b[32];
        snprintf(b, sizeof(b), "%zu bytes", sizeof(phic::RuntimeNote));
        return b; })().c_str());
    std::printf("\n");

    // =========================================================================
    // SECTION A — Engine step() Throughput
    // =========================================================================
    md_hr();
    md_h2("Section A \xe2\x80\x94 Engine step() Throughput");
    md_note("3000 steps/scenario (500 for extreme). Timing in nanoseconds per step. "
            "All scenarios use autoplay=true unless noted.");

    std::vector<BenchResult> sec_a;

    // A1. Baseline — light chart (minimum requirement: 500 notes, 20 lines)
    { phic::RenderConfig cfg; cfg.autoplay = true;
      sec_a.push_back(run_bench("light_500", 20, 25, cfg, 3000)); }

    // A2. Medium chart — 5K notes / 25 lines
    { phic::RenderConfig cfg; cfg.autoplay = true;
      sec_a.push_back(run_bench("medium_5k", 25, 200, cfg, 3000)); }

    // A3. Heavy — 50K notes / 50 lines
    { phic::RenderConfig cfg; cfg.autoplay = true;
      sec_a.push_back(run_bench("heavy_50k", 50, 1000, cfg, 1000)); }

    // A4. Very heavy — 200K notes / 100 lines
    { phic::RenderConfig cfg; cfg.autoplay = true;
      sec_a.push_back(run_bench("very_heavy_200k", 100, 2000, cfg, 500)); }

    // A5. Extreme — 1M notes / 200 lines
    { phic::RenderConfig cfg; cfg.autoplay = true;
      sec_a.push_back(run_bench("extreme_1m", 200, 5000, cfg, 200)); }

    // A6. Hold-heavy — 5K hold notes, 20 lines
    { phic::RenderConfig cfg; cfg.autoplay = true;
      sec_a.push_back(run_bench("hold_heavy_5k", 20, 250, cfg, 3000, phic::NoteKind::Hold)); }

    // A7. Flick-heavy — 5K flick notes, 20 lines
    { phic::RenderConfig cfg; cfg.autoplay = true;
      sec_a.push_back(run_bench("flick_heavy_5k", 20, 250, cfg, 3000, phic::NoteKind::Flick)); }

    // A8. Mixed kinds — 10K notes, 40 lines
    { phic::RenderConfig cfg; cfg.autoplay = true;
      sec_a.push_back(run_bench_mixed("mixed_10k", 40, 250, cfg, 3000)); }

    // A9. Mirror mod — 5K notes, 20 lines
    { phic::RenderConfig cfg; cfg.autoplay = true; cfg.mods.mirror = true;
      sec_a.push_back(run_bench("mod_mirror_5k", 20, 250, cfg, 3000)); }

    // A10. Wave mod — 5K notes, 20 lines
    { phic::RenderConfig cfg; cfg.autoplay = true;
      cfg.mods.wave = true; cfg.mods.wave_amplitude_lane = 1.5; cfg.mods.wave_period_sec = 1.0;
      sec_a.push_back(run_bench("mod_wave_5k", 20, 250, cfg, 3000)); }

    // A11. Stutter x3 mod — 5K notes, 20 lines
    { phic::RenderConfig cfg; cfg.autoplay = true;
      cfg.mods.stutter = true; cfg.mods.stutter_repeat = 3; cfg.mods.stutter_interval_sec = 0.04;
      sec_a.push_back(run_bench("mod_stutter3_5k", 20, 250, cfg, 3000)); }

    // A12. Quantize mod
    { phic::RenderConfig cfg; cfg.autoplay = true;
      cfg.mods.quantize = true; cfg.mods.quantize_step_sec = 0.05;
      sec_a.push_back(run_bench("mod_quantize_5k", 20, 250, cfg, 3000)); }

    // A13. Reverse time mod
    { phic::RenderConfig cfg; cfg.autoplay = true; cfg.mods.reverse_time = true;
      sec_a.push_back(run_bench("mod_reverse_5k", 20, 250, cfg, 3000)); }

    // A14. Full blue mod
    { phic::RenderConfig cfg; cfg.autoplay = true; cfg.mods.full_blue = true;
      sec_a.push_back(run_bench("mod_fullblue_5k", 20, 250, cfg, 3000)); }

    // A15. Fade mod
    { phic::RenderConfig cfg; cfg.autoplay = true;
      cfg.mods.fade_enable = true;
      cfg.mods.fade_has_time_start = true; cfg.mods.fade_time_start = 0.0;
      cfg.mods.fade_has_time_end   = true; cfg.mods.fade_time_end   = 10.0;
      cfg.mods.fade_alpha_start = 0.1; cfg.mods.fade_alpha_end = 1.0;
      sec_a.push_back(run_bench("mod_fade_5k", 20, 250, cfg, 3000)); }

    // A16. Lane scale mod
    { phic::RenderConfig cfg; cfg.autoplay = true;
      cfg.mods.lane_scale = 1.5; cfg.mods.lane_scale_center = 3.5;
      sec_a.push_back(run_bench("mod_lanescale_5k", 20, 250, cfg, 3000)); }

    // A17. Thin-out x2
    { phic::RenderConfig cfg; cfg.autoplay = true; cfg.mods.thin_out_every = 2;
      sec_a.push_back(run_bench("mod_thinout_5k", 20, 500, cfg, 3000)); }

    // A18. Note rules (Hold alpha override)
    { phic::RenderConfig cfg; cfg.autoplay = true;
      phic::ModConfig::NoteRule rule;
      rule.filter.active = true; rule.filter.kinds = {phic::NoteKind::Hold};
      rule.set.has_alpha = true; rule.set.alpha01 = 0.5;
      cfg.mods.note_rules.push_back(rule);
      sec_a.push_back(run_bench_mixed("mod_rules_5k", 25, 200, cfg, 3000)); }

    // A19. All mods combined — 10K notes, 50 lines
    { phic::RenderConfig cfg; cfg.autoplay = true;
      cfg.mods.mirror = true;
      cfg.mods.wave = true; cfg.mods.wave_amplitude_lane = 0.5; cfg.mods.wave_period_sec = 1.0;
      cfg.mods.stutter = true; cfg.mods.stutter_repeat = 2; cfg.mods.stutter_interval_sec = 0.05;
      cfg.mods.quantize = true; cfg.mods.quantize_step_sec = 0.05;
      cfg.mods.full_blue = true; cfg.mods.stretch_factor = 1.5; cfg.mods.lane_scale = 1.2;
      cfg.mods.fade_enable = true;
      cfg.mods.fade_has_time_start = true; cfg.mods.fade_time_start = 0.0;
      cfg.mods.fade_has_time_end   = true; cfg.mods.fade_time_end   = 15.0;
      sec_a.push_back(run_bench("mod_all_10k", 50, 200, cfg, 1000)); }

    // A20. Seek interleaved — 5K notes
    sec_a.push_back(run_bench_seek("seek_5k", 20, 250, 2000));

    // A21. Input events (non-autoplay, 2000 events/step, 500 notes min)
    { phic::RenderConfig cfg; cfg.autoplay = false;
      phic::Engine engine(cfg);
      engine.load_chart(make_chart(20, 25));  // 500 notes
      for (int i = 0; i < 20; ++i) engine.step(1.0/60.0, {});
      engine.reset();
      const int step_count = 3000;
      const int ev_per_step = 2000 / step_count + 1;  // ~1 event/step baseline
      std::vector<phic::InputEvent> evs(ev_per_step);
      for (auto& e : evs) { e.type = phic::InputEvent::Type::PointerDown; e.lane = 0; }
      std::vector<double> samp(step_count);
      for (int i = 0; i < step_count; ++i) {
          for (auto& e : evs) { e.lane = i % 8; e.event_time = i / 60.0; }
          samp[i] = time_step_ns(engine, 1.0/60.0, evs);
      }
      BenchResult r{"input_2k_events", 500, 20, step_count, compute_stats(samp)};
      sec_a.push_back(r);
    }

    // --- Print Section A ---
    md_h3("A1. Timing Statistics (ns per step)");
    std::printf("| %-22s | %8s | %5s | %6s | %9s | %9s | %9s | %9s | %9s | %9s | %11s |\n",
                "Scenario","Notes","Lines","Steps","Mean","p50","p95","p99","Min","Max","Stddev");
    std::printf("|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|\n",
                ":-----------------------","--------:","-----:","------:",
                "---------:","---------:","---------:","---------:",
                "---------:","---------:","----------:");
    for (const auto& r : sec_a) {
        const auto& t = r.timing;
        std::printf("| %-22s | %8d | %5d | %6d | %9.1f | %9.1f | %9.1f | %9.1f | %9.1f | %9.1f | %11.1f |\n",
                    r.name, r.note_count, r.line_count, r.step_count,
                    t.mean_ns, t.median_ns, t.p95_ns, t.p99_ns,
                    t.min_ns, t.max_ns, t.stddev_ns);
    }
    std::printf("\n");

    md_h3("A2. Throughput Summary (steps/sec based on mean)");
    std::printf("| %-22s | %8s | %5s | %14s |\n",
                "Scenario","Notes","Lines","steps/sec");
    std::printf("|%s|%s|%s|%s|\n",
                ":-----------------------","--------:","-----:","-------------:");
    for (const auto& r : sec_a) {
        std::printf("| %-22s | %8d | %5d | %14.0f |\n",
                    r.name, r.note_count, r.line_count,
                    1e9 / r.timing.mean_ns);
    }
    std::printf("\n");

    // =========================================================================
    // SECTION B — Loading Cost & Memory
    // =========================================================================
    md_hr();
    md_h2("Section B \xe2\x80\x94 Loading Cost & Memory");
    md_note("Measures `load_chart()` wall-clock time (includes `apply_mods()` + engine reset). "
            "`data_bytes` = analytical estimate of chart+engine data structures. "
            "`rss_delta` = process RSS change (Linux `/proc/self/status`).");

    std::vector<LoadingResult> sec_b;

    // Noop mods across note counts
    { phic::ModConfig m; sec_b.push_back(run_loading_bench("noop_500",   20,   25, m, 200)); }
    { phic::ModConfig m; sec_b.push_back(run_loading_bench("noop_2k",    20,  100, m, 200)); }
    { phic::ModConfig m; sec_b.push_back(run_loading_bench("noop_10k",   25,  400, m, 200)); }
    { phic::ModConfig m; sec_b.push_back(run_loading_bench("noop_50k",   50, 1000, m,  50)); }
    { phic::ModConfig m; sec_b.push_back(run_loading_bench("noop_200k", 100, 2000, m,  20)); }
    { phic::ModConfig m; sec_b.push_back(run_loading_bench("noop_1m",   200, 5000, m,   5)); }

    // Wave+quantize at key sizes
    { phic::ModConfig m;
      m.wave = true; m.wave_amplitude_lane = 1.0; m.wave_period_sec = 2.0;
      m.quantize = true; m.quantize_step_sec = 0.05;
      sec_b.push_back(run_loading_bench("wave+q_10k",  25,  400, m, 100)); }
    { phic::ModConfig m;
      m.wave = true; m.wave_amplitude_lane = 1.0; m.wave_period_sec = 2.0;
      m.quantize = true; m.quantize_step_sec = 0.05;
      sec_b.push_back(run_loading_bench("wave+q_200k", 100, 2000, m,  20)); }

    // Stutter x4 (note multiplier)
    { phic::ModConfig m;
      m.stutter = true; m.stutter_repeat = 4; m.stutter_interval_sec = 0.02;
      sec_b.push_back(run_loading_bench("stutter4_10k",  25,  400, m, 100)); }
    { phic::ModConfig m;
      m.stutter = true; m.stutter_repeat = 4; m.stutter_interval_sec = 0.02;
      sec_b.push_back(run_loading_bench("stutter4_50k",  50, 1000, m,  30)); }

    // Print Section B
    std::printf("| %-18s | %8s | %8s | %5s | %12s | %12s | %12s | %12s | %14s |\n",
                "Scenario","notes_in","notes_out","lines","mean_ns","p50_ns","p95_ns","p99_ns","data_bytes");
    std::printf("|%s|%s|%s|%s|%s|%s|%s|%s|%s|\n",
                ":-----------------","--------:","--------:","-----:",
                "-----------:","-----------:","-----------:","-----------:","-------------:");
    for (const auto& r : sec_b) {
        char db[32];
        if (r.data_bytes > 1048576)
            snprintf(db, sizeof(db), "%.1f MiB", r.data_bytes / 1048576.0);
        else if (r.data_bytes > 1024)
            snprintf(db, sizeof(db), "%.1f KiB", r.data_bytes / 1024.0);
        else
            snprintf(db, sizeof(db), "%zu B", r.data_bytes);
        const auto& t = r.timing;
        std::printf("| %-18s | %8d | %8d | %5d | %12.1f | %12.1f | %12.1f | %12.1f | %14s |\n",
                    r.name, r.note_count_in, r.note_count_out, r.line_count,
                    t.mean_ns, t.median_ns, t.p95_ns, t.p99_ns, db);
    }
    std::printf("\n");

    // =========================================================================
    // SECTION C — Event Processing Throughput
    // =========================================================================
    md_hr();
    md_h2("Section C \xe2\x80\x94 Event Processing Throughput");
    md_note("Non-autoplay engine. Events are `PointerDown` at cycling lanes. "
            "Chart: 5K notes / 20 lines (dense). 2000 steps per run. "
            "Extreme row: 50K events/step \xc3\x97 2000 steps = 1\xc3\x97"
            "10\xe2\x81\xb8 events total.");

    std::vector<EventsResult> sec_c;

    // Vary events per step; chart: 20 lines × 250 notes = 5K notes
    for (auto [label, eps] : std::vector<std::pair<const char*, int>>{
            {"1_evt/step",     1},
            {"8_evt/step",     8},
            {"64_evt/step",   64},
            {"512_evt/step",  512},
            {"2k_evt/step",  2000},
            {"8k_evt/step",  8000},
            {"32k_evt/step", 32000},
            {"50k_evt/step", 50000},
        }) {
        sec_c.push_back(run_events_bench(label, 20, 250, eps, 2000));
    }

    // Extreme: 200K notes (200 lines), heavy event stream
    for (auto [label, eps] : std::vector<std::pair<const char*, int>>{
            {"50k_evt+200kN", 50000},
        }) {
        sec_c.push_back(run_events_bench(label, 100, 2000, eps, 2000));
    }

    std::printf("| %-20s | %8s | %12s | %12s | %12s | %12s | %12s | %14s |\n",
                "Scenario","notes","evts/step","total_evts","mean_ns","p95_ns","p99_ns","ns/event");
    std::printf("|%s|%s|%s|%s|%s|%s|%s|%s|\n",
                ":--------------------","-------:","----------:","----------:",
                "---------:","---------:","---------:","-------------:");
    for (const auto& r : sec_c) {
        char tevts[32];
        if (r.total_events >= 1000000000LL)
            snprintf(tevts, sizeof(tevts), "%.1fB", r.total_events / 1e9);
        else if (r.total_events >= 1000000LL)
            snprintf(tevts, sizeof(tevts), "%.1fM", r.total_events / 1e6);
        else
            snprintf(tevts, sizeof(tevts), "%lldK", r.total_events / 1000);
        std::printf("| %-20s | %8d | %12d | %12s | %12.1f | %12.1f | %12.1f | %14.3f |\n",
                    r.name, r.notes, r.events_per_step, tevts,
                    r.timing.mean_ns, r.timing.p95_ns, r.timing.p99_ns, r.ns_per_event);
    }
    std::printf("\n");

    // =========================================================================
    // SECTION D — Rendering Output Analysis
    // =========================================================================
    md_hr();
    md_h2("Section D \xe2\x80\x94 Rendering Output Analysis");
    md_note("Runs full chart to completion (or 12 000-step cap). "
            "All scenarios use autoplay=true. Note interval: 0.05 s.");

    std::vector<RenderBenchResult> sec_d;

    // D1–D3: approach window variants (5K notes)
    { phic::RenderConfig c; c.autoplay = true; c.approach_sec = 1.0;
      sec_d.push_back(run_render_bench("approach_1s",  20, 250, c)); }
    { phic::RenderConfig c; c.autoplay = true; c.approach_sec = 3.0;
      sec_d.push_back(run_render_bench("approach_3s",  20, 250, c)); }
    { phic::RenderConfig c; c.autoplay = true; c.approach_sec = 5.0;
      sec_d.push_back(run_render_bench("approach_5s",  20, 250, c)); }

    // D4: no_cull — all notes emitted every frame
    { phic::RenderConfig c; c.autoplay = true; c.no_cull = true;
      sec_d.push_back(run_render_bench("no_cull",       20, 250, c)); }

    // D5: mixed kinds
    { phic::RenderConfig c; c.autoplay = true;
      sec_d.push_back(run_render_bench("mixed_kinds",   20, 250, c,
                      phic::NoteKind::Tap, /*mixed=*/true)); }

    // D6: holds only
    { phic::RenderConfig c; c.autoplay = true;
      sec_d.push_back(run_render_bench("holds_only",    20, 250, c,
                      phic::NoteKind::Hold)); }

    // D7: wave mod — cmd positions shift per step
    { phic::RenderConfig c; c.autoplay = true;
      c.mods.wave = true; c.mods.wave_amplitude_lane = 2.0; c.mods.wave_period_sec = 1.0;
      sec_d.push_back(run_render_bench("wave_mod",      20, 250, c)); }

    // D8: fade mod — alpha decay visible in avg_alpha
    { phic::RenderConfig c; c.autoplay = true;
      c.mods.fade_enable = true;
      c.mods.fade_has_time_start = true; c.mods.fade_time_start = 0.0;
      c.mods.fade_has_time_end   = true; c.mods.fade_time_end   = 6.0;
      c.mods.fade_alpha_start = 0.1; c.mods.fade_alpha_end = 1.0;
      sec_d.push_back(run_render_bench("fade_mod",      20, 250, c)); }

    // D9: stutter x3
    { phic::RenderConfig c; c.autoplay = true;
      c.mods.stutter = true; c.mods.stutter_repeat = 3; c.mods.stutter_interval_sec = 0.04;
      sec_d.push_back(run_render_bench("stutter_x3",    10, 100, c)); }

    // D10: dense chart 20K notes
    { phic::RenderConfig c; c.autoplay = true;
      sec_d.push_back(run_render_bench("dense_20k",     50, 400, c)); }

    // D11: heavy chart 50K notes
    { phic::RenderConfig c; c.autoplay = true;
      sec_d.push_back(run_render_bench("heavy_50k",     50, 1000, c)); }

    // Sub-table D1: frame command statistics
    md_h3("D1. Frame Command Statistics");
    std::printf("| %-18s | %6s | %5s | %9s | %9s | %9s | %8s | %8s |\n",
                "Scenario","notes","steps","avg_cmds","peak_cmds","stddev","vis%%","mean_ns");
    std::printf("|%s|%s|%s|%s|%s|%s|%s|%s|\n",
                ":-----------------","-----:","----:",
                "--------:","--------:","--------:","------:","-------:");
    for (const auto& r : sec_d) {
        std::printf("| %-18s | %6d | %5d | %9.1f | %9.0f | %9.1f | %7.1f%% | %8.1f |\n",
                    r.name, r.note_count, r.total_steps,
                    r.avg_cmds_per_step, r.peak_cmds_per_step, r.cmd_stddev,
                    r.visibility_ratio * 100.0, r.step_timing.mean_ns);
    }
    std::printf("\n");

    // Sub-table D2: note kind distribution
    md_h3("D2. Note Kind Distribution & Alpha");
    std::printf("| %-18s | %9s | %9s | %9s | %9s | %10s |\n",
                "Scenario","tap %%","drag %%","hold %%","flick %%","avg_alpha");
    std::printf("|%s|%s|%s|%s|%s|%s|\n",
                ":-----------------","---------:","---------:",
                "---------:","---------:","---------:");
    for (const auto& r : sec_d) {
        std::printf("| %-18s | %8.1f%% | %8.1f%% | %8.1f%% | %8.1f%% | %10.3f |\n",
                    r.name,
                    r.frac_tap * 100.0, r.frac_drag * 100.0,
                    r.frac_hold * 100.0, r.frac_flick * 100.0,
                    r.avg_alpha);
    }
    std::printf("\n");

    // Sub-table D3: judge breakdown
    md_h3("D3. Judge Breakdown & Accuracy");
    std::printf("| %-18s | %7s | %7s | %5s | %5s | %5s | %9s | %9s |\n",
                "Scenario","total","perfect","good","bad","miss","accuracy","maxcombo");
    std::printf("|%s|%s|%s|%s|%s|%s|%s|%s|\n",
                ":-----------------","------:","------:","----:","----:","----:","-------:","-------:");
    for (const auto& r : sec_d) {
        const int tot = r.judge_perfect + r.judge_good + r.judge_bad + r.judge_miss;
        std::printf("| %-18s | %7d | %7d | %5d | %5d | %5d | %8.2f%% | %9d |\n",
                    r.name, tot, r.judge_perfect, r.judge_good,
                    r.judge_bad, r.judge_miss,
                    r.final_accuracy * 100.0, r.final_max_combo);
    }
    std::printf("\n");

    // Sub-table D4: step timing statistics
    md_h3("D4. Render Step Timing Statistics (ns)");
    std::printf("| %-18s | %9s | %9s | %9s | %9s | %9s | %9s | %10s |\n",
                "Scenario","mean","p50","p95","p99","min","max","stddev");
    std::printf("|%s|%s|%s|%s|%s|%s|%s|%s|\n",
                ":-----------------","---------:","---------:","---------:",
                "---------:","---------:","---------:","----------:");
    for (const auto& r : sec_d) {
        const auto& t = r.step_timing;
        std::printf("| %-18s | %9.1f | %9.1f | %9.1f | %9.1f | %9.1f | %9.1f | %10.1f |\n",
                    r.name, t.mean_ns, t.median_ns, t.p95_ns, t.p99_ns,
                    t.min_ns, t.max_ns, t.stddev_ns);
    }
    std::printf("\n");

    // =========================================================================
    // SECTION E — apply_mods() Preprocessing Cost
    // =========================================================================
    md_hr();
    md_h2("Section E \xe2\x80\x94 apply\\_mods() Preprocessing Cost");
    md_note("Each call reconstructs the chart from scratch, then times `apply_mods()` alone. "
            "`notes_out` vs `notes_in` shows note multiplication from stutter.");

    std::vector<ApplyModsResult> sec_e;

    // Noop — scaling
    { phic::ModConfig m; sec_e.push_back(run_bench_apply_mods("noop_500",    20,   25, m, 500)); }
    { phic::ModConfig m; sec_e.push_back(run_bench_apply_mods("noop_5k",     20,  250, m, 500)); }
    { phic::ModConfig m; sec_e.push_back(run_bench_apply_mods("noop_50k",    50, 1000, m, 200)); }
    { phic::ModConfig m; sec_e.push_back(run_bench_apply_mods("noop_500k",  100, 5000, m,  50)); }
    { phic::ModConfig m; sec_e.push_back(run_bench_apply_mods("noop_1m",    200, 5000, m,  20)); }

    // Mirror
    { phic::ModConfig m; m.mirror = true;
      sec_e.push_back(run_bench_apply_mods("mirror_5k",  20, 250, m, 300)); }
    // Wave+quantize
    { phic::ModConfig m;
      m.wave = true; m.wave_amplitude_lane = 1.0; m.wave_period_sec = 2.0;
      m.quantize = true; m.quantize_step_sec = 0.05;
      sec_e.push_back(run_bench_apply_mods("wave+q_5k",  20, 250, m, 300)); }
    { phic::ModConfig m;
      m.wave = true; m.wave_amplitude_lane = 1.0; m.wave_period_sec = 2.0;
      m.quantize = true; m.quantize_step_sec = 0.05;
      sec_e.push_back(run_bench_apply_mods("wave+q_50k", 50, 1000, m, 100)); }
    // Stutter x4
    { phic::ModConfig m;
      m.stutter = true; m.stutter_repeat = 4; m.stutter_interval_sec = 0.02;
      sec_e.push_back(run_bench_apply_mods("stutter4_5k",   20,  250, m, 200)); }
    { phic::ModConfig m;
      m.stutter = true; m.stutter_repeat = 4; m.stutter_interval_sec = 0.02;
      sec_e.push_back(run_bench_apply_mods("stutter4_50k",  50, 1000, m,  50)); }
    // Reverse time
    { phic::ModConfig m; m.reverse_time = true;
      sec_e.push_back(run_bench_apply_mods("reverse_5k",    20, 250, m, 300)); }
    // Fade
    { phic::ModConfig m;
      m.fade_enable = true;
      m.fade_has_time_start = true; m.fade_time_start = 0.0;
      m.fade_has_time_end   = true; m.fade_time_end   = 12.0;
      sec_e.push_back(run_bench_apply_mods("fade_5k",       20, 250, m, 300)); }
    // Note rules
    { phic::ModConfig m;
      phic::ModConfig::NoteRule rule;
      rule.filter.active = true;
      rule.filter.kinds  = {phic::NoteKind::Tap, phic::NoteKind::Hold};
      rule.set.has_alpha = true; rule.set.alpha01 = 0.7;
      m.note_rules.push_back(rule);
      sec_e.push_back(run_bench_apply_mods("note_rules_5k", 20, 250, m, 300)); }
    // All mods combined
    { phic::ModConfig m;
      m.mirror = true; m.full_blue = true;
      m.wave = true; m.wave_amplitude_lane = 0.5; m.wave_period_sec = 1.0;
      m.stutter = true; m.stutter_repeat = 2; m.stutter_interval_sec = 0.05;
      m.quantize = true; m.quantize_step_sec = 0.05;
      m.stretch_factor = 1.5; m.lane_scale = 1.2;
      m.fade_enable = true;
      sec_e.push_back(run_bench_apply_mods("all_mods_5k",   20,  250, m, 200)); }
    { phic::ModConfig m;
      m.mirror = true; m.full_blue = true;
      m.wave = true; m.wave_amplitude_lane = 0.5; m.wave_period_sec = 1.0;
      m.stutter = true; m.stutter_repeat = 2; m.stutter_interval_sec = 0.05;
      m.quantize = true; m.quantize_step_sec = 0.05;
      m.stretch_factor = 1.5; m.lane_scale = 1.2;
      m.fade_enable = true;
      sec_e.push_back(run_bench_apply_mods("all_mods_50k",  50, 1000, m,  50)); }

    std::printf("| %-22s | %8s | %8s | %12s | %12s | %12s | %12s | %12s |\n",
                "Scenario","notes_in","notes_out","mean_ns","p50_ns","p95_ns","p99_ns","stddev_ns");
    std::printf("|%s|%s|%s|%s|%s|%s|%s|%s|\n",
                ":---------------------","-------:","-------:",
                "-----------:","-----------:","-----------:","-----------:","-----------:");
    for (const auto& r : sec_e) {
        const auto& t = r.timing;
        std::printf("| %-22s | %8d | %8d | %12.1f | %12.1f | %12.1f | %12.1f | %12.1f |\n",
                    r.name, r.note_count_in, r.note_count_out,
                    t.mean_ns, t.median_ns, t.p95_ns, t.p99_ns, t.stddev_ns);
    }
    std::printf("\n");

    md_hr();
    std::printf("*Report generated by `phic_bench_perf`. "
                "All timings wall-clock on a single thread.*\n");

    // =========================================================================
    // SECTION F — Line Event Evaluation Cost
    // =========================================================================
    md_hr();
    md_h2("Section F \xe2\x80\x94 Line Event Evaluation Cost");
    md_note("Measures `step()` cost with synthetic line animation tracks. "
            "Each line has pos\\_x, pos\\_y, rot, alpha (EasedTrack layers) + scroll IntegralTrack. "
            "`segs/ch` = segments per channel; `layers` = RPE-style event-layer count. "
            "`ns/line` = mean step time ÷ line count (amortised per-line eval cost).");

    std::vector<LineAnimResult> sec_f;

    // ── Baseline: no line events (0 segs) ──────────────────────────────────
    sec_f.push_back(run_line_anim_bench("no_events_20l",     20,   500,   0,  0,  1, 3000));
    sec_f.push_back(run_line_anim_bench("no_events_200l",   200,    50,   0,  0,  1, 3000));

    // ── Sparse events: 10 segs/channel, 1 layer ─────────────────────────────
    sec_f.push_back(run_line_anim_bench("sparse_1l_10s",      1,  5000,  10, 10,  1, 3000));
    sec_f.push_back(run_line_anim_bench("sparse_20l_10s",    20,   500,  10, 10,  1, 3000));
    sec_f.push_back(run_line_anim_bench("sparse_200l_10s",  200,    50,  10, 10,  1, 2000));

    // ── Medium events: 100 segs/channel, 1 layer ────────────────────────────
    sec_f.push_back(run_line_anim_bench("medium_1l_100s",     1,  5000, 100, 50,  1, 3000));
    sec_f.push_back(run_line_anim_bench("medium_20l_100s",   20,   500, 100, 50,  1, 3000));
    sec_f.push_back(run_line_anim_bench("medium_200l_100s", 200,    50, 100, 50,  1, 2000));

    // ── Dense events: 1000 segs/channel, 1 layer ────────────────────────────
    sec_f.push_back(run_line_anim_bench("dense_1l_1000s",     1,  5000,1000,100,  1, 2000));
    sec_f.push_back(run_line_anim_bench("dense_20l_1000s",   20,   500,1000,100,  1, 2000));
    sec_f.push_back(run_line_anim_bench("dense_200l_1000s", 200,    50,1000,100,  1, 1000));

    // ── RPE multi-layer: 3 layers × 100 segs/channel ────────────────────────
    sec_f.push_back(run_line_anim_bench("rpe_3lay_20l_100s",  20,  500, 100, 50,  3, 3000));
    sec_f.push_back(run_line_anim_bench("rpe_3lay_200l_100s",200,   50, 100, 50,  3, 2000));

    // ── Extreme: 5 layers × 1000 segs/channel × 200 lines ───────────────────
    sec_f.push_back(run_line_anim_bench("extreme_200l_1000s",200,   50,1000,200,  5,  500));

    // ── Seek pattern (tests cursor reset overhead) ────────────────────────────
    {   // seek version: reset cursors every step
        const char* name = "seek_20l_1000s";
        phic::RenderConfig cfg; cfg.autoplay = true;
        phic::Engine engine(cfg);
        engine.load_chart(make_chart_with_anims(20, 500, 1000, 100, 1));
        const int kSteps = 2000;
        std::vector<double> samps(kSteps);
        for (int i = 0; i < kSteps; ++i) {
            engine.seek(0.0);
            samps[i] = time_step_ns(engine, 1.0 / 60.0, {});
        }
        BenchStats st = compute_stats(samps);
        LineAnimResult r;
        r.name             = name;
        r.line_count       = 20;
        r.segs_per_channel = 1000;
        r.layers           = 1;
        r.total_event_segs = 1 * 1000 * 4 + 100;
        r.step_timing      = st;
        r.ns_per_line_step = st.mean_ns / 20;
        sec_f.push_back(r);
    }

    std::printf("| %-24s | %5s | %7s | %6s | %12s | %12s | %12s | %12s | %10s |\n",
                "Scenario","lines","segs/ch","layers","mean_ns","p50_ns","p95_ns","p99_ns","ns/line");
    std::printf("|%s|%s|%s|%s|%s|%s|%s|%s|%s|\n",
                ":-----------------------","----:","------:","-----:",
                "-----------:","-----------:","-----------:","-----------:","---------:");
    for (const auto& r : sec_f) {
        const auto& t = r.step_timing;
        std::printf("| %-24s | %5d | %7d | %6d | %12.1f | %12.1f | %12.1f | %12.1f | %10.1f |\n",
                    r.name, r.line_count, r.segs_per_channel, r.layers,
                    t.mean_ns, t.median_ns, t.p95_ns, t.p99_ns, r.ns_per_line_step);
    }
    std::printf("\n");

    md_hr();
    std::printf("*Report generated by `phic_bench_perf`. "
                "All timings wall-clock on a single thread.*\n");
    return 0;
}
