// phigros_render — chart renderer / player.
// Usage: phigros_render <chart_path> [options]  (see --help / app_args.hpp)

#include "phigros/app/app_args.hpp"
#include "phigros/app/app_context.hpp"
#include "phigros/app/game_loop.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/chart/compiler.hpp"
#include "phigros/chart/phbc_io.hpp"
#include "phigros/chart/chart_loader.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/visibility.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/note_manager.hpp"
#include "phigros/engine/hold_logic.hpp"
#include "phigros/engine/exact_autoplay.hpp"
#include "phigros/engine/simulateplay.hpp"
#include "phigros/engine/scriptplay.hpp"
#include "phigros/core/mods.hpp"
#include "phigros/core/mod_loader.hpp"
#include "phigros/engine/chartscript.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <iomanip>

#ifdef PHIGROS_WASM
#include <emscripten.h>
#endif

namespace fs = std::filesystem;

// ── Chart loading helpers ────────────────────────────────────────────────────

static phigros::engine::SimMode parse_sim_mode_local(const std::string& mode) {
    if (mode == "conservative") return phigros::engine::SimMode::Conservative;
    if (mode == "extreme") return phigros::engine::SimMode::Extreme;
    return phigros::engine::SimMode::Aggressive;
}

static std::string detect_format(const std::string& path) {
    return phigros::chart::chart_format_name(phigros::chart::detect_chart_format(path));
}

static phigros::ChartData load_chart(const std::string& path,
                                      const phigros::config::RenderConfig& cfg,
                                      const std::string& password = "") {
    PHLOG_DEBUG(Chart, "load_chart: path=" << path
        << " window=" << cfg.window_w << "x" << cfg.window_h
        << " easing_shift=" << cfg.rpe_easing_shift);
    auto loaded = phigros::chart::load_chart_with_entry(path, cfg.window_w, cfg.window_h,
                                                        cfg.rpe_easing_shift, password);
    PHLOG_INFO(Chart, "Loaded chart format=" << phigros::chart::chart_format_name(loaded.format)
        << " source=" << loaded.entry.chart_path
        << " diff=" << (loaded.entry.difficulty.empty() ? "<none>" : loaded.entry.difficulty));
    return std::move(loaded.chart);
}

// ── main / SDL_main ──────────────────────────────────────────────────────────

