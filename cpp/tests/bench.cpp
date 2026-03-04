// bench.cpp — Phigros Renderer comprehensive benchmark suite
//
// Covers:
//   1. Chart inventory (file size, complexity)
//   2. Parser throughput (load time, MB/s)
//   3. Engine simulation (full loop realtime multiplier)
//   4. build_frame (render snapshot, μs/frame)
//   5. Kinematics (eval_line_state calls/sec)
//   6. Mod application (μs per op type)
//   7. Chart compiler (ms, KB output at multiple Hz)
//   8. PHBC binary I/O (write + read time, speedup vs source)
//   9. Memory footprint (struct sizes, chart heap estimate)
//  10. Track evaluation (piecewise vs SampledTrack latency)
//
// Usage:
//   ./bench <charts_dir> [--out BENCHMARKS.md]
//
// Output: formatted tables to stdout + markdown file.

#include "phigros/chart/parser.hpp"
#include "phigros/chart/compiler.hpp"
#include "phigros/chart/compiled_chart.hpp"
#include "phigros/chart/phbc_io.hpp"
#include "phigros/chart/sampled_track.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/visibility.hpp"
#include "phigros/engine/note_manager.hpp"
#include "phigros/engine/hold_logic.hpp"
#include "phigros/engine/simulateplay.hpp"
#include "phigros/engine/effects.hpp"
#include "phigros/core/mods.hpp"
#include "phigros/core/mod_loader.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/render/renderer.hpp"
#include "phigros/math/tracks.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace phigros;
using json = nlohmann::json;
using SC = std::chrono::steady_clock;

// ─────────────────────────────────────────────────────────────────────────────
// Timing helpers
// ─────────────────────────────────────────────────────────────────────────────

struct BenchResult {
    double min_ns, mean_ns, max_ns, p95_ns, stddev_ns;
    int    iters;
};

template<typename F>
BenchResult bench(F&& fn, int warmup = 3, int iters = 10) {
    for (int i = 0; i < warmup; ++i) fn();

    std::vector<double> samples;
    samples.reserve(iters);
    for (int i = 0; i < iters; ++i) {
        auto t0 = SC::now();
        fn();
        auto t1 = SC::now();
        samples.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }

    std::sort(samples.begin(), samples.end());
    double mn  = samples.front();
    double mx  = samples.back();
    double p95 = samples[static_cast<size_t>(iters * 0.95)];
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    double mean = sum / iters;
    double var  = 0;
    for (double s : samples) var += (s - mean) * (s - mean);
    return { mn, mean, mx, p95, std::sqrt(var / iters), iters };
}

