#pragma once
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
    double      duration            = 0.0;
    bool        headless            = false;
    bool        score_only          = false;
    bool        benchmark           = false;
    int         benchmark_iterations = 10;
    bool        play_mode           = false;
    std::string save_replay_path;
    std::string play_replay_path;
    std::string backend             = "sdl";
    double      audio_offset_ms     = 0.0;
    // Recording
    std::string record_output;
    std::string record_preset       = "balanced";
    std::string record_codec;
    double      record_fps          = 60.0;
    int         record_w            = 0, record_h = 0;
    double      record_start        = -1.0;
    double      record_end          = 0.0;
    // Compile
    std::string compile_output;
    float       compile_sample_rate = 240.0f;
    // Mods (applied to chart after load, in order)
    std::vector<std::string> mod_paths; // --mod <file.mod.json>, repeatable
    // Info / utility
    bool        version_mode        = false; // --version
    bool        info_mode           = false; // --info  : print chart metadata and exit
    std::string list_charts_dir;             // --list-charts <dir>
    // Window overrides (take precedence over config)
    int         window_w            = 0;
    int         window_h            = 0;
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
        else if (a == "--duration"  && i+1 < argc) args.duration      = std::atof(argv[++i]);
        else if (a == "--audio-offset" && i+1 < argc) args.audio_offset_ms = std::atof(argv[++i]);
        else if (a == "--width"     && i+1 < argc) args.window_w      = std::atoi(argv[++i]);
        else if (a == "--height"    && i+1 < argc) args.window_h      = std::atoi(argv[++i]);
        else if (a == "--headless")    args.headless = true;
        else if (a == "--score-only")  { args.score_only = true; args.headless = true; }
        else if (a == "--benchmark")   { args.benchmark = true; args.score_only = true; args.headless = true; }
        else if (a == "--benchmark-iterations" && i+1 < argc) args.benchmark_iterations = std::atoi(argv[++i]);
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
        else if (a == "--record-fps"        && i+1 < argc) args.record_fps    = std::atof(argv[++i]);
        else if (a == "--record-resolution" && i+1 < argc) {
            std::string res = argv[++i];
            auto x = res.find('x');
            if (x != std::string::npos) {
                args.record_w = std::atoi(res.substr(0, x).c_str());
                args.record_h = std::atoi(res.substr(x + 1).c_str());
            }
        }
        else if (a == "--record-start"  && i+1 < argc) args.record_start = std::atof(argv[++i]);
        else if (a == "--record-end"    && i+1 < argc) args.record_end   = std::atof(argv[++i]);
        else if (a == "--compile"       && i+1 < argc) { args.compile_output = argv[++i]; args.headless = true; }
        else if (a == "--sample-rate"   && i+1 < argc) args.compile_sample_rate = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--mod"           && i+1 < argc) args.mod_paths.push_back(argv[++i]);
        else if (a == "--list-charts"   && i+1 < argc) { args.list_charts_dir = argv[++i]; args.headless = true; }
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
        "  --audio-offset <ms>       Audio latency compensation\n"
        "  --width <px>              Window width  (overrides config)\n"
        "  --height <px>             Window height (overrides config)\n"
        "\n"
        "COMPILE\n"
        "  --compile <out.phbc>      Compile chart to binary and exit\n"
        "  --sample-rate <Hz>        Sampling rate for --compile (default 240)\n"
        "\n"
        "MODS\n"
        "  --mod <file.mod.json>     Apply a mod to the chart (repeatable, applied in order)\n"
        "                            See mods/ directory for examples and scripts/new_mod.py\n"
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
        "  --info    (-i)            Print chart metadata and exit\n"
        "  --list-charts <dir>       Discover and list all charts under dir\n"
        "\n"
        "RECORDING\n"
        "  --record <output.mp4>     Record video (headless)\n"
        "  --record-preset <name>    fast|balanced|quality|archive\n"
        "  --record-codec <codec>    libx264|libx265|libvpx-vp9\n"
        "  --record-fps <fps>        Recording framerate (default 60)\n"
        "  --record-resolution WxH   Recording resolution\n"
        "  --record-start <sec>      Start recording at time\n"
        "  --record-end <sec>        Stop recording at time\n"
        "\n"
        "REPLAY\n"
        "  --save-replay <file.rep>  Save replay from --play session\n"
        "  --play-replay <file.rep>  Play back a saved replay\n"
        "\n"
        "OTHER\n"
        "  --headless                No visible window\n"
        "  --screenshot-dir <dir>    Save PNG screenshots every 5 s\n"
        "  --backend <name>          Renderer backend (sdl)\n"
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

} // namespace phigros::app
