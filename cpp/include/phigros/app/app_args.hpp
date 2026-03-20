#pragma once
#include "phigros/core/logger.hpp"
#include "phigros/app/debug_flags.hpp"
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>

#define PHIGROS_VERSION "1.0.0-dev"

namespace phigros::app {

struct AppArgs {
    std::string chart_path;
    std::string config_path;
    std::string respack_path;
    std::string bg_path;
    std::string font_path;
    std::string audio_path;
    std::string screenshot_dir;
    double      screenshot_fps      = 0.2;   // screenshots per chart-second (default 1/5s)
    double      duration            = 0.0;
    bool        truncate_at_duration = false;
    bool        headless            = false;
    bool        score_only          = false;
    bool        benchmark           = false;
    int         benchmark_iterations = 10;
    bool        play_mode           = false;
    bool        profile             = false;  // --profile: print per-phase frame timings
    std::string save_replay_path;
    std::string play_replay_path;
    std::string backend;
    double      audio_offset_ms     = 0.0;
    // Recording
    std::string record_output;
    std::string record_preset       = "balanced";
    std::string record_codec;
    std::string record_hw;
    double      record_fps          = 60.0;
    int         record_w            = 0, record_h = 0;
    int         record_capture_w    = 0, record_capture_h = 0;
    int         record_queue_depth  = 6;
    double      record_start        = -1.0;
    double      record_end          = 0.0;
    double      sim_fps             = 240.0; // internal simulation sampling rate
    // Compile
    std::string compile_output;
    float       compile_sample_rate = 240.0f;
    bool        compile_compress    = false;
    std::string compile_compress_algo; // "zlib" or "lzma" (default: zlib)
    bool        compile_encrypt     = false;
    std::string compile_encrypt_algo;  // "aes-gcm", "aes-cbc", "chacha20", "xor" (default: aes-gcm)
    std::string password;              // for --encrypt / loading encrypted .phbc
    // Mods (applied to chart after load, in order)
    std::vector<std::string> mod_paths; // --mod <file.mod.json>, repeatable
    // Chart script (declarative playlist DSL)
    std::string script_path;             // --script <file.chartscript.json>
    // Info / utility
    bool        version_mode        = false; // --version
    bool        info_mode           = false; // --info  : print chart metadata and exit
    std::string list_charts_dir;             // --list-charts <dir>
    // Window overrides (take precedence over config)
    int         window_w            = 0;
    int         window_h            = 0;
    // ── Render preference overrides (take precedence over config) ──────────────
    // Use negative sentinel to detect "not set on command line".
    double      approach            = -1.0;  // --approach <sec>
    double      chart_speed         = -1.0;  // --chart-speed <mul>
    double      expand_factor       = -1.0;  // --expand <factor>
    double      note_scale_x        = -1.0;  // --note-scale-x <mul>
    double      note_scale_y        = -1.0;  // --note-scale-y <mul>
    double      note_alpha          = -1.0;  // --note-alpha <0-1>
    DebugFlag   debug_flags         = DebugFlag::NONE; // --debug-flags A|B|C
    // ── Logging ────────────────────────────────────────────────────────────────
    std::string log_level;    // --log-level trace|debug|info|warn|error|fatal|off
    std::string log_filter;   // --log-filter chart,render,audio,…  (comma-separated)
    std::string log_file;     // --log-file <path>  — write copy to file
    bool        log_no_color  = false; // --log-no-color
    bool        log_time      = false; // --log-time  — prepend HH:MM:SS
    bool        log_trace     = false; // --trace    alias for --log-level trace
    bool        log_verbose   = false; // --verbose  alias for --log-level debug
    bool        log_quiet     = false; // --quiet    alias for --log-level warn
};

inline AppArgs parse_args(int argc, char* argv[]) {
    AppArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--version" || a == "-v")    { args.version_mode = true; }
        else if (a == "--info"    || a == "-i")    { args.info_mode = true; args.headless = true; }
        else if (a == "--help"    || a == "-h")    { /* handled in main */ }
        else if (a == "--config"    && i+1 < argc) args.config_path   = argv[++i];
        else if (a == "--respack"   && i+1 < argc) args.respack_path  = argv[++i];
        else if (a == "--bg"        && i+1 < argc) args.bg_path       = argv[++i];
        else if (a == "--font"      && i+1 < argc) args.font_path     = argv[++i];
        else if (a == "--audio"     && i+1 < argc) args.audio_path    = argv[++i];
        else if (a == "--screenshot-dir" && i+1 < argc) args.screenshot_dir = argv[++i];
        else if (a == "--screenshot-fps" && i+1 < argc) args.screenshot_fps = std::atof(argv[++i]);
        else if (a == "--duration"  && i+1 < argc) args.duration      = std::atof(argv[++i]);
        else if (a == "--truncate-at-duration") args.truncate_at_duration = true;
        else if (a == "--audio-offset" && i+1 < argc) args.audio_offset_ms = std::atof(argv[++i]);
        else if (a == "--width"     && i+1 < argc) args.window_w      = std::atoi(argv[++i]);
        else if (a == "--height"    && i+1 < argc) args.window_h      = std::atoi(argv[++i]);
        else if (a == "--approach"      && i+1 < argc) args.approach      = std::atof(argv[++i]);
        else if (a == "--chart-speed"   && i+1 < argc) args.chart_speed   = std::atof(argv[++i]);
        else if (a == "--expand"        && i+1 < argc) args.expand_factor = std::atof(argv[++i]);
        else if (a == "--note-scale-x"  && i+1 < argc) args.note_scale_x  = std::atof(argv[++i]);
        else if (a == "--note-scale-y"  && i+1 < argc) args.note_scale_y  = std::atof(argv[++i]);
        else if (a == "--note-alpha"    && i+1 < argc) args.note_alpha     = std::atof(argv[++i]);
        else if (a.rfind("--debug-flags=", 0) == 0 || a.rfind("--debug_flags=", 0) == 0) {
            std::string err;
            auto eq = a.find('=');
            if (!parse_debug_flags(a.substr(eq + 1), args.debug_flags, &err))
                std::fprintf(stderr, "[Args] Warning: %s\n", err.c_str());
        }
        else if ((a == "--debug-flags" || a == "--debug_flags") && i+1 < argc) {
            std::string err;
            if (!parse_debug_flags(argv[++i], args.debug_flags, &err))
                std::fprintf(stderr, "[Args] Warning: %s\n", err.c_str());
        }
        else if (a == "--headless")    args.headless = true;
        else if (a == "--score-only")  { args.score_only = true; args.headless = true; }
        else if (a == "--benchmark")   { args.benchmark = true; args.score_only = true; args.headless = true; }
        else if (a == "--benchmark-iterations" && i+1 < argc) args.benchmark_iterations = std::atoi(argv[++i]);
        else if (a == "--profile")     args.profile = true;
        else if (a == "--play")        args.play_mode = true;
        else if (a == "--save-replay"  && i+1 < argc) args.save_replay_path = argv[++i];
        else if (a == "--play-replay"  && i+1 < argc) args.play_replay_path = argv[++i];
        else if (a == "--backend"      && i+1 < argc) args.backend    = argv[++i];
        else if (a == "--record"       && i+1 < argc) {
            args.record_output = argv[++i];
            args.headless = true;
        }
        else if (a == "--record-preset"     && i+1 < argc) args.record_preset = argv[++i];
        else if (a == "--record-codec"      && i+1 < argc) args.record_codec  = argv[++i];
        else if (a == "--record-hw"         && i+1 < argc) args.record_hw     = argv[++i];
        else if (a == "--record-fps"        && i+1 < argc) args.record_fps    = std::atof(argv[++i]);
        else if (a == "--sim-fps"           && i+1 < argc) args.sim_fps       = std::atof(argv[++i]);
        else if (a == "--record-resolution" && i+1 < argc) {
            std::string res = argv[++i];
            auto x = res.find('x');
            if (x != std::string::npos) {
                args.record_w = std::atoi(res.substr(0, x).c_str());
                args.record_h = std::atoi(res.substr(x + 1).c_str());
            }
        }
        else if (a == "--record-capture-resolution" && i+1 < argc) {
            std::string res = argv[++i];
            auto x = res.find('x');
            if (x != std::string::npos) {
                args.record_capture_w = std::atoi(res.substr(0, x).c_str());
                args.record_capture_h = std::atoi(res.substr(x + 1).c_str());
            }
        }
        else if (a == "--record-queue-depth" && i+1 < argc) {
            args.record_queue_depth = std::atoi(argv[++i]);
        }
        else if (a == "--record-start"  && i+1 < argc) args.record_start = std::atof(argv[++i]);
        else if (a == "--record-end"    && i+1 < argc) args.record_end   = std::atof(argv[++i]);
        else if (a == "--compile"       && i+1 < argc) { args.compile_output = argv[++i]; args.headless = true; }
        else if (a == "--sample-rate"   && i+1 < argc) args.compile_sample_rate = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--compress") {
            args.compile_compress = true;
            if (i+1 < argc && argv[i+1][0] != '-') args.compile_compress_algo = argv[++i];
        }
        else if (a == "--encrypt") {
            args.compile_encrypt = true;
            if (i+1 < argc && argv[i+1][0] != '-') args.compile_encrypt_algo = argv[++i];
        }
        else if (a == "--password"      && i+1 < argc) args.password = argv[++i];
        else if (a == "--mod"           && i+1 < argc) args.mod_paths.push_back(argv[++i]);
        else if (a == "--script"        && i+1 < argc) args.script_path = argv[++i];
        else if (a == "--list-charts"   && i+1 < argc) { args.list_charts_dir = argv[++i]; args.headless = true; }
        // ── Logging flags ────────────────────────────────────────────────────
        else if (a == "--log-level"  && i+1 < argc) args.log_level  = argv[++i];
        else if (a == "--log-filter" && i+1 < argc) args.log_filter = argv[++i];
        else if (a == "--log-file"   && i+1 < argc) args.log_file   = argv[++i];
        else if (a == "--log-no-color")              args.log_no_color = true;
        else if (a == "--log-time")                  args.log_time     = true;
        else if (a == "--trace")                     args.log_trace    = true;
        else if (a == "--verbose")                   args.log_verbose  = true;
        else if (a == "--quiet")                     args.log_quiet    = true;
        else if (args.chart_path.empty()) args.chart_path = a;
    }
    return args;
}