static std::string fmt_ns(double ns) {
    std::ostringstream o;
    if      (ns < 1'000)          { o << std::fixed << std::setprecision(1) << ns    << " ns"; }
    else if (ns < 1'000'000)      { o << std::fixed << std::setprecision(2) << ns/1e3 << " μs"; }
    else if (ns < 1'000'000'000)  { o << std::fixed << std::setprecision(2) << ns/1e6 << " ms"; }
    else                          { o << std::fixed << std::setprecision(3) << ns/1e9 << " s";  }
    return o.str();
}

static std::string fmt_x(double x) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(0) << x << "×";
    return o.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Chart loading helpers (same pattern as test_engine.cpp)
// ─────────────────────────────────────────────────────────────────────────────

static std::string detect_format(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::string first;
    std::getline(f, first);
    if (!first.empty()) {
        char c = first[0];
        if (c == 'b' || c == 'c' || c == 'n' || c == '#' ||
            (c >= '0' && c <= '9')) return "pec";
    }
    f.seekg(0);
    try {
        json j = json::parse(f);
        if (j.contains("META")) return "rpe";
        return "official";
    } catch (...) {}
    return "pec";
}

static ChartData load_source(const std::string& path, int W = 1280, int H = 720) {
    std::string fmt = detect_format(path);
    if (fmt == "rpe" || fmt == "official") {
        std::ifstream f(path);
        json j = json::parse(f);
        config::RenderConfig cfg;
        if (fmt == "rpe") return chart::parse_rpe(j, W, H, cfg.rpe_easing_shift);
        return chart::parse_official(j, W, H);
    }
    return chart::parse_pec(path, W, H);
}

// ─────────────────────────────────────────────────────────────────────────────
// Discover charts
// ─────────────────────────────────────────────────────────────────────────────

struct ChartEntry {
    std::string name;   // display name (parent dir)
    std::string path;   // full path to .json
    std::string format;
    uintmax_t   file_bytes = 0;
};

static std::vector<ChartEntry> discover_charts(const std::string& dir) {
    std::vector<ChartEntry> out;
    try {
        for (const auto& e : fs::recursive_directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            auto ext = e.path().extension().string();
            if (ext != ".json" && ext != ".pec") continue;
            std::string fmt = detect_format(e.path().string());
            if (fmt.empty()) continue;
            ChartEntry ce;
            ce.path       = e.path().string();
            ce.format     = fmt;
            ce.file_bytes = fs::file_size(e.path());
            ce.name       = e.path().parent_path().filename().string()
                          + "/" + e.path().filename().string();
            out.push_back(ce);
        }
    } catch (...) {}
    std::sort(out.begin(), out.end(),
              [](const ChartEntry& a, const ChartEntry& b){ return a.name < b.name; });
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Markdown builder
// ─────────────────────────────────────────────────────────────────────────────

struct MD {
    std::ostringstream buf;

    void h1(const std::string& s)  { buf << "# "  << s << "\n\n"; }
    void h2(const std::string& s)  { buf << "## " << s << "\n\n"; }
    void h3(const std::string& s)  { buf << "### "<< s << "\n\n"; }
    void p(const std::string& s)   { buf << s << "\n\n"; }
    void note(const std::string& s){ buf << "> " << s << "\n\n"; }
    void hr()                       { buf << "---\n\n"; }

    // Table: header row + data rows (vector of vectors)
    void table(const std::vector<std::string>& hdr,
               const std::vector<std::vector<std::string>>& rows) {
        // Column widths
        std::vector<size_t> w(hdr.size(), 0);
        for (size_t c = 0; c < hdr.size(); ++c) w[c] = hdr[c].size();
        for (const auto& r : rows)
            for (size_t c = 0; c < r.size() && c < w.size(); ++c)
                w[c] = std::max(w[c], r[c].size());

        auto row_str = [&](const std::vector<std::string>& r) {
            buf << "|";
            for (size_t c = 0; c < hdr.size(); ++c) {
                std::string cell = (c < r.size()) ? r[c] : "";
                buf << " " << std::left << std::setw((int)w[c]) << cell << " |";
            }
            buf << "\n";
        };
        auto sep_row = [&]() {
            buf << "|";
            for (size_t c = 0; c < hdr.size(); ++c)
                buf << " " << std::string(w[c], '-') << " |";
            buf << "\n";
        };

        row_str(hdr);
        sep_row();
        for (const auto& r : rows) row_str(r);
        buf << "\n";
    }

    std::string str() const { return buf.str(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// System info
// ─────────────────────────────────────────────────────────────────────────────

static std::string get_cpu_model() {
    std::ifstream f("/proc/cpuinfo");
    if (f) {
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("model name", 0) == 0) {
                auto pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string s = line.substr(pos + 2);
                    while (!s.empty() && std::isspace(s.back())) s.pop_back();
                    return s;
                }
            }
        }
    }
    return "unknown";
}

static int get_nproc() {
    return (int)std::thread::hardware_concurrency();
}

static std::string get_os() {
#if defined(__linux__)
    return "Linux";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(_WIN32)
    return "Windows";
#else
    return "unknown";
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 1 — Chart inventory
// ─────────────────────────────────────────────────────────────────────────────

struct ChartStats {
    ChartEntry entry;
    int    lines = 0, notes = 0, holds = 0, fakes = 0;
    double duration = 0.0, offset = 0.0;
    size_t notes_heap_bytes = 0;   // sizeof(Note) * notes.size()
    size_t lines_heap_bytes = 0;   // sizeof(Line) * lines.size()
};

static ChartStats compute_stats(const ChartEntry& e) {
    ChartStats s;
    s.entry = e;
    ChartData chart = load_source(e.path);
    engine::precompute_t_enter(chart.lines, chart.notes, 1280, 720);
    s.lines    = (int)chart.lines.size();
    s.notes    = chart.playable_count;
    for (auto& n : chart.notes) {
        if (n.fake) ++s.fakes;
        if (!n.fake && n.kind == 3) ++s.holds;
    }
    s.duration = chart.chart_end_t - chart.offset;
    s.offset   = chart.offset;
    s.notes_heap_bytes = chart.notes.size() * sizeof(Note);
    s.lines_heap_bytes = chart.lines.size() * sizeof(Line);
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 2 — Parser throughput
// ─────────────────────────────────────────────────────────────────────────────

struct ParseBench {
    ChartEntry entry;
    BenchResult result;   // ns per parse
    double mb_per_s = 0;
};

static ParseBench bench_parse(const ChartEntry& e) {
    ParseBench pb;
    pb.entry  = e;
    pb.result = bench([&]{ load_source(e.path); }, 2, 8);
    double file_mb = e.file_bytes / 1e6;
    pb.mb_per_s = file_mb / (pb.result.mean_ns / 1e9);
    return pb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 3 — Engine simulation
// ─────────────────────────────────────────────────────────────────────────────

struct SimBench {
    std::string name;
    int    notes = 0;
    double duration = 0.0;
    double dt = 0.0;
    int    frames = 0;
    double total_ms = 0.0;
    double us_per_note = 0.0;
    double realtime_x = 0.0;
};

static SimBench bench_sim(const ChartEntry& e, double dt = 1.0/240.0) {
    ChartData chart = load_source(e.path);
    engine::precompute_t_enter(chart.lines, chart.notes, 1280, 720);

    int total_notes = chart.playable_count;
    double t_start  = chart.offset;
    double t_end    = chart.chart_end_t + 2.0;
    double dur      = t_end - t_start;

    SimBench sb;
    sb.name     = e.name;
    sb.notes    = total_notes;
    sb.duration = dur;
    sb.dt       = dt;

    auto run_sim = [&]() {
        std::vector<NoteState> s(chart.notes.size());
        for (size_t i = 0; i < s.size(); ++i) s[i].note = &chart.notes[i];
        engine::Judge          judge;
        engine::SimulatePlayer sim(engine::SimMode::Conservative);
        engine::NoteManager    note_mgr(&chart.notes, &s);
        int frame_count = 0;
        for (double t = t_start; t <= t_end; t += dt) {
            int idx = note_mgr.find_next_note_index(t);
            sim.step(t, chart.notes, s, chart.lines, judge, 1280, 720);
            engine::detect_misses(s, idx, t, engine::Judge::BAD, judge);
            engine::hold_maintenance(s, idx, t, 0.30, judge);
            engine::hold_finalize(s, idx, t, 0.30, engine::Judge::BAD, judge);
            ++frame_count;
        }
        sb.frames = frame_count;
    };

    run_sim(); // warmup

    auto t0 = SC::now();
    run_sim();
    auto t1 = SC::now();
    sb.total_ms   = std::chrono::duration<double, std::milli>(t1 - t0).count();
    sb.us_per_note = total_notes > 0 ? sb.total_ms * 1000.0 / total_notes : 0;
    sb.realtime_x  = sb.total_ms > 0 ? (dur * 1000.0) / sb.total_ms : 0;

    return sb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 4 — build_frame performance
// ─────────────────────────────────────────────────────────────────────────────

struct BuildFrameBench {
    std::string name;
    int    notes = 0, lines = 0;
    double us_mean = 0, us_min = 0, us_max = 0, us_p95 = 0;
    double realtime_x = 0;
    int    samples = 0;  // frames sampled
};

static BuildFrameBench bench_build_frame(const ChartEntry& e,
                                         double expand_factor = 1.0) {
    ChartData chart = load_source(e.path);
    engine::precompute_t_enter(chart.lines, chart.notes, 1280, 720, expand_factor);

    double t_start = chart.offset;
    double t_end   = chart.chart_end_t + 2.0;
    constexpr double SIM_DT = 1.0 / 240.0;

    config::RenderConfig cfg;
    cfg.window_w = 1280;
    cfg.window_h = 720;

    // Run full simulation, collecting build_frame timings
    std::vector<NoteState> states(chart.notes.size());
    for (size_t i = 0; i < states.size(); ++i) states[i].note = &chart.notes[i];
    engine::Judge          judge;
    engine::SimulatePlayer sim(engine::SimMode::Conservative);
    engine::NoteManager    note_mgr(&chart.notes, &states);

    std::vector<double> bf_ns;
    bf_ns.reserve(static_cast<size_t>((t_end - t_start) / SIM_DT) + 128);

    for (double t = t_start; t <= t_end; t += SIM_DT) {
        int idx = note_mgr.find_next_note_index(t);
        sim.step(t, chart.notes, states, chart.lines, judge, 1280, 720);
        engine::detect_misses(states, idx, t, engine::Judge::BAD, judge);
        engine::hold_maintenance(states, idx, t, 0.30, judge);
        engine::hold_finalize(states, idx, t, 0.30, engine::Judge::BAD, judge);

        auto ta = SC::now();
        auto fr = render::build_frame(t, chart, states, judge, cfg);
        auto tb = SC::now();
        bf_ns.push_back(
            std::chrono::duration<double, std::nano>(tb - ta).count());
        (void)fr;
    }

    if (bf_ns.empty()) { return {}; }
    std::sort(bf_ns.begin(), bf_ns.end());

    double sum  = std::accumulate(bf_ns.begin(), bf_ns.end(), 0.0);
    double mean = sum / bf_ns.size();
    double p95  = bf_ns[static_cast<size_t>(bf_ns.size() * 0.95)];

    BuildFrameBench b;
    b.name      = e.name;
    b.notes     = chart.playable_count;
    b.lines     = (int)chart.lines.size();
    b.us_mean   = mean / 1000.0;
    b.us_min    = bf_ns.front() / 1000.0;
    b.us_max    = bf_ns.back() / 1000.0;
    b.us_p95    = p95 / 1000.0;
    b.samples   = (int)bf_ns.size();
    // Realtime: how many times faster than a 240fps render loop
    double budget_ns = 1e9 / 240.0;   // ns per frame at 240fps
    b.realtime_x = budget_ns / mean;

    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 5 — Kinematics (eval_line_state)
// ─────────────────────────────────────────────────────────────────────────────

struct KinematicsBench {
    std::string name;
    int    lines = 0;
    double ns_per_call = 0;
    double million_calls_per_s = 0;
};

static KinematicsBench bench_kinematics(const ChartEntry& e) {
    ChartData chart = load_source(e.path);
    engine::precompute_t_enter(chart.lines, chart.notes, 1280, 720);

    int nlines = (int)chart.lines.size();
    double t_mid = chart.offset + (chart.chart_end_t - chart.offset) * 0.5;

    // Measure a tight loop of eval_line_state for all lines
    constexpr int ITERS = 2000;
    volatile double sink = 0;

    auto r = bench([&]{
        for (int i = 0; i < ITERS; ++i) {
            double t = t_mid + i * (1.0 / 240.0);
            for (const auto& ln : chart.lines) {
                auto ls = engine::eval_line_state(ln, t);
                sink += ls.x + ls.y + ls.rot;
            }
        }
    }, 3, 8);
    (void)sink;

    double total_calls = (double)ITERS * nlines;
    double ns_per_call = r.mean_ns / total_calls;

    KinematicsBench kb;
    kb.name              = e.name;
    kb.lines             = nlines;
    kb.ns_per_call       = ns_per_call;
    kb.million_calls_per_s = (ns_per_call > 0) ? 1e3 / ns_per_call : 0;
    return kb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 6 — Mod application
// ─────────────────────────────────────────────────────────────────────────────

struct ModBench {
    std::string mod_name;
    std::string op_type;
    double      us_mean = 0;
    double      notes_affected = 0;
};

static std::vector<ModBench> bench_mods(const ChartEntry& ref) {
    std::vector<ModBench> out;

    // Helper: load a fresh copy, apply mod, measure
    auto run = [&](const std::string& mod_name,
                   const std::string& op_type,
                   mods::AnyOp op) {
        ModBench mb;
        mb.mod_name      = mod_name;
        mb.op_type       = op_type;
        ChartData chart  = load_source(ref.path);
        engine::precompute_t_enter(chart.lines, chart.notes, 1280, 720);
        mb.notes_affected = (double)chart.playable_count;

        auto r = bench([&]{
            ChartData c2 = chart;  // copy, then apply
            mods::apply(c2, op);
        }, 3, 12);
        mb.us_mean = r.mean_ns / 1000.0;
        out.push_back(mb);
    };

    run("Mirror",       "mirror",      mods::MirrorOp{});
    run("Mirror+flip",  "mirror",      mods::MirrorOp{0.0, true});
    run("Colorize constant", "colorize", mods::ColorizeOp{mods::ColorMode::Constant, {255,0,0}});
    run("Colorize gradient", "colorize", mods::ColorizeOp{mods::ColorMode::Gradient, {}, {0,0,0}, {255,255,255}});
    run("Colorize hue",      "colorize", mods::ColorizeOp{mods::ColorMode::Hue});
    run("Speed ×1.5",   "speed",       mods::SpeedOp{1.5});
    run("Opacity 0.5",  "opacity",     mods::OpacityOp{0.5});
    run("Wave",         "wave",        mods::WaveOp{100.0, 1.0, 0.0});
    run("Shuffle",      "shuffle",     mods::ShuffleOp{42, 200.0});
    run("Filter: taps only", "note_filter",  mods::NoteFilterOp{{1,4}, {}});
    run("Filter: no holds",  "note_filter",  mods::NoteFilterOp{{}, {3}});
    run("Flip timing",  "flip_timing", mods::FlipTimingOp{});
    run("Scale ×1.2",   "scale",       mods::ScaleOp{1.2, 1.0});

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 7 — Chart compiler
// ─────────────────────────────────────────────────────────────────────────────

struct CompilerBench {
    std::string name;
    int    sample_rate = 0;
    int    sample_count = 0;
    double compile_ms = 0;
    size_t phbc_bytes = 0;
    double bytes_per_note = 0;
    double kb_per_sec = 0;  // phbc KB per chart-second
};

static std::vector<CompilerBench> bench_compiler(const ChartEntry& e) {
    ChartData chart = load_source(e.path);
    engine::precompute_t_enter(chart.lines, chart.notes, 1280, 720);

    std::vector<CompilerBench> out;

    for (int hz : {60, 120, 240, 480}) {
        CompilerBench cb;
        cb.name        = e.name;
        cb.sample_rate = hz;

        // Compile + write to memory-buffer
        auto t0 = SC::now();
        auto compiled  = chart::compile_chart(chart, static_cast<float>(hz));
        auto t1 = SC::now();
        cb.compile_ms  = std::chrono::duration<double, std::milli>(t1 - t0).count();
        cb.sample_count = compiled.sample_count;

        // Measure output size by writing to a stringstream
        std::ostringstream oss(std::ios::binary);
        chart::write_phbc(compiled, oss);
        cb.phbc_bytes  = oss.str().size();

        double dur = chart.chart_end_t - chart.offset;
        cb.bytes_per_note = chart.playable_count > 0
                          ? static_cast<double>(cb.phbc_bytes) / chart.playable_count : 0;
        cb.kb_per_sec = dur > 0 ? (cb.phbc_bytes / 1000.0) / dur : 0;

        out.push_back(cb);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 8 — PHBC I/O
// ─────────────────────────────────────────────────────────────────────────────

struct PhbcIOBench {
    std::string name;
    double source_load_ms = 0;  // parse source chart
    double compile_ms     = 0;  // compile to CompiledChartData
    double write_ms       = 0;  // write_phbc to buffer
    double read_ms        = 0;  // read_phbc from buffer
    double tochartdata_ms = 0;  // CompiledChartData::to_chart_data()
    double total_phbc_ms  = 0;  // read_phbc + to_chart_data
    double speedup        = 0;  // source_load_ms / total_phbc_ms
    size_t phbc_bytes     = 0;
};

static PhbcIOBench bench_phbc_io(const ChartEntry& e) {
    PhbcIOBench pb;
    pb.name = e.name;

    // 1. Measure source load (3 warmup, 8 iters)
    {
        auto r = bench([&]{ load_source(e.path); }, 3, 8);
        pb.source_load_ms = r.mean_ns / 1e6;
    }

    // 2. Compile once at 240 Hz
    ChartData chart = load_source(e.path);
    engine::precompute_t_enter(chart.lines, chart.notes, 1280, 720);
    {
        auto t0 = SC::now();
        auto compiled = chart::compile_chart(chart, 240.0f);
        auto t1 = SC::now();
        pb.compile_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        // 3. Write to string buffer
        std::string buf;
        {
            auto tw0 = SC::now();
            std::ostringstream oss(std::ios::binary);
            chart::write_phbc(compiled, oss);
            buf = oss.str();
            auto tw1 = SC::now();
            pb.write_ms = std::chrono::duration<double, std::milli>(tw1 - tw0).count();
        }
        pb.phbc_bytes = buf.size();

        // 4. Measure read_phbc (3 warmup, 8 iters)
        {
            auto r = bench([&]{
                std::istringstream iss(buf, std::ios::binary);
                auto c = chart::read_phbc(iss);
                (void)c;
            }, 3, 8);
            pb.read_ms = r.mean_ns / 1e6;
        }

        // 5. Measure to_chart_data()
        {
            std::istringstream iss(buf, std::ios::binary);
            auto compiled2 = chart::read_phbc(iss);
            auto r = bench([&]{
                auto cd = compiled2.to_chart_data();
                (void)cd;
            }, 2, 6);
            pb.tochartdata_ms = r.mean_ns / 1e6;
        }
    }

    pb.total_phbc_ms = pb.read_ms + pb.tochartdata_ms;
    pb.speedup = pb.total_phbc_ms > 0 ? pb.source_load_ms / pb.total_phbc_ms : 0;
    return pb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 9 — Memory footprint
// ─────────────────────────────────────────────────────────────────────────────

struct MemFootprint {
    // struct sizes
    size_t sizeof_note        = sizeof(Note);
    size_t sizeof_line        = sizeof(Line);
    size_t sizeof_notestate   = sizeof(NoteState);
    size_t sizeof_linestate   = sizeof(engine::LineState);

    // Per-chart estimated heap (notes vector only — lower bound)
    struct ChartMem {
        std::string name;
        int    notes = 0, lines = 0;
        size_t notes_vec_bytes = 0;
        size_t states_vec_bytes = 0;
        size_t phbc_240hz_bytes = 0;
    };
    std::vector<ChartMem> charts;
};

static MemFootprint compute_mem(const std::vector<ChartEntry>& entries) {
    MemFootprint mf;
    for (const auto& e : entries) {
        ChartData chart = load_source(e.path);
        MemFootprint::ChartMem cm;
        cm.name             = e.name;
        cm.notes            = (int)chart.notes.size();
        cm.lines            = (int)chart.lines.size();
        cm.notes_vec_bytes  = chart.notes.size() * sizeof(Note);
        cm.states_vec_bytes = chart.notes.size() * sizeof(NoteState);

        // Compile to get phbc size
        engine::precompute_t_enter(chart.lines, chart.notes, 1280, 720);
        auto compiled = chart::compile_chart(chart, 240.0f);
        std::ostringstream oss(std::ios::binary);
        chart::write_phbc(compiled, oss);
        cm.phbc_240hz_bytes = oss.str().size();

        mf.charts.push_back(cm);
    }
    return mf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 10 — Track evaluation (piecewise vs SampledTrack)
// ─────────────────────────────────────────────────────────────────────────────

struct TrackEvalBench {
    double piecewise_ns_per_call = 0;    // TrackFn lambda wrapping PiecewiseTrack
    double sampled_ns_per_call   = 0;    // SampledTrack::eval()
    double speedup               = 0;
};

static TrackEvalBench bench_track_eval(const ChartEntry& e) {
    ChartData chart = load_source(e.path);
    engine::precompute_t_enter(chart.lines, chart.notes, 1280, 720);

    if (chart.lines.empty()) return {};

    // Use line 0's pos_x track
    const Line& ln0 = chart.lines[0];

    double t_start = chart.offset;
    double t_end   = chart.chart_end_t;
    constexpr int N = 100'000;

    // Piecewise (TrackFn = std::function)
    // Use volatile pointer to prevent dead-code elimination of the eval loop.
    static volatile double g_sink = 0;
    double sink1 = 0;
    auto r1 = bench([&]{
        for (int i = 0; i < N; ++i) {
            double t = t_start + (t_end - t_start) * i / N;
            sink1 += ln0.pos_x(t);
        }
        g_sink += sink1;
    }, 3, 8);
    (void)sink1;

    // SampledTrack at 240 Hz
    auto compiled = chart::compile_chart(chart, 240.0f);
    if (compiled.lines.empty()) return {};

    // Wrap line 0 pos_x in a SampledTrack
    chart::SampledTrack st;
    st.t_start     = compiled.t_start;
    st.sample_rate = 240.0f;
    st.samples     = compiled.lines[0].pos_x;

    double sink2 = 0;
    auto r2 = bench([&]{
        for (int i = 0; i < N; ++i) {
            double t = t_start + (t_end - t_start) * i / N;
            sink2 += st.eval(t);
        }
        g_sink += sink2;
    }, 3, 8);
    (void)sink2;

    TrackEvalBench tb;
    tb.piecewise_ns_per_call = r1.mean_ns / N;
    tb.sampled_ns_per_call   = r2.mean_ns / N;
    tb.speedup = tb.piecewise_ns_per_call > 0
               ? tb.piecewise_ns_per_call / tb.sampled_ns_per_call : 0;
    return tb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 11 — Render Effects CPU Overhead (trail / motion blur simulation)
// ─────────────────────────────────────────────────────────────────────────────
//
// Trail:  The trail renderer reuses past SDL render-target textures — zero extra
//         build_frame() calls per render frame.  CPU overhead = negligible.
//
// Motion blur: Samples N evenly-spaced sub-frames within the current shutter
//              window, calling build_frame() once per sample.  This section
//              measures the per-render-frame cost for 1×, 4×, and 8× samples
//              to show how motion-blur sample count scales.
//
// We also check that no_cull_enter_time=false (t_enter culling enabled) does
// not visually regress the snapshot versus the default no-cull baseline.
// ─────────────────────────────────────────────────────────────────────────────

struct RenderEffectsBench {
    std::string name;
    int notes = 0;
    // us per render-frame for 1×/4×/8× build_frame calls
    double us_plain  = 0;   // 1 sample  (plain render)
    double us_mb4    = 0;   // 4 samples (motion blur, shutter=0.5)
    double us_mb8    = 0;   // 8 samples (motion blur, shutter=0.5)
    // t_enter culling: verify snapshot note count matches baseline
    int    cull_ok   = 1;   // 1 = culled count ≤ uncullled count (no extra notes)
};

static RenderEffectsBench bench_render_effects(const ChartEntry& e) {
    ChartData chart = load_source(e.path);
    engine::precompute_t_enter(chart.lines, chart.notes, 1280, 720);

    const double t_start   = chart.offset;
    const double t_end     = chart.chart_end_t + 2.0;
    constexpr double SIM_DT = 1.0 / 240.0;
    constexpr double SHUTTER = 0.5;    // shutter angle fraction (matches default)

    config::RenderConfig cfg_plain;
    cfg_plain.window_w = 1280; cfg_plain.window_h = 720;
    // no_cull_enter_time = true (default) — t_enter culling OFF

    config::RenderConfig cfg_cull = cfg_plain;
    cfg_cull.no_cull_enter_time = false; // t_enter culling ON

    std::vector<NoteState> states(chart.notes.size());
    for (size_t i = 0; i < states.size(); ++i) states[i].note = &chart.notes[i];
    engine::Judge          judge;
    engine::SimulatePlayer sim(engine::SimMode::Conservative);
    engine::NoteManager    note_mgr(&chart.notes, &states);

    // Collect a representative set of simulation time points for benchmarking
    std::vector<double> ts;
    ts.reserve(static_cast<size_t>((t_end - t_start) / SIM_DT) + 128);

    for (double t = t_start; t <= t_end; t += SIM_DT) {
        int idx = note_mgr.find_next_note_index(t);
        sim.step(t, chart.notes, states, chart.lines, judge, 1280, 720);
        engine::detect_misses(states, idx, t, engine::Judge::BAD, judge);
        engine::hold_maintenance(states, idx, t, 0.30, judge);
        engine::hold_finalize(states, idx, t, 0.30, engine::Judge::BAD, judge);
        ts.push_back(t);
    }
    if (ts.empty()) return {};

    // Helper: time N build_frame calls per render tick at each time point
    auto measure_ns = [&](int samples_per_frame,
                          const config::RenderConfig& cfg) -> double {
        // Limit frames measured to keep benchmark fast (sample every 4th tick)
        std::vector<double> frame_ns;
        frame_ns.reserve(ts.size() / 4 + 1);
        for (size_t fi = 0; fi < ts.size(); fi += 4) {
            double t_base = ts[fi];
            auto ta = SC::now();
            for (int s = 0; s < samples_per_frame; ++s) {
                // Sub-frame offset: spread samples across shutter window
                double frac = (samples_per_frame <= 1)
                    ? 0.0
                    : static_cast<double>(s) / (samples_per_frame - 1);
                double t_sub = t_base - SHUTTER * SIM_DT * (1.0 - frac);
                auto fr = render::build_frame(t_sub, chart, states, judge, cfg);
                (void)fr;
            }
            auto tb = SC::now();
            frame_ns.push_back(
                std::chrono::duration<double, std::nano>(tb - ta).count());
        }
        double sum = std::accumulate(frame_ns.begin(), frame_ns.end(), 0.0);
        return (sum / frame_ns.size()) / 1000.0; // → μs
    };

    // Verify: with t_enter culling on, note count in snapshot must be ≤ baseline
    int cull_ok = 1;
    {
        // Pick a time near the midpoint where notes are active
        double t_mid = ts[ts.size() / 2];
        auto fr_plain = render::build_frame(t_mid, chart, states, judge, cfg_plain);
        auto fr_cull  = render::build_frame(t_mid, chart, states, judge, cfg_cull);
        // Culled snapshot must not show MORE notes than uncullled baseline
        if (fr_cull.notes.size() > fr_plain.notes.size()) cull_ok = 0;
    }

    RenderEffectsBench b;
    b.name    = e.name;
    b.notes   = chart.playable_count;
    b.us_plain = measure_ns(1, cfg_plain);
    b.us_mb4   = measure_ns(4, cfg_plain);
    b.us_mb8   = measure_ns(8, cfg_plain);
    b.cull_ok  = cull_ok;
    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pretty-print helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string kb(uintmax_t bytes) {
    std::ostringstream o;
    if      (bytes >= 1'000'000) o << std::fixed << std::setprecision(1) << bytes/1e6 << " MB";
    else if (bytes >= 1'000)     o << std::fixed << std::setprecision(1) << bytes/1e3 << " KB";
    else                         o << bytes << " B";
    return o.str();
}

static std::string commify(long long v) {
    std::string s = std::to_string(v);
    int n = (int)s.size() - 3;
    while (n > 0) { s.insert(n, ","); n -= 3; }
    return s;
}

static std::string pct(double v, double budget) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(1) << (v / budget * 100) << "%";
    return o.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string charts_dir = ".";
    std::string out_path   = "BENCHMARKS.md";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) out_path = argv[++i];
        else if (a[0] != '-') charts_dir = a;
    }

    std::cout << "════════════════════════════════════════════════════════════════\n";
    std::cout << "  Phigros Renderer — Comprehensive Benchmark Suite\n";
    std::cout << "════════════════════════════════════════════════════════════════\n";
    std::cout << "  Charts dir : " << charts_dir << "\n";
    std::cout << "  Output     : " << out_path   << "\n\n";

    // ── Discover charts ──────────────────────────────────────────────────────
    auto entries = discover_charts(charts_dir);
    if (entries.empty()) {
        std::cerr << "[Error] No charts found in: " << charts_dir << "\n";
        return 1;
    }
    std::cout << "  Found " << entries.size() << " chart(s)\n\n";

    // ── Run all benchmarks ───────────────────────────────────────────────────
    MD md;
    md.h1("Phigros Renderer — Benchmark Report");

    // ── System info ──────────────────────────────────────────────────────────
    std::string cpu = get_cpu_model();
    int ncpu = get_nproc();
    std::string os  = get_os();

    md.h2("System");
    md.table({"Key", "Value"}, {
        {"CPU",          cpu},
        {"Cores",        std::to_string(ncpu)},
        {"OS",           os},
        {"Build",        "Release (-O3)"},
        {"C++ standard", "C++17"},
    });

    std::cout << "CPU: " << cpu << "  Cores: " << ncpu << "  OS: " << os << "\n\n";

    // ── Chart inventory ──────────────────────────────────────────────────────
    std::cout << "──── 1. Chart Inventory ─────────────────────────────────────────\n";
    md.h2("1. Chart Inventory");

    std::vector<ChartStats> all_stats;
    for (const auto& e : entries) {
        std::cout << "  Loading: " << e.name << "…  ";
        std::cout.flush();
        all_stats.push_back(compute_stats(e));
        std::cout << "OK  (" << all_stats.back().notes << " notes, "
                  << all_stats.back().lines << " lines, "
                  << std::fixed << std::setprecision(1) << all_stats.back().duration << "s)\n";
    }

    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& s : all_stats) {
            rows.push_back({
                s.entry.name,
                s.entry.format,
                kb(s.entry.file_bytes),
                std::to_string(s.lines),
                std::to_string(s.notes),
                std::to_string(s.holds),
                std::to_string(s.fakes),
                std::to_string((int)s.duration) + "s",
                kb(s.notes_heap_bytes + s.lines_heap_bytes),
            });
        }
        md.table({"Chart", "Format", "File Size", "Lines", "Notes", "Holds",
                  "Fakes", "Duration", "Notes+Lines Heap"},
                 rows);
    }

    // ── Parser throughput ────────────────────────────────────────────────────
    std::cout << "\n──── 2. Parser Throughput ───────────────────────────────────────\n";
    md.h2("2. Parser Throughput");
    md.p("Each chart parsed 8 times (3 warmup); mean/min/max reported.");

    std::vector<ParseBench> parse_results;
    for (const auto& e : entries) {
        std::cout << "  " << e.name << "…  "; std::cout.flush();
        parse_results.push_back(bench_parse(e));
        std::cout << fmt_ns(parse_results.back().result.mean_ns) << "\n";
    }

    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& p : parse_results) {
            std::ostringstream mbs;
            mbs << std::fixed << std::setprecision(1) << p.mb_per_s << " MB/s";
            rows.push_back({
                p.entry.name,
                p.entry.format,
                kb(p.entry.file_bytes),
                fmt_ns(p.result.min_ns),
                fmt_ns(p.result.mean_ns),
                fmt_ns(p.result.max_ns),
                fmt_ns(p.result.stddev_ns) + " σ",
                mbs.str(),
            });
        }
        md.table({"Chart", "Format", "File Size",
                  "Min Parse", "Mean Parse", "Max Parse", "Stddev", "Throughput"},
                 rows);
    }

    // ── Engine simulation ────────────────────────────────────────────────────
    std::cout << "\n──── 3. Engine Simulation ───────────────────────────────────────\n";
    md.h2("3. Engine Simulation (Full Loop)");
    md.p("Single simulation pass at 240 Hz (1/240 s step). "
         "Includes SimulatePlay, miss detection, hold maintenance, hold finalization.");

    struct SimRow { SimBench b; };
    std::vector<SimBench> sim_results;
    for (const auto& e : entries) {
        std::cout << "  " << e.name << "…  "; std::cout.flush();
        sim_results.push_back(bench_sim(e, 1.0/240.0));
        auto& s = sim_results.back();
        std::cout << std::fixed << std::setprecision(1) << s.total_ms << " ms  ("
                  << std::setprecision(0) << s.realtime_x << "× realtime)\n";
    }

    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& s : sim_results) {
            std::ostringstream ms, uspn, rt;
            ms   << std::fixed << std::setprecision(1) << s.total_ms << " ms";
            uspn << std::fixed << std::setprecision(3) << s.us_per_note << " μs";
            rt   << std::fixed << std::setprecision(0) << s.realtime_x << "×";
            rows.push_back({
                s.name,
                std::to_string(s.notes),
                std::to_string(s.frames),
                std::to_string((int)s.duration) + "s",
                ms.str(),
                uspn.str(),
                rt.str(),
            });
        }
        md.table({"Chart", "Notes", "Frames (240Hz)", "Duration",
                  "Sim Time", "μs/note", "Realtime ×"},
                 rows);
    }

    // Simulation step comparison for largest chart
    const ChartEntry& ref = entries.back();
    std::cout << "\n  Step-rate comparison on: " << ref.name << "\n";
    md.h3("Simulation Step-Rate Comparison");
    md.p("Same chart at different simulation step sizes:");
    {
        std::vector<std::vector<std::string>> rows;
        for (double dt : {1.0/60.0, 1.0/120.0, 1.0/240.0, 1.0/480.0}) {
            int hz = (int)std::round(1.0/dt);
            std::cout << "    " << hz << " Hz…  "; std::cout.flush();
            auto s = bench_sim(ref, dt);
            std::ostringstream ms, rt;
            ms << std::fixed << std::setprecision(1) << s.total_ms << " ms";
            rt << std::fixed << std::setprecision(0) << s.realtime_x << "×";
            std::cout << ms.str() << "\n";
            rows.push_back({
                std::to_string(hz) + " Hz",
                std::to_string(s.frames),
                ms.str(),
                rt.str(),
            });
        }
        md.table({"Step Rate", "Frames", "Sim Time", "Realtime ×"}, rows);
    }

    // ── build_frame performance ──────────────────────────────────────────────
    std::cout << "\n──── 4. Render build_frame ──────────────────────────────────────\n";
    md.h2("4. Render Pipeline — build_frame");
    md.p("Pure CPU cost of assembling one render frame snapshot (no GPU calls). "
         "Measured across all frames at 240 Hz simulation; p95 column shows tail latency. "
         "Frame budget @ 60 fps = 16,667 μs; @ 240 fps = 4,167 μs.");

    std::vector<BuildFrameBench> bf_results;
    for (const auto& e : entries) {
        std::cout << "  " << e.name << "…  "; std::cout.flush();
        bf_results.push_back(bench_build_frame(e));
        auto& b = bf_results.back();
        std::cout << std::fixed << std::setprecision(2) << b.us_mean << " μs/frame  ("
                  << std::setprecision(0) << b.realtime_x << "× realtime)\n";
    }

    {
        double budget_240 = 1e6 / 240.0;
        std::vector<std::vector<std::string>> rows;
        for (const auto& b : bf_results) {
            std::ostringstream mn, mean, mx, p95, rt, pct_b;
            mn  << std::fixed << std::setprecision(2) << b.us_min  << " μs";
            mean<< std::fixed << std::setprecision(2) << b.us_mean << " μs";
            mx  << std::fixed << std::setprecision(2) << b.us_max  << " μs";
            p95 << std::fixed << std::setprecision(2) << b.us_p95  << " μs";
            rt  << std::fixed << std::setprecision(0) << b.realtime_x << "×";
            pct_b << std::fixed << std::setprecision(1)
                  << (b.us_mean / budget_240 * 100) << "%";
            rows.push_back({
                b.name,
                std::to_string(b.lines) + "L/" + std::to_string(b.notes) + "N",
                std::to_string(b.samples),
                mn.str(), mean.str(), mx.str(), p95.str(),
                rt.str(),
                pct_b.str() + " of 240fps budget",
            });
        }
        md.table({"Chart", "L/N", "Frames", "Min", "Mean", "Max", "p95",
                  "Realtime ×", "Budget %"},
                 rows);
    }

    // ── Kinematics ───────────────────────────────────────────────────────────
    std::cout << "\n──── 5. Kinematics (eval_line_state) ────────────────────────────\n";
    md.h2("5. Kinematics — eval_line_state");
    md.p("Cost of evaluating all track functions for one judge-line at one time step. "
         "Benchmarked as a tight loop of 2,000 × lines iterations.");

    std::vector<KinematicsBench> kin_results;
    for (const auto& e : entries) {
        std::cout << "  " << e.name << "…  "; std::cout.flush();
        kin_results.push_back(bench_kinematics(e));
        auto& k = kin_results.back();
        std::cout << std::fixed << std::setprecision(1) << k.ns_per_call << " ns/call  ("
                  << std::setprecision(1) << k.million_calls_per_s << "M calls/s)\n";
    }

    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& k : kin_results) {
            std::ostringstream ns, mcps;
            ns   << std::fixed << std::setprecision(1) << k.ns_per_call << " ns";
            mcps << std::fixed << std::setprecision(1) << k.million_calls_per_s << "M/s";
            rows.push_back({ k.name, std::to_string(k.lines), ns.str(), mcps.str() });
        }
        md.table({"Chart", "Lines", "ns/call", "Calls/sec"}, rows);
    }

    // ── Mod application ──────────────────────────────────────────────────────
    std::cout << "\n──── 6. Mod Application ─────────────────────────────────────────\n";
    md.h2("6. Mod Application");
    md.p("Applied to the largest available chart (includes copying ChartData). "
         "12 warmup + measurement iterations.");

    // Pick largest chart by notes
    const ChartEntry* mod_ref = &entries[0];
    for (const auto& e : entries) {
        if (e.file_bytes > mod_ref->file_bytes) mod_ref = &e;
    }
    std::cout << "  Reference chart: " << mod_ref->name << "\n";
    md.note("Reference chart: " + mod_ref->name);

    auto mod_results = bench_mods(*mod_ref);

    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& m : mod_results) {
            std::ostringstream us;
            us << std::fixed << std::setprecision(1) << m.us_mean << " μs";
            rows.push_back({
                m.mod_name,
                m.op_type,
                std::to_string((int)m.notes_affected),
                us.str(),
                std::to_string((int)(m.notes_affected / (m.us_mean / 1e6))) + " notes/s",
            });
        }
        md.table({"Mod", "Op Type", "Notes", "Mean Time", "Throughput"}, rows);
    }
    for (const auto& m : mod_results) {
        std::cout << "  " << std::left << std::setw(22) << m.mod_name
                  << std::fixed << std::setprecision(1) << m.us_mean << " μs\n";
    }

    // ── Compiler ─────────────────────────────────────────────────────────────
    std::cout << "\n──── 7. Chart Compiler ──────────────────────────────────────────\n";
    md.h2("7. Chart Compiler — compile_chart()");
    md.p("One-time compile cost at 60/120/240/480 Hz sample rates. "
         "Output size measured via write_phbc to memory buffer.");

    for (const auto& e : entries) {
        std::cout << "  " << e.name << "…  "; std::cout.flush();
        auto comp_results = bench_compiler(e);

        std::vector<std::vector<std::string>> rows;
        for (const auto& c : comp_results) {
            std::ostringstream ms, sz, bpn, kps;
            ms  << std::fixed << std::setprecision(1) << c.compile_ms  << " ms";
            sz  << kb(c.phbc_bytes);
            bpn << std::fixed << std::setprecision(0) << c.bytes_per_note << " B/note";
            kps << std::fixed << std::setprecision(1) << c.kb_per_sec << " KB/s";
            rows.push_back({
                std::to_string(c.sample_rate) + " Hz",
                std::to_string(c.sample_count),
                ms.str(), sz.str(), bpn.str(), kps.str()
            });
        }
        std::cout << comp_results[2].compile_ms << " ms @ 240Hz  ("
                  << kb(comp_results[2].phbc_bytes) << ")\n";
        md.h3(e.name);
        md.table({"Sample Rate", "Samples", "Compile Time", "PHBC Size",
                  "Bytes/Note", "KB/Chart-Sec"}, rows);
    }

    // ── PHBC I/O ─────────────────────────────────────────────────────────────
    std::cout << "\n──── 8. PHBC Binary I/O ─────────────────────────────────────────\n";
    md.h2("8. PHBC Binary I/O");
    md.p("Comparing source chart load (JSON parse + track build) "
         "vs PHBC load (binary read + to_chart_data) at 240 Hz compilation. "
         "Speedup = source_load / (read_phbc + to_chart_data).");

    std::vector<PhbcIOBench> phbc_results;
    for (const auto& e : entries) {
        std::cout << "  " << e.name << "…  "; std::cout.flush();
        phbc_results.push_back(bench_phbc_io(e));
        auto& p = phbc_results.back();
        std::cout << "src=" << std::fixed << std::setprecision(1) << p.source_load_ms << "ms"
                  << "  phbc=" << p.total_phbc_ms << "ms"
                  << "  speedup=" << std::setprecision(2) << p.speedup << "×\n";
    }

    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& p : phbc_results) {
            std::ostringstream src, cpl, wr, rd, tcd, tot, sp;
            src << std::fixed << std::setprecision(1) << p.source_load_ms  << " ms";
            cpl << std::fixed << std::setprecision(1) << p.compile_ms      << " ms";
            wr  << std::fixed << std::setprecision(1) << p.write_ms        << " ms";
            rd  << std::fixed << std::setprecision(1) << p.read_ms         << " ms";
            tcd << std::fixed << std::setprecision(1) << p.tochartdata_ms  << " ms";
            tot << std::fixed << std::setprecision(1) << p.total_phbc_ms   << " ms";
            sp  << std::fixed << std::setprecision(2) << p.speedup         << "×";
            rows.push_back({
                p.name,
                kb(p.phbc_bytes),
                src.str(), cpl.str(), wr.str(), rd.str(), tcd.str(), tot.str(), sp.str()
            });
        }
        md.table({"Chart", "PHBC Size",
                  "Source Load", "Compile", "Write", "Read", "→ChartData",
                  "PHBC Total", "Speedup"},
                 rows);
    }

    // ── Memory footprint ─────────────────────────────────────────────────────
    std::cout << "\n──── 9. Memory Footprint ────────────────────────────────────────\n";
    md.h2("9. Memory Footprint");

    auto mf = compute_mem(entries);

    md.h3("Struct Sizes");
    md.table({"Struct", "Size (bytes)", "Notes"},
    {
        {"Note",        std::to_string(mf.sizeof_note),
                        "per-note data: times, position, kind, tint, state"},
        {"Line",        std::to_string(mf.sizeof_line),
                        "judge-line: 4× TrackFn (std::function) + PiecewiseTrack"},
        {"NoteState",   std::to_string(mf.sizeof_notestate),
                        "runtime judge state per note"},
        {"LineState",   std::to_string(mf.sizeof_linestate),
                        "per-frame eval result (x, y, rot, alpha, scroll, cos/sin)"},
    });

    std::cout << "  sizeof(Note)       = " << mf.sizeof_note      << " bytes\n";
    std::cout << "  sizeof(Line)       = " << mf.sizeof_line      << " bytes\n";
    std::cout << "  sizeof(NoteState)  = " << mf.sizeof_notestate << " bytes\n";
    std::cout << "  sizeof(LineState)  = " << mf.sizeof_linestate << " bytes\n\n";

    md.h3("Per-Chart Heap Estimate");
    md.note("Notes vector: `sizeof(Note) × note_count` (lower bound — does not include "
            "string heap for hitsound_path or PiecewiseTrack segment vectors). "
            "PHBC size is the on-disk binary; in-memory it becomes SampledTrack float arrays.");
    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& cm : mf.charts) {
            rows.push_back({
                cm.name,
                std::to_string(cm.notes),
                std::to_string(cm.lines),
                kb(cm.notes_vec_bytes),
                kb(cm.states_vec_bytes),
                kb(cm.phbc_240hz_bytes),
            });
        }
        md.table({"Chart", "Notes", "Lines",
                  "Notes Vec (lb)", "States Vec", "PHBC @ 240Hz"},
                 rows);
    }

    // ── Track evaluation ─────────────────────────────────────────────────────
    std::cout << "\n──── 10. Track Evaluation ───────────────────────────────────────\n";
    md.h2("10. Track Evaluation — Piecewise vs SampledTrack");
    md.p("Cost of one `eval(t)` call for a judge-line's `pos_x` track. "
         "Piecewise = `std::function` wrapping a `PiecewiseTrack` (binary search over segments). "
         "Sampled = `SampledTrack::eval()` at 240 Hz (two array lookups + linear interp). "
         "100,000 calls per iteration, 3 warmup + 8 measured iterations.");

    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& e : entries) {
            std::cout << "  " << e.name << "…  "; std::cout.flush();
            auto tb = bench_track_eval(e);
            std::ostringstream pw, st, sp;
            pw << std::fixed << std::setprecision(2) << tb.piecewise_ns_per_call << " ns";
            st << std::fixed << std::setprecision(2) << tb.sampled_ns_per_call   << " ns";
            sp << std::fixed << std::setprecision(2) << tb.speedup << "×";
            std::cout << "piecewise=" << pw.str() << "  sampled=" << st.str()
                      << "  speedup=" << sp.str() << "\n";
            rows.push_back({ e.name, pw.str(), st.str(), sp.str() });
        }
        md.table({"Chart", "Piecewise ns/call", "SampledTrack ns/call", "Speedup"}, rows);
    }

    // ── Render Effects (motion blur CPU overhead) ─────────────────────────────
    std::cout << "\n──── 11. Render Effects (Motion Blur CPU Overhead) ──────────────\n";
    md.h2("11. Render Effects — Motion Blur CPU Overhead");
    md.p("Measures the CPU cost of calling `build_frame()` 1×, 4×, and 8× per render "
         "tick to simulate motion-blur sub-frame sampling. Shutter fraction = 0.5 (180° "
         "shutter), sub-frames spread evenly across the shutter window. "
         "**Trail** uses SDL render-target compositing only — zero extra `build_frame()` "
         "calls — so trail CPU overhead is negligible and not listed separately. "
         "t_enter culling (`no_cull_enter_time=false`) is verified: the culled note count "
         "must not exceed the baseline (uncullled) count at the chart midpoint.");

    std::vector<RenderEffectsBench> re_results;
    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& e : entries) {
            std::cout << "  " << e.name << "…  "; std::cout.flush();
            auto rb = bench_render_effects(e);
            re_results.push_back(rb);

            std::ostringstream plain_s, mb4_s, mb8_s, ov4_s, ov8_s, fps4_s, cull_s;
            plain_s << std::fixed << std::setprecision(1) << rb.us_plain  << " μs";
            mb4_s   << std::fixed << std::setprecision(1) << rb.us_mb4    << " μs";
            mb8_s   << std::fixed << std::setprecision(1) << rb.us_mb8    << " μs";
            double ov4 = rb.us_plain > 0 ? rb.us_mb4 / rb.us_plain : 0;
            double ov8 = rb.us_plain > 0 ? rb.us_mb8 / rb.us_plain : 0;
            double max_fps4 = rb.us_mb4 > 0 ? 1e6 / rb.us_mb4 : 0;
            ov4_s   << std::fixed << std::setprecision(2) << ov4  << "×";
            ov8_s   << std::fixed << std::setprecision(2) << ov8  << "×";
            fps4_s  << std::fixed << std::setprecision(0) << max_fps4;
            cull_s  << (rb.cull_ok ? "✓" : "✗ REGRESSED");

            std::cout << "plain=" << plain_s.str()
                      << "  MB×4=" << mb4_s.str() << " (" << ov4_s.str() << ")"
                      << "  MB×8=" << mb8_s.str() << " (" << ov8_s.str() << ")"
                      << "  cull=" << cull_s.str() << "\n";
            rows.push_back({
                e.name, plain_s.str(),
                mb4_s.str() + " (" + ov4_s.str() + ")",
                mb8_s.str() + " (" + ov8_s.str() + ")",
                fps4_s.str() + " fps",
                cull_s.str()
            });
        }
        md.table(
            {"Chart", "Plain (1×)", "Motion Blur 4× (overhead)", "Motion Blur 8× (overhead)",
             "Max fps at MB×4", "t_enter cull"},
            rows);
        md.p("*Overhead = μs(N×) / μs(1×). Max fps at MB×4 = 1,000,000 / μs(4×).*");
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    md.h2("Summary");

    // Find best/worst per section
    double best_parse_mbs = 0, worst_parse_ms = 0;
    for (const auto& p : parse_results) {
        best_parse_mbs  = std::max(best_parse_mbs, p.mb_per_s);
        worst_parse_ms  = std::max(worst_parse_ms, p.result.mean_ns / 1e6);
    }
    double best_rt = 0;
    for (const auto& s : sim_results) best_rt = std::max(best_rt, s.realtime_x);
    double best_bf_rt = 0, worst_bf_us = 0;
    for (const auto& b : bf_results) {
        best_bf_rt  = std::max(best_bf_rt,  b.realtime_x);
        worst_bf_us = std::max(worst_bf_us, b.us_mean);
    }
    double fastest_mod_us = 1e18, slowest_mod_us = 0;
    for (const auto& m : mod_results) {
        fastest_mod_us = std::min(fastest_mod_us, m.us_mean);
        slowest_mod_us = std::max(slowest_mod_us, m.us_mean);
    }
    double best_speedup = 0;
    for (const auto& p : phbc_results) best_speedup = std::max(best_speedup, p.speedup);

    {
        std::ostringstream best_rt_s, best_bf_s, worst_parse_s,
                           mod_range_s, speedup_s;
        best_rt_s   << std::fixed << std::setprecision(0) << best_rt   << "×";
        best_bf_s   << std::fixed << std::setprecision(0) << best_bf_rt << "×";
        worst_parse_s << std::fixed << std::setprecision(1) << worst_parse_ms << " ms";
        mod_range_s << std::fixed << std::setprecision(0)
                    << fastest_mod_us << "–" << slowest_mod_us << " μs";
        speedup_s   << std::fixed << std::setprecision(1) << best_speedup << "×";

        md.table({"Area", "Headline Result"},
        {
            {"Parser throughput",      std::to_string((int)best_parse_mbs) + " MB/s peak (worst-case: " + worst_parse_s.str() + " for largest chart)"},
            {"Engine simulation",      "Up to " + best_rt_s.str() + " realtime at 240Hz step"},
            {"build_frame (renderer)", "Up to " + best_bf_s.str() + " realtime; worst-case mean " + fmt_ns(worst_bf_us * 1000) + "/frame"},
            {"Mod application",        "All mods < " + fmt_ns(slowest_mod_us * 1000) + "; range " + mod_range_s.str()},
            {"PHBC load speedup",      "Up to " + speedup_s.str() + " faster than source chart load"},
            {"Note struct size",       std::to_string(mf.sizeof_note) + " bytes; NoteState " + std::to_string(mf.sizeof_notestate) + " bytes"},
            {"Render effects (MB CPU)", [&]{
                double max_mb4 = 0;
                for (const auto& r : re_results) max_mb4 = std::max(max_mb4, r.us_mb4);
                std::ostringstream s;
                s << std::fixed << std::setprecision(1)
                  << "Motion blur ×4 worst-case " << max_mb4 << " μs/frame; "
                  << "trail = 0 extra CPU";
                return s.str(); }()},
        });
    }

    md.hr();
    md.p("*Generated by `bench` — run `cpp/build/bench <charts_dir>` to reproduce.*");

    // ── Write markdown ────────────────────────────────────────────────────────
    {
        std::ofstream f(out_path);
        if (!f) {
            std::cerr << "[Error] Cannot write: " << out_path << "\n";
        } else {
            f << md.str();
            f.flush();
            std::cout << "\n══════════════════════════════════════════════════════════════\n";
            std::cout << "  Markdown written: " << out_path << "\n";
            std::cout << "══════════════════════════════════════════════════════════════\n";
        }
    }

    return 0;
}
