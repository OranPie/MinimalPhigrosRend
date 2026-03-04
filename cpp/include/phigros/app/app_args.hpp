#pragma once
#include <string>
#include <cstdlib>

namespace phigros::app {

struct AppArgs {
    std::string chart_path;
    std::string config_path;
    std::string respack_path;
    std::string bg_path;
    std::string font_path;
    std::string audio_path;
    std::string screenshot_dir;
    double      duration           = 0.0;
    bool        headless           = false;
    bool        score_only         = false;
    bool        benchmark          = false;
    int         benchmark_iterations = 10;
    bool        play_mode          = false;
    std::string save_replay_path;
    std::string play_replay_path;
    std::string backend            = "sdl";
    double      audio_offset_ms    = 0.0; // CLI override; merged into cfg before use
    // Recording
    std::string record_output;
    std::string record_preset      = "balanced";
    std::string record_codec;
    double      record_fps         = 60.0;
    int         record_w           = 0, record_h = 0;
    double      record_start       = -1.0;
    double      record_end         = 0.0;
};

inline AppArgs parse_args(int argc, char* argv[]) {
    AppArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--config"    && i+1 < argc) args.config_path   = argv[++i];
        else if (a == "--respack"   && i+1 < argc) args.respack_path  = argv[++i];
        else if (a == "--bg"        && i+1 < argc) args.bg_path       = argv[++i];
        else if (a == "--font"      && i+1 < argc) args.font_path     = argv[++i];
        else if (a == "--audio"     && i+1 < argc) args.audio_path    = argv[++i];
        else if (a == "--screenshot-dir" && i+1 < argc) args.screenshot_dir = argv[++i];
        else if (a == "--duration"  && i+1 < argc) args.duration      = std::atof(argv[++i]);
        else if (a == "--audio-offset" && i+1 < argc) args.audio_offset_ms = std::atof(argv[++i]);
        else if (a == "--headless")   args.headless = true;
        else if (a == "--score-only") { args.score_only = true; args.headless = true; }
        else if (a == "--benchmark")  { args.benchmark = true; args.score_only = true; args.headless = true; }
        else if (a == "--benchmark-iterations" && i+1 < argc) args.benchmark_iterations = std::atoi(argv[++i]);
        else if (a == "--play")       args.play_mode = true;
        else if (a == "--save-replay" && i+1 < argc) args.save_replay_path = argv[++i];
        else if (a == "--play-replay" && i+1 < argc) args.play_replay_path = argv[++i];
        else if (a == "--backend"    && i+1 < argc) args.backend      = argv[++i];
        else if (a == "--record"     && i+1 < argc) {
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
        else if (a == "--record-start" && i+1 < argc) args.record_start = std::atof(argv[++i]);
        else if (a == "--record-end"   && i+1 < argc) args.record_end   = std::atof(argv[++i]);
        else if (args.chart_path.empty()) args.chart_path = a;
    }
    return args;
}

inline void print_usage(const char* prog) {
    (void)prog;
    printf(
        "Usage: phigros_render <chart_path> [options]\n"
        "  --config <path>           Render config JSONC\n"
        "  --respack <path>          Respack ZIP file\n"
        "  --bg <path>               Background image\n"
        "  --font <path>             TTF font file\n"
        "  --audio <path>            BGM audio file\n"
        "  --audio-offset <ms>       Audio latency compensation (ms, positive=advance notes)\n"
        "  --screenshot-dir <dir>    Save frames as PNG every 5s\n"
        "  --duration <sec>          Auto-quit after N seconds\n"
        "  --headless                No visible window\n"
        "  --score-only              Engine-only scoring (fastest)\n"
        "  --benchmark               Benchmark engine performance\n"
        "  --benchmark-iterations N  Number of benchmark runs (default 10)\n"
        "  --play                    Interactive mode (mouse/touch input)\n"
        "  --save-replay <file.rep>  Save replay from --play session\n"
        "  --play-replay <file.rep>  Replay a saved replay file\n"
        "  --record <output.mp4>     Record video\n"
        "  --record-preset <name>    fast|balanced|quality|archive\n"
        "  --record-codec <codec>    libx264|libx265|libvpx-vp9\n"
        "  --record-fps <fps>        Recording framerate (default 60)\n"
        "  --record-resolution WxH   Recording resolution\n"
        "  --record-start <sec>      Start recording at time\n"
        "  --record-end <sec>        Stop recording at time\n"
    );
}

} // namespace phigros::app