inline void print_usage(const char* prog) {
    printf(
        "Phigros Chart Renderer  v" PHIGROS_VERSION "\n"
        "Usage: %s <chart_path> [options]\n"
        "\n"
        "CHART FORMATS\n"
        "  .json          Official / RPE chart\n"
        "  .pec           PEC legacy chart\n"
        "  .phbc          Pre-compiled binary chart (fastest load)\n"
        "\n"
        "PLAYBACK\n"
        "  --play                    Interactive mode (mouse/touch input)\n"
        "  --score-only              Headless engine scoring (fastest)\n"
        "  --duration <sec>          Auto-quit after N seconds\n"
        "  --truncate-at-duration    Score denominator uses notes fully inside --duration\n"
        "  --audio-offset <ms>       Audio latency compensation\n"
        "  --width <px>              Window width  (overrides config)\n"
        "  --height <px>             Window height (overrides config)\n"
        "\n"
        "RENDER PREFERENCES  (override config file values without editing the config)\n"
        "  --approach <sec>          Approach time in seconds (0.1 – 30, default 3.0)\n"
        "  --chart-speed <mul>       Chart speed multiplier (0.1 – 20, default 1.0)\n"
        "  --expand <factor>         Lane expand factor (>1 compresses, default 1.0)\n"
        "  --note-scale-x <mul>      Note width scale (default 2.5)\n"
        "  --note-scale-y <mul>      Note height scale (default 1.0)\n"
        "  --note-alpha <0-1>        Global note opacity (default 1.0)\n"
        "  --debug-flags <flags>     Pipe/comma-separated debug overlays, e.g.\n"
        "                            FRAME_TIME|AUDIO_INFO|JUDGE_LINE_NUMBER\n"
        "\n"
        "COMPILE\n"
        "  --compile <out.phbc>      Compile chart to binary and exit\n"
        "  --sample-rate <Hz>        Sampling rate for --compile (default 240)\n"
        "  --compress [zlib|lzma]    Compress payload (default: zlib)\n"
        "  --encrypt [aes-gcm|aes-cbc|chacha20|xor]\n"
        "                            Encrypt payload (default: aes-gcm)\n"
        "  --password <passphrase>   Password for encryption/decryption\n"
        "\n"
        "MODS\n"
        "  --mod <file.mod.json>     Apply a mod to the chart (repeatable, applied in order)\n"
        "                            See mods/ directory for examples and scripts/new_mod.py\n"
        "\n"
        "CHART SCRIPT\n"
        "  --script <file.chartscript.json>\n"
        "                            Run a declarative chart playlist script.\n"
        "                            Supports sequence/shuffle/loop modes, per-item config\n"
        "                            overrides, inline mods, filters, and auto-discovery.\n"
        "\n"
        "ASSETS\n"
        "  --config <path>           Render config JSONC file\n"
        "  --respack <path>          Respack ZIP\n"
        "  --bg <path>               Background image\n"
        "  --font <path>             TTF font file\n"
        "  --audio <path>            BGM audio file\n"
        "\n"
        "BENCHMARK / ANALYSIS\n"
        "  --benchmark               Benchmark engine (implies --score-only)\n"
        "  --benchmark-iterations N  Benchmark runs (default 10)\n"
        "  --profile                 Print per-phase frame timing stats every 60 frames\n"
        "  --info    (-i)            Print chart metadata and exit\n"
        "  --list-charts <dir>       Discover and list all charts under dir\n"
        "\n"
        "RECORDING\n"
        "  --record <output.mp4>     Record video (headless)\n"
        "  --record-preset <name>    fast|balanced|quality|archive\n"
        "  --record-codec <codec>    libx264|libx265|libvpx-vp9|h264_nvenc|hevc_nvenc|h264_qsv|h264_vaapi\n"
        "  --record-hw <type>        nvenc|qsv|vaapi|amf|videotoolbox (sets hw codec if --record-codec is empty)\n"
        "  --record-fps <fps>        Recording framerate (default 60)\n"
        "  --sim-fps <fps>           Internal simulation sampling rate (default 240)\n"
        "  --record-resolution WxH   Output video resolution\n"
        "  --record-capture-resolution WxH  Render/readback resolution before encoding\n"
        "  --record-queue-depth N    Async encoder queue depth (<=1 = sync write)\n"
        "  --record-start <sec>      Start recording at time\n"
        "  --record-end <sec>        Stop recording at time\n"
        "\n"
        "REPLAY\n"
        "  --save-replay <file.rep>  Save replay from --play session\n"
        "  --play-replay <file.rep>  Play back a saved replay\n"
        "\n"
        "LOGGING\n"
        "  --trace                   Shorthand for --log-level trace (maximum detail)\n"
        "  --verbose                 Shorthand for --log-level debug\n"
        "  --quiet                   Shorthand for --log-level warn\n"
        "  --log-level <level>       Minimum level: trace|debug|info|warn|error|fatal|off\n"
        "  --log-filter <channels>   Comma-separated channel whitelist, e.g. chart,render,audio\n"
        "                            Channels: general chart render audio record engine input\n"
        "                                      window respack compile chartscript mod profile\n"
        "  --log-file <path>         Write log copy to file in addition to stdout/stderr\n"
        "  --log-no-color            Disable ANSI colour codes in terminal output\n"
        "  --log-time                Prepend HH:MM:SS timestamp to each log line\n"
        "\n"
        "OTHER\n"
        "  --headless                No visible window\n"
        "  --screenshot-dir <dir>    Save PNG screenshots periodically\n"
        "  --screenshot-fps <fps>    Screenshot rate in chart-seconds (default 0.2 = every 5s)\n"
        "  --backend <name>          Renderer backend (sdl|sdl_hw|sdl_sw)\n"
        "  --version  (-v)           Print version and exit\n"
        "  --help     (-h)           Print this help and exit\n"
        "\n"
        "KEYBINDINGS (desktop)\n"
        "  SPACE  Pause / resume\n"
        "  R      Restart chart\n"
        "  ESC    Quit\n",
        prog
    );
}