#if defined(PHIGROS_IOS)
extern "C" int SDL_main(int argc, char* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
    using namespace phigros;
    using namespace phigros::app;

    auto args = parse_args(argc, argv);
    init_logger(args);  // Apply --verbose/--quiet/--log-level/--log-filter/--log-file/--log-time
    PHLOG_DEBUG(General, "CLI parsed: argc=" << argc
        << " chart=" << (args.chart_path.empty() ? "<none>" : args.chart_path)
        << " script=" << (args.script_path.empty() ? "<none>" : args.script_path)
        << " scriptplay=" << (args.scriptplay_path.empty() ? "<none>" : args.scriptplay_path)
        << " config=" << (args.config_path.empty() ? "<default>" : args.config_path)
        << " headless=" << args.headless
        << " play=" << args.play_mode
        << " score_only=" << args.score_only
        << " record=" << (!args.record_output.empty()));

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
        auto entries = phigros::chart::scan_charts_directory(args.list_charts_dir);
        printf("%-45s %-6s %-8s %-6s %-6s\n", "Name", "Diff", "Type", "Music", "Image");
        printf("%s\n", std::string(80, '-').c_str());
        for (const auto& e : entries) {
            std::string name = e.name;
            if (name.size() > 43) name = name.substr(0, 40) + "...";
            printf("%-45s %-6s %-8s %-6s %-6s\n",
                   name.c_str(),
                   e.difficulty.empty() ? "-" : e.difficulty.c_str(),
                   e.source_type.c_str(),
                   e.assets.music_path.empty() ? "no" : "yes",
                   e.assets.illustration_path.empty() ? "no" : "yes");
        }
        printf("\n%d chart(s) found.\n", (int)entries.size());
        return 0;
    }

    // ── Load config; apply CLI overrides ──────────────────────────────────────
    config::RenderConfig cfg;
    if (!args.config_path.empty()) {
        PHLOG_INFO(General, "Loading config: " << args.config_path);
        cfg = config::load_config(args.config_path);
    }
    if (!args.backend.empty()) cfg.backend = args.backend;
    if (!args.mode_override.empty()) cfg.gameplay_mode = args.mode_override;
    if (!args.scriptplay_path.empty()) cfg.judge_script_path = args.scriptplay_path;
    if (args.play_mode) cfg.gameplay_mode = "manual";
    if (args.audio_offset_ms != 0.0) cfg.audio_offset_ms = args.audio_offset_ms;
    if (args.playback_speed >= 0.0) cfg.playback_speed = args.playback_speed;
    if (args.window_w > 0) cfg.window_w = args.window_w;
    if (args.window_h > 0) cfg.window_h = args.window_h;
    if (!args.record_output.empty()) {
        if (args.record_capture_w > 0) cfg.window_w = args.record_capture_w;
        if (args.record_capture_h > 0) cfg.window_h = args.record_capture_h;
    }
    // Render preference overrides (take precedence over config file)
    if (args.approach     >= 0.0) cfg.approach      = args.approach;
    if (args.chart_speed  >= 0.0) cfg.chart_speed   = args.chart_speed;
    if (args.expand_factor>= 0.0) cfg.expand_factor = args.expand_factor;
    if (args.note_scale_x >= 0.0) cfg.note_scale_x  = args.note_scale_x;
    if (args.note_scale_y >= 0.0) cfg.note_scale_y  = args.note_scale_y;
    if (args.note_alpha   >= 0.0) cfg.note_alpha     = args.note_alpha;
    if (args.font_size    >= 0.0) cfg.font_size      = args.font_size;
    if (args.overlay_transparent) cfg.overlay_transparent = true;
    const int W = cfg.window_w, H = cfg.window_h;
    PHLOG_DEBUG(General, "Effective config: backend=" << cfg.backend
        << " window=" << W << "x" << H
        << " audio_offset_ms=" << cfg.audio_offset_ms
        << " playback_speed=" << (cfg.playback_speed.has_value() ? std::to_string(*cfg.playback_speed) : std::string("<chart_speed>"))
        << " gameplay_mode=" << cfg.gameplay_mode
        << " judge_script=" << (cfg.judge_script_path.empty() ? "<none>" : cfg.judge_script_path)
        << " approach=" << cfg.approach
        << " chart_speed=" << cfg.chart_speed
        << " expand=" << cfg.expand_factor
        << " note_scale=(" << cfg.note_scale_x << "," << cfg.note_scale_y << ")"
        << " note_alpha=" << cfg.note_alpha
        << " font_size=" << cfg.font_size
        << " overlay_transparent=" << cfg.overlay_transparent);
    if (cfg.gameplay_mode == "scriptplay" && cfg.judge_script_path.empty()) {
        PHLOG_FATAL(Engine, "scriptplay mode requires gameplay.judge_script or --scriptplay");
        return 1;
    }

    // ── Info mode (no full parse) ─────────────────────────────────────────────
    if (args.info_mode) {
        const std::string& p = args.chart_path;
        if (p.size() >= 5 && p.substr(p.size()-5) == ".phbc") {
            std::ifstream f(p, std::ios::binary);
            if (!f) { PHLOG_FATAL(General, "Cannot open: " << p); return 1; }
            auto compiled = phigros::chart::read_phbc(f, args.password);
            f.seekg(0, std::ios::end);
            double mb = static_cast<double>(f.tellg()) / 1e6;
            double dur = compiled.chart_end_t - compiled.offset;

            // Re-read header to get version/flags for display
            f.seekg(4);  // skip magic
            uint16_t ver = 0, fl = 0;
            f.read(reinterpret_cast<char*>(&ver), 2);
            f.read(reinterpret_cast<char*>(&fl), 2);

            // Print chart info as plain output (not log-level gated — this is the purpose of --info)
            printf("[Info] %s\n", p.c_str());
            printf("  Format:       .phbc v%d", (int)ver);
            if (ver >= 2 && fl != 0) {
                auto calgo = phigros::chart::phbc_compression_from_flags(fl);
                bool enc   = (fl & phigros::chart::PHBC_FLAG_ENCRYPTED) != 0;
                auto ealgo = phigros::chart::phbc_encryption_from_flags(fl);
                if (calgo != phigros::chart::CompressionAlgo::None)
                    printf(" (compressed: %s)", phigros::chart::compression_name(calgo));
                if (enc)
                    printf(" (encrypted: %s)", phigros::chart::encryption_name(ealgo));
            }
            printf("\n");
            printf("  Lines:        %d\n", (int)compiled.lines.size());
            printf("  Notes:        %d  (%d playable)\n",
                   (int)compiled.notes.size(), compiled.playable_count);
            printf("  Duration:     %.2f s\n", dur);
            printf("  Sample rate:  %.0f Hz\n", (double)compiled.sample_rate);
            printf("  Samples:      %d\n", compiled.sample_count);
            printf("  File size:    %.2f MB\n", mb);
        } else {
            auto chart = load_chart(p, cfg);
            engine::precompute_t_enter(chart.lines, chart.notes, W, H, cfg.expand_factor,
                                       cfg.note_scale_x, cfg.note_scale_y);
            chart.build_notes_by_enter_index();
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
        PHLOG_INFO(ChartScript, "Loading: " << args.script_path);
        chartscript::Script script;
        try {
            script = chartscript::load_script(args.script_path);
            chartscript::apply_ordering(script);
        } catch (const std::exception& e) {
            PHLOG_FATAL(ChartScript, "Error loading script: " << e.what());
            return 1;
        }
        chartscript::print_script_summary(script);

        if (script.items.empty()) {
            PHLOG_FATAL(ChartScript, "No items to play.");
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
                          item_args.headless, iW, iH, item_cfg,
                          /*no_vsync=*/!item_args.record_output.empty(),
                          item_chart.metadata);

            engine::ScoreResult sr{};
            try {
                GameLoop gl(item_ctx, item_args, item_cfg, item_chart,
                            item_chart.playable_count, seg_end);

#ifdef PHIGROS_WASM
                emscripten_set_main_loop_arg(GameLoop::wasm_tick, &gl, 0, 1);
                quit_out = true;
                return {};
#else
                while (gl.run_frame()) {}
#endif
                sr = gl.final_score();
                quit_out = item_ctx.window.quit_requested;
                gl.finish();
            } catch (const std::exception& e) {
                item_ctx.destroy();
                throw;
            }
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
            PHLOG_INFO(ChartScript, "Resuming from item " << (start_cursor + 1));

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
                {
                    std::ostringstream hdr;
                    hdr << "[" << (cursor + 1) << "/" << script.items.size() << "] " << item.input;
                    if (!item.name.empty())  hdr << " (" << item.name << ")";
                    if (!item.level.empty()) hdr << " [" << item.level << "]";
                    PHLOG_INFO(ChartScript, hdr.str());
                }

                ChartData item_chart;
                try {
                    item_chart = load_chart(item.input, item_cfg, args.password);
                } catch (const std::exception& e) {
                    PHLOG_ERROR(ChartScript, "Failed to load: " << e.what());
                    ++cursor; continue;
                }
                if (!item_chart.is_compiled) {
                    engine::precompute_t_enter(item_chart.lines, item_chart.notes, iW, iH, item_cfg.expand_factor,
                                               item_cfg.note_scale_x, item_cfg.note_scale_y);
                }
                item_chart.build_notes_by_enter_index();
                for (const auto& mp : args.mod_paths) {
                    try { mods::apply(item_chart, mods::load_mod(mp)); } catch (...) {}
                }
                auto item_mod = chartscript::resolve_item_mods(item);
                if (!item_mod.ops.empty()) {
                    PHLOG_INFO(ChartScript, "Applying " << item_mod.ops.size() << " inline mod(s)");
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

                PHLOG_INFO(ChartScript, "Score: " << last_sr.score
                    << "  Acc: " << (last_sr.acc_ratio * 100.0) << "%");

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

        PHLOG_INFO(ChartScript, "Done. Played " << total_played << " segment(s).");
        if (!script.resume_file.empty())
            chartscript::save_resume(script.resume_file, 0);
        return 0;
    }

    // Load chart
    PHLOG_INFO(Chart, "Loading: " << args.chart_path);
    auto chart = load_chart(args.chart_path, cfg, args.password);
    PHLOG_INFO(Chart, "Loaded: lines=" << chart.lines.size()
        << " notes=" << chart.notes.size()
        << " offset=" << chart.offset << "s"
        << " duration=" << (chart.chart_end_t - chart.offset) << "s");
    PHLOG_DEBUG(Chart, "is_compiled=" << chart.is_compiled
        << " playable=" << chart.playable_count
        << " chart_end_t=" << chart.chart_end_t);

    if (!chart.is_compiled) {
        engine::precompute_t_enter(chart.lines, chart.notes, W, H, cfg.expand_factor,
                                   cfg.note_scale_x, cfg.note_scale_y);
    }
    chart.build_notes_by_enter_index();

    // ── Apply mods ────────────────────────────────────────────────────────────
    for (const auto& mp : args.mod_paths) {
        try {
            auto mod = mods::load_mod(mp);
            std::ostringstream mline;
            mline << "Applying mod: " << mod.name;
            if (!mod.description.empty()) mline << " — " << mod.description;
            mline << "  (" << mod.ops.size() << " op" << (mod.ops.size() != 1 ? "s" : "") << ")";
            PHLOG_INFO(Mod, mline.str());
            mods::apply(chart, mod);
        } catch (const std::exception& e) {
            PHLOG_FATAL(Mod, "Error loading '" << mp << "': " << e.what());
            return 1;
        }
    }

    double chart_end = chart.chart_end_t + 2.0;
    double simulation_end = chart_end;
    if (args.duration > 0.0) simulation_end = std::min(simulation_end, args.duration);

    auto count_scoring_notes = [&](double cutoff_t) -> int {
        if (!(args.truncate_at_duration && args.duration > 0.0)) return chart.playable_count;
        int cnt = 0;
        for (const auto& n : chart.notes) {
            if (n.fake) continue;
            if (n.kind == static_cast<int>(NoteKind::Hold)) {
                if (n.t_end <= cutoff_t) ++cnt;
            } else {
                if (n.t_hit <= cutoff_t) ++cnt;
            }
        }
        return cnt;
    };
    int scoring_notes = count_scoring_notes(simulation_end);
    PHLOG_DEBUG(Engine, "Timing bounds: chart_end=" << chart_end
        << " simulation_end=" << simulation_end
        << " scoring_notes=" << scoring_notes
        << " truncate=" << args.truncate_at_duration);

    // ── COMPILE MODE ──────────────────────────────────────────────────────────
    if (!args.compile_output.empty()) {
        using clock = std::chrono::steady_clock;
        PHLOG_INFO(Compile, "Sampling at " << args.compile_sample_rate << " Hz …");
        auto t0 = clock::now();
        auto compiled = phigros::chart::compile_chart(chart, args.compile_sample_rate);
        auto t1 = clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        double est_mb = compiled.lines.size() * compiled.sample_count * 5 * sizeof(float) / 1e6;
        PHLOG_INFO(Compile, "Done in " << static_cast<int>(ms) << " ms"
            << "  samples=" << compiled.sample_count
            << "  lines=" << compiled.lines.size()
            << "  notes=" << compiled.notes.size()
            << "  est. size=" << std::fixed << std::setprecision(2) << est_mb << " MB");

        std::ofstream f(args.compile_output, std::ios::binary);
        if (!f) { PHLOG_FATAL(Compile, "Cannot open output: " << args.compile_output); return 1; }

        if (args.compile_compress || args.compile_encrypt) {
            phigros::chart::PhbcWriteOptions wopts;
            wopts.compress = args.compile_compress;
            if (!args.compile_compress_algo.empty() && args.compile_compress_algo == "lzma")
                wopts.compress_algo = phigros::chart::CompressionAlgo::Lzma;
            wopts.encrypt = args.compile_encrypt;
            if (!args.compile_encrypt_algo.empty()) {
                if (args.compile_encrypt_algo == "aes-cbc")
                    wopts.encrypt_algo = phigros::chart::EncryptionAlgo::AES_256_CBC;
                else if (args.compile_encrypt_algo == "chacha20")
                    wopts.encrypt_algo = phigros::chart::EncryptionAlgo::ChaCha20_Poly1305;
                else if (args.compile_encrypt_algo == "xor")
                    wopts.encrypt_algo = phigros::chart::EncryptionAlgo::XOR;
                // default: AES_256_GCM
            }
            wopts.password = args.password;
            phigros::chart::write_phbc(compiled, f, wopts);

            std::string features;
            if (wopts.compress)
                features += std::string("compressed:") + phigros::chart::compression_name(wopts.compress_algo);
            if (wopts.encrypt) {
                if (!features.empty()) features += ", ";
                features += std::string("encrypted:") + phigros::chart::encryption_name(wopts.encrypt_algo);
            }
            PHLOG_INFO(Compile, "Written v2 (" << features << "): " << args.compile_output);
        } else {
            phigros::chart::write_phbc(compiled, f);
            PHLOG_INFO(Compile, "Written: " << args.compile_output);
        }
        f.flush();
        return 0;
    }

    // ── SCORE-ONLY ────────────────────────────────────────────────────────────
    if (args.score_only) {
        constexpr double SIM_DT   = 1.0 / 240.0;
        const double HOLD_TOL = cfg.hold_tail_tol;
        const bool use_scriptplay = (cfg.gameplay_mode == "scriptplay");

        auto run_engine = [&]() -> engine::ScoreResult {
            std::vector<NoteState> st(chart.notes.size());
            for (size_t i = 0; i < st.size(); ++i) st[i].note = &chart.notes[i];
            engine::Judge j;
            engine::ScriptPlayPlayer sp;
            if (use_scriptplay)
                sp.load(cfg.judge_script_path, chart, HOLD_TOL);
            auto fnext = [&](double tc) {
                int lo = 0, hi = (int)st.size();
                while (lo < hi) { int m = (lo+hi)/2;
                    if (st[m].judged || st[m].note->t_hit < tc - 0.5) lo = m+1; else hi = m; }
                return lo;
            };
            int inx = 0;
            double prev_tc = chart.offset - SIM_DT;
            for (double tc = chart.offset; tc <= simulation_end; tc += SIM_DT) {
                if (use_scriptplay) sp.tick(tc, chart.notes, st, j);
                else engine::exact_autoplay_step(prev_tc, tc, chart.notes, st, chart.lines, j, W, H);
                inx = fnext(tc);
                engine::detect_misses(st, inx, tc, engine::Judge::BAD, j);
                engine::hold_maintenance(st, inx, tc, HOLD_TOL, j);
                engine::hold_finalize(st, inx, tc, HOLD_TOL, engine::Judge::BAD, j);
                prev_tc = tc;
            }
            return engine::compute_score(j.acc_sum, j.max_combo, scoring_notes);
        };

        if (args.benchmark) {
            int N = args.benchmark_iterations;
            PHLOG_INFO(Engine, "Benchmark: running " << N << " iterations…");
            using clock = std::chrono::high_resolution_clock;
            std::vector<double> ms; ms.reserve(N);
            for (int i = 0; i < N; ++i) {
                auto t0 = clock::now();
                auto sr = run_engine();
                ms.push_back(std::chrono::duration<double,std::milli>(clock::now()-t0).count());
                if (sr.score != 1000000)
                    PHLOG_WARN(Engine, "Benchmark WARNING iter=" << i << " score=" << sr.score);
            }
            std::sort(ms.begin(), ms.end());
            double tot = 0; for (double m : ms) tot += m;
            double len = std::max(0.0, simulation_end - chart.offset);
            // Print benchmark results as plain output (not log-level gated)
            printf("\n=== Benchmark (%d iters) ===\n", N);
            printf("  Mean:   %.3f ms\n", tot/N);
            printf("  Median: %.3f ms\n", ms[N/2]);
            printf("  P95:    %.3f ms\n", ms[std::min(N-1,(int)(N*0.95))]);
            printf("  Speed:  %.1fx realtime\n", len/(tot/N/1000.0));
            return 0;
        }

        std::vector<NoteState> st(chart.notes.size());
        PHLOG_INFO(Engine, "Score-only run: sim_dt=" << SIM_DT
            << " start=" << chart.offset << " end=" << simulation_end
            << " notes=" << chart.notes.size());
        for (size_t i = 0; i < st.size(); ++i) st[i].note = &chart.notes[i];
        engine::Judge j;
        engine::ScriptPlayPlayer sp;
        if (use_scriptplay) {
            try {
                sp.load(cfg.judge_script_path, chart, HOLD_TOL);
            } catch (const std::exception& e) {
                PHLOG_FATAL(Engine, e.what());
                return 1;
            }
        }
        auto fnext = [&](double tc) {
            int lo=0, hi=(int)st.size();
            while (lo<hi) { int m=(lo+hi)/2;
                if (st[m].judged||st[m].note->t_hit<tc-0.5) lo=m+1; else hi=m; }
            return lo;
        };
        int inx = 0;
        double prev_tc = chart.offset - SIM_DT;
        for (double tc = chart.offset; tc <= simulation_end; tc += SIM_DT) {
            if (use_scriptplay) sp.tick(tc, chart.notes, st, j);
            else engine::exact_autoplay_step(prev_tc, tc, chart.notes, st, chart.lines, j, W, H);
            inx = fnext(tc);
            engine::detect_misses(st, inx, tc, engine::Judge::BAD, j);
            engine::hold_maintenance(st, inx, tc, HOLD_TOL, j);
            engine::hold_finalize(st, inx, tc, HOLD_TOL, engine::Judge::BAD, j);
            prev_tc = tc;
        }
        auto sr = engine::compute_score(j.acc_sum, j.max_combo, scoring_notes);
        // Print score as plain output (purpose of --score-only)
        printf("\n=== Score Only ===\nScore: %d\nAccuracy: %.4f%%\nMaxCombo: %d/%d\nJudged: %d/%d\n",
               sr.score, sr.acc_ratio*100.0,
               j.max_combo, scoring_notes, j.judged_cnt, scoring_notes);
        return (sr.score == 1000000) ? 0 : 1;
    }

    // ── RENDERING / INTERACTIVE MODE ─────────────────────────────────────────
    PHLOG_INFO(Render, "Starting: chart_end=" << chart_end << "s"
        << " playable=" << chart.playable_count
        << " scoring_notes=" << scoring_notes);

    AppContext ctx;
    ctx.init(args.chart_path, chart.offset,
             args.respack_path, args.bg_path, args.font_path, args.audio_path,
             args.headless, W, H, cfg,
             /*no_vsync=*/!args.record_output.empty(),
             chart.metadata);

    try {
        GameLoop gl(ctx, args, cfg, chart, scoring_notes, chart_end);

#ifdef PHIGROS_WASM
        emscripten_set_main_loop_arg(GameLoop::wasm_tick, &gl, 0, 1);
#else
        while (gl.run_frame()) {}
#endif

        PHLOG_INFO(Engine, "Main loop finished: quit_requested=" << ctx.window.quit_requested);

        // Final results
        auto sr = gl.final_score();
        const char* tag = gl.is_play_mode
            ? (gl.replay_player.enabled() ? "Replay Complete"
               : (gl.is_scriptplay_mode ? "ScriptPlay Complete" : "Play Complete"))
            : "Render Complete";
        // Print final score as plain output (always visible regardless of log level)
        printf("\n=== %s ===\nScore: %d\nAccuracy: %.4f%%\nMaxCombo: %d/%d\nJudged: %d/%d\n",
               tag, sr.score, sr.acc_ratio * 100.0,
               gl.judge.max_combo, scoring_notes,
               gl.judge.judged_cnt, scoring_notes);
        PHLOG_DEBUG(Engine, "acc_sum=" << gl.judge.acc_sum
            << " judged=" << gl.judge.judged_cnt << "/" << scoring_notes
            << " max_combo=" << gl.judge.max_combo);

        gl.finish();
        ctx.destroy();
        return (sr.score == 1000000) ? 0 : 1;
    } catch (const std::exception& e) {
        PHLOG_FATAL(Engine, e.what());
        ctx.destroy();
        return 1;
    }
}
