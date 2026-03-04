// phigros_render — chart renderer / player.
// Usage: phigros_render <chart_path> [options]  (see --help / app_args.hpp)

#include "phigros/app/app_args.hpp"
#include "phigros/app/app_context.hpp"
#include "phigros/app/game_loop.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/chart/parser.hpp"
#include "phigros/chart/compiler.hpp"
#include "phigros/chart/phbc_io.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/visibility.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/note_manager.hpp"
#include "phigros/engine/hold_logic.hpp"
#include "phigros/engine/simulateplay.hpp"
#include "phigros/core/mods.hpp"
#include "phigros/core/mod_loader.hpp"
#include "phigros/engine/chartscript.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <iomanip>

#ifdef PHIGROS_WASM
#include <emscripten.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── Chart loading helpers ────────────────────────────────────────────────────

static std::string detect_format(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::string first;
    std::getline(f, first);
    if (!first.empty() && (first[0] == 'b' || first[0] == 'c' || first[0] == 'n' ||
        first[0] == '#' || (first[0] >= '0' && first[0] <= '9')))
        return "pec";
    f.seekg(0);
    try {
        json j = json::parse(f);
        if (j.contains("META")) return "rpe";
        return "official";
    } catch (...) {}
    return "pec";
}

static phigros::ChartData load_chart(const std::string& path,
                                      const phigros::config::RenderConfig& cfg) {
    // Binary compiled chart
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".phbc") {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open .phbc file: " + path);
        auto compiled = phigros::chart::read_phbc(f);
        return compiled.to_chart_data(); // is_compiled = true → skip precompute_t_enter
    }
    std::string fmt = detect_format(path);
    if (fmt == "rpe" || fmt == "official") {
        std::ifstream f(path);
        json j = json::parse(f);
        if (fmt == "rpe")
            return phigros::chart::parse_rpe(j, cfg.window_w, cfg.window_h, cfg.rpe_easing_shift);
        return phigros::chart::parse_official(j, cfg.window_w, cfg.window_h);
    }
    return phigros::chart::parse_pec(path, cfg.window_w, cfg.window_h);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    using namespace phigros;
    using namespace phigros::app;

    auto args = parse_args(argc, argv);

    // ── Early exit modes ──────────────────────────────────────────────────────
    if (args.version_mode) {
        printf("phigros-renderer v" PHIGROS_VERSION "\n");
        return 0;
    }
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") { print_usage(argv[0]); return 0; }
    }
    if (args.chart_path.empty() && args.list_charts_dir.empty() && args.script_path.empty()) {
        print_usage(argv[0]); return 1;
    }

    // ── List-charts mode ──────────────────────────────────────────────────────
    if (!args.list_charts_dir.empty()) {
        namespace fs = std::filesystem;
        int count = 0;
        printf("%-50s %-10s %-10s\n", "Chart", "Format", "Path");
        printf("%s\n", std::string(80, '-').c_str());
        try {
            for (const auto& e : fs::recursive_directory_iterator(args.list_charts_dir)) {
                if (!e.is_regular_file()) continue;
                auto ext = e.path().extension().string();
                std::string fmt;
                if (ext == ".phbc") fmt = "phbc";
                else if (ext == ".json") fmt = "json";
                else if (ext == ".pec")  fmt = "pec";
                else continue;
                std::string name = e.path().parent_path().filename().string();
                if (name.size() > 47) name = name.substr(0,44) + "...";
                printf("%-50s %-10s %s\n", name.c_str(), fmt.c_str(),
                       e.path().string().c_str());
                ++count;
            }
        } catch (const std::exception& e2) {
            std::cerr << "[Error] " << e2.what() << "\n"; return 1;
        }
        printf("\n%d chart(s) found.\n", count);
        return 0;
    }

    // ── Load config; apply CLI overrides ──────────────────────────────────────
    config::RenderConfig cfg;
    if (!args.config_path.empty()) cfg = config::load_config(args.config_path);
    if (args.audio_offset_ms != 0.0) cfg.audio_offset_ms = args.audio_offset_ms;
    if (args.window_w > 0) cfg.window_w = args.window_w;
    if (args.window_h > 0) cfg.window_h = args.window_h;
    const int W = cfg.window_w, H = cfg.window_h;

    // ── Info mode (no full parse) ─────────────────────────────────────────────
    if (args.info_mode) {
        const std::string& p = args.chart_path;
        if (p.size() >= 5 && p.substr(p.size()-5) == ".phbc") {
            std::ifstream f(p, std::ios::binary);
            if (!f) { std::cerr << "[Error] Cannot open: " << p << "\n"; return 1; }
            auto compiled = phigros::chart::read_phbc(f);
            f.seekg(0, std::ios::end);
            double mb = static_cast<double>(f.tellg()) / 1e6;
            double dur = compiled.chart_end_t - compiled.offset;
            printf("[Info] %s\n", p.c_str());
            printf("  Format:       .phbc v1\n");
            printf("  Lines:        %d\n", (int)compiled.lines.size());
            printf("  Notes:        %d  (%d playable)\n",
                   (int)compiled.notes.size(), compiled.playable_count);
            printf("  Duration:     %.2f s\n", dur);
            printf("  Sample rate:  %.0f Hz\n", (double)compiled.sample_rate);
            printf("  Samples:      %d\n", compiled.sample_count);
            printf("  File size:    %.2f MB\n", mb);
        } else {
            auto chart = load_chart(p, cfg);
            engine::precompute_t_enter(chart.lines, chart.notes, W, H);
            int holds = 0;
            for (auto& n : chart.notes) if (!n.fake && n.kind == 3) ++holds;
            printf("[Info] %s\n", p.c_str());
            printf("  Format:       %s\n", detect_format(p).c_str());
            printf("  Lines:        %d\n", (int)chart.lines.size());
            printf("  Notes:        %d  (%d playable, %d holds)\n",
                   (int)chart.notes.size(), chart.playable_count, holds);
            printf("  Duration:     %.2f s\n", chart.chart_end_t - chart.offset);
            printf("  Offset:       %.3f s\n", chart.offset);
        }
        return 0;
    }

    // ── CHART SCRIPT MODE ───────────────────────────────────────────────────
    if (!args.script_path.empty()) {
        std::cout << "[ChartScript] Loading: " << args.script_path << "\n";
        chartscript::Script script;
        try {
            script = chartscript::load_script(args.script_path);
            chartscript::apply_ordering(script);
        } catch (const std::exception& e) {
            std::cerr << "[ChartScript] Error: " << e.what() << "\n";
            return 1;
        }
        chartscript::print_script_summary(script);

        if (script.items.empty()) {
            std::cerr << "[ChartScript] No items to play.\n";
            return 1;
        }

        // Helper: run one segment of a chart, returns score
        auto run_segment = [&](
            const chartscript::Item& item,
            const ChartData& item_chart,
            const config::RenderConfig& item_cfg,
            double seg_start, double seg_end,
            bool& quit_out) -> engine::ScoreResult
        {
            const int iW = item_cfg.window_w, iH = item_cfg.window_h;
            AppArgs item_args = args;
            item_args.chart_path = item.input;
            if (!item.bgm.empty()) item_args.audio_path = item.bgm;
            if (!item.bg.empty())  item_args.bg_path    = item.bg;
            double duration = seg_end - seg_start;
            if (duration > 0.0) item_args.duration = duration;

            AppContext item_ctx;
            item_ctx.init(item.input, item_chart.offset + seg_start,
                          item_args.respack_path, item_args.bg_path,
                          item_args.font_path, item_args.audio_path,
                          item_args.headless, iW, iH, item_cfg);

            GameLoop gl(item_ctx, item_args, item_cfg, item_chart,
                        item_chart.playable_count, seg_end);

#ifdef PHIGROS_WASM
            emscripten_set_main_loop_arg(GameLoop::wasm_tick, &gl, 0, 1);
            quit_out = true;
            return {};
#else
            while (gl.run_frame()) {}
#endif
            auto sr = gl.final_score();
            quit_out = item_ctx.window.quit_requested;
            gl.finish();
            item_ctx.destroy();
            return sr;
        };

        // Collect sorted note hit times for notes_window calculation
        auto get_sorted_note_times = [](const ChartData& chart) {
            std::vector<double> times;
            times.reserve(chart.notes.size());
            for (const auto& n : chart.notes)
                if (!n.fake) times.push_back(n.t_hit);
            std::sort(times.begin(), times.end());
            return times;
        };

        // Resume cursor
        int start_cursor = chartscript::load_resume(script.resume_file);
        if (start_cursor > 0)
            std::cout << "[ChartScript] Resuming from item " << (start_cursor + 1) << "\n";

        int  repeats_done = 0;
        int  total_played = 0;
        bool keep_going   = true;
        int  cursor       = start_cursor;

        while (keep_going) {
            while (cursor < static_cast<int>(script.items.size()) && keep_going) {
                const auto& item = script.items[static_cast<size_t>(cursor)];
                if (item.input.empty()) { ++cursor; continue; }

                // Build per-item config
                auto item_cfg = chartscript::build_item_config(cfg, script.defaults, item.config);
                const int iW = item_cfg.window_w, iH = item_cfg.window_h;

                // Load chart (once for all segments)
                std::cout << "\n[ChartScript] [" << (cursor + 1) << "/"
                          << script.items.size() << "] " << item.input;
                if (!item.name.empty())  std::cout << " (" << item.name << ")";
                if (!item.level.empty()) std::cout << " [" << item.level << "]";
                std::cout << "\n";

                ChartData item_chart;
                try {
                    item_chart = load_chart(item.input, item_cfg);
                } catch (const std::exception& e) {
                    std::cerr << "[ChartScript] Failed to load: " << e.what() << "\n";
                    ++cursor; continue;
                }
                if (!item_chart.is_compiled)
                    engine::precompute_t_enter(item_chart.lines, item_chart.notes, iW, iH);

                // Apply mods
                for (const auto& mp : args.mod_paths) {
                    try { mods::apply(item_chart, mods::load_mod(mp)); } catch (...) {}
                }
                auto item_mod = chartscript::resolve_item_mods(item);
                if (!item_mod.ops.empty()) {
                    std::cout << "[ChartScript] Applying " << item_mod.ops.size() << " mod(s)\n";
                    mods::apply(item_chart, item_mod);
                }

                // Build segment list (from segments[], or single start/end, or notes_window)
                std::vector<std::pair<double,double>> windows;
                if (!item.segments.empty()) {
                    auto note_times = get_sorted_note_times(item_chart);
                    for (const auto& seg : item.segments) {
                        double s_start = seg.start;
                        double s_end   = seg.end;
                        if (seg.notes_window > 0) {
                            double nw = chartscript::notes_window_end(
                                note_times, seg.notes_window, seg.tail_time);
                            if (nw > 0.0) s_end = s_start + nw;
                        }
                        if (s_end < 0.0) s_end = item_chart.chart_end_t + 2.0;
                        windows.push_back({s_start, s_end});
                    }
                } else {
                    double s_start = item.start;
                    double s_end   = (item.end > 0.0) ? item.end
                                                       : (item_chart.chart_end_t + 2.0);
                    if (item.notes_window > 0) {
                        auto note_times = get_sorted_note_times(item_chart);
                        double nw = chartscript::notes_window_end(
                            note_times, item.notes_window, item.tail_time);
                        if (nw > 0.0) s_end = s_start + nw;
                    }
                    windows.push_back({s_start, s_end});
                }

                // Play each segment window
                engine::ScoreResult last_sr{};
                for (auto& [w_start, w_end] : windows) {
                    if (w_end <= w_start) continue;
                    bool quit = false;
                    last_sr = run_segment(item, item_chart, item_cfg, w_start, w_end, quit);
                    ++total_played;
                    if (quit) { keep_going = false; break; }
                }

                std::cout << "[ChartScript] Score: " << last_sr.score
                          << "  Acc: " << (last_sr.acc_ratio * 100.0) << "%\n";

                // Save resume position
                chartscript::save_resume(script.resume_file, cursor + 1);

                if (!keep_going) break;

                // on_complete: decide what happens next
                const auto& oc = item.on_complete;
                chartscript::CompleteAction action = oc.action;
                if (oc.has_condition())
                    action = (last_sr.score >= oc.min_score) ? oc.action : oc.else_action;

                switch (action) {
                case chartscript::CompleteAction::Stop:
                    keep_going = false;
                    break;
                case chartscript::CompleteAction::Repeat:
                    // Re-run same item (don't advance cursor)
                    continue;
                case chartscript::CompleteAction::Loop:
                    cursor = 0;
                    continue;
                case chartscript::CompleteAction::Goto:
                    if (oc.goto_index >= 0 &&
                        oc.goto_index < static_cast<int>(script.items.size())) {
                        cursor = oc.goto_index;
                        continue;
                    }
                    [[fallthrough]];
                case chartscript::CompleteAction::Next:
                default:
                    ++cursor;
                    break;
                }
            }

            // End of one pass through items
            ++repeats_done;
            if (script.repeat > 0 && repeats_done >= script.repeat) keep_going = false;

            if (keep_going) {
                cursor = 0;
                chartscript::save_resume(script.resume_file, 0);
                // Re-shuffle for next loop pass
                if (script.mode == chartscript::PlayMode::Shuffle)
                    chartscript::weighted_shuffle(script.items, 0);
            }
        }

        std::cout << "\n[ChartScript] Done. Played " << total_played << " segment(s).\n";
        if (!script.resume_file.empty())
            chartscript::save_resume(script.resume_file, 0);
        return 0;
    }

    // Load chart
    std::cout << "[Chart] Loading: " << args.chart_path << "\n";
    auto chart = load_chart(args.chart_path, cfg);
    std::cout << "[Chart] Lines=" << chart.lines.size()
              << " Notes=" << chart.notes.size()
              << " Offset=" << chart.offset << "s\n";

    if (!chart.is_compiled)
        engine::precompute_t_enter(chart.lines, chart.notes, W, H);

    // ── Apply mods ────────────────────────────────────────────────────────────
    for (const auto& mp : args.mod_paths) {
        try {
            auto mod = mods::load_mod(mp);
            std::cout << "[Mod] Applying: " << mod.name;
            if (!mod.description.empty()) std::cout << " — " << mod.description;
            std::cout << "  (" << mod.ops.size() << " op"
                      << (mod.ops.size() != 1 ? "s" : "") << ")\n";
            mods::apply(chart, mod);
        } catch (const std::exception& e) {
            std::cerr << "[Mod] Error loading '" << mp << "': " << e.what() << "\n";
            return 1;
        }
    }

    double chart_end = chart.chart_end_t + 2.0;

    // ── COMPILE MODE ──────────────────────────────────────────────────────────
    if (!args.compile_output.empty()) {
        using clock = std::chrono::steady_clock;
        std::cout << "[Compile] Sampling at " << args.compile_sample_rate << " Hz …\n";
        auto t0 = clock::now();
        auto compiled = phigros::chart::compile_chart(chart, args.compile_sample_rate);
        auto t1 = clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        double est_mb = compiled.lines.size() * compiled.sample_count * 5 * sizeof(float) / 1e6;
        std::cout << "[Compile] Done in " << static_cast<int>(ms) << " ms"
                  << "  samples=" << compiled.sample_count
                  << "  lines=" << compiled.lines.size()
                  << "  notes=" << compiled.notes.size()
                  << "  est. size=" << std::fixed << std::setprecision(2) << est_mb << " MB\n";

        std::ofstream f(args.compile_output, std::ios::binary);
        if (!f) { std::cerr << "[Error] Cannot open output: " << args.compile_output << "\n"; return 1; }
        phigros::chart::write_phbc(compiled, f);
        f.flush();
        std::cout << "[Compile] Written: " << args.compile_output << "\n";
        return 0;
    }

    // ── SCORE-ONLY / BENCHMARK ────────────────────────────────────────────────
    if (args.score_only) {
        constexpr double SIM_DT   = 1.0 / 240.0;
        constexpr double HOLD_TOL = 0.30;

        auto run_engine = [&]() -> engine::ScoreResult {
            std::vector<NoteState> st(chart.notes.size());
            for (size_t i = 0; i < st.size(); ++i) st[i].note = &chart.notes[i];
            engine::Judge j;
            engine::SimulatePlayer ap(engine::SimMode::Conservative);
            auto fnext = [&](double tc) {
                int lo = 0, hi = (int)st.size();
                while (lo < hi) { int m = (lo+hi)/2;
                    if (st[m].judged || st[m].note->t_hit < tc - 0.5) lo = m+1; else hi = m; }
                return lo;
            };
            int inx = 0;
            for (double tc = chart.offset; tc <= chart_end; tc += SIM_DT) {
                ap.step(tc, chart.notes, st, chart.lines, j, W, H);
                inx = fnext(tc);
                engine::detect_misses(st, inx, tc, engine::Judge::BAD, j);
                engine::hold_maintenance(st, inx, tc, HOLD_TOL, j);
                engine::hold_finalize(st, inx, tc, HOLD_TOL, engine::Judge::BAD, j);
            }
            return engine::compute_score(j.acc_sum, j.max_combo, chart.playable_count);
        };

        if (args.benchmark) {
            int N = args.benchmark_iterations;
            std::cout << "[Benchmark] Running " << N << " iterations...\n";
            using clock = std::chrono::high_resolution_clock;
            std::vector<double> ms; ms.reserve(N);
            for (int i = 0; i < N; ++i) {
                auto t0 = clock::now();
                auto sr = run_engine();
                ms.push_back(std::chrono::duration<double,std::milli>(clock::now()-t0).count());
                if (sr.score != 1000000)
                    std::cerr << "[Benchmark] WARNING iter=" << i << " score=" << sr.score << "\n";
            }
            std::sort(ms.begin(), ms.end());
            double tot = 0; for (double m : ms) tot += m;
            double len = chart_end - chart.offset;
            std::cout << "\n=== Benchmark (" << N << " iters) ==="
                      << "\n  Mean:   " << (tot/N) << " ms"
                      << "\n  Median: " << ms[N/2] << " ms"
                      << "\n  P95:    " << ms[std::min(N-1,(int)(N*0.95))] << " ms"
                      << "\n  Speed:  " << (len/(tot/N/1000.0)) << "x realtime\n";
            return 0;
        }

        std::vector<NoteState> st(chart.notes.size());
        for (size_t i = 0; i < st.size(); ++i) st[i].note = &chart.notes[i];
        engine::Judge j; engine::SimulatePlayer ap(engine::SimMode::Conservative);
        auto fnext = [&](double tc) {
            int lo=0, hi=(int)st.size();
            while (lo<hi) { int m=(lo+hi)/2;
                if (st[m].judged||st[m].note->t_hit<tc-0.5) lo=m+1; else hi=m; }
            return lo;
        };
        int inx = 0;
        for (double tc = chart.offset; tc <= chart_end; tc += SIM_DT) {
            ap.step(tc, chart.notes, st, chart.lines, j, W, H);
            inx = fnext(tc);
            engine::detect_misses(st, inx, tc, engine::Judge::BAD, j);
            engine::hold_maintenance(st, inx, tc, 0.30, j);
            engine::hold_finalize(st, inx, tc, 0.30, engine::Judge::BAD, j);
        }
        auto sr = engine::compute_score(j.acc_sum, j.max_combo, chart.playable_count);
        std::cout << "\n=== Score Only ===\nScore: " << sr.score
                  << "\nAccuracy: " << (sr.acc_ratio*100.0) << "%"
                  << "\nMaxCombo: " << j.max_combo << "/" << chart.playable_count
                  << "\nJudged: "   << j.judged_cnt << "/" << chart.playable_count << "\n";
        return (sr.score == 1000000) ? 0 : 1;
    }

    // ── RENDERING / INTERACTIVE MODE ─────────────────────────────────────────
    std::cout << "[Render] Starting (chart_end=" << chart_end << "s, "
              << chart.playable_count << " playable notes)\n";

    AppContext ctx;
    ctx.init(args.chart_path, chart.offset,
             args.respack_path, args.bg_path, args.font_path, args.audio_path,
             args.headless, W, H, cfg);

    GameLoop gl(ctx, args, cfg, chart, chart.playable_count, chart_end);

#ifdef PHIGROS_WASM
    emscripten_set_main_loop_arg(GameLoop::wasm_tick, &gl, 0, 1);
#else
    while (gl.run_frame()) {}
#endif

    // Final results
    auto sr = gl.final_score();
    const char* tag = gl.is_play_mode
        ? (gl.replay_player.enabled() ? "Replay Complete" : "Play Complete")
        : "Render Complete";
    std::cout << "\n=== " << tag << " ==="
              << "\nScore: "    << sr.score
              << "\nAccuracy: " << (sr.acc_ratio * 100.0) << "%"
              << "\nMaxCombo: " << gl.judge.max_combo << "/" << chart.playable_count
              << "\nJudged: "   << gl.judge.judged_cnt << "/" << chart.playable_count << "\n";

    gl.finish();
    ctx.destroy();
    return (sr.score == 1000000) ? 0 : 1;
}