/// Apply AppArgs logging flags to the global Logger singleton.
/// Call this immediately after parse_args() before any other output.
inline void init_logger(const AppArgs& args) {
    auto& log = phigros::core::Logger::get();

    // Explicit level flag takes priority; --verbose/--quiet are shorthands
    if (!args.log_level.empty())
        log.set_level(args.log_level);
    else if (args.log_trace)
        log.set_level(phigros::core::LogLevel::Trace);
    else if (args.log_verbose)
        log.set_level(phigros::core::LogLevel::Debug);
    else if (args.log_quiet)
        log.set_level(phigros::core::LogLevel::Warn);

    if (!args.log_filter.empty())
        log.set_channel_filter(args.log_filter);

    if (args.log_no_color)
        log.use_color = false;

    if (args.log_time)
        log.show_time = true;

    if (!args.log_file.empty()) {
        if (!log.open_file(args.log_file))
            fprintf(stderr, "[Logger] Warning: could not open log file: %s\n",
                    args.log_file.c_str());
    }

    // Emit logger configuration once the selected sinks are in place.
    PHLOG_TRACE(General, "Logger sink setup: color=" << log.use_color
        << " time=" << log.show_time
        << " channel=" << log.show_channel);
    PHLOG_DEBUG(General, "Logger init: level="
        << phigros::core::Logger::level_name(log.min_level)
        << (args.log_filter.empty() ? "" : " filter=" + args.log_filter)
        << (args.log_file.empty()   ? "" : " file=" + args.log_file));
}

} // namespace phigros::app
