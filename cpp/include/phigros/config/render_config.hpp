#pragma once
#include <string>
#include <optional>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace phigros::config {

struct RenderConfig {
    // Window
    int window_w = 1280;
    int window_h = 720;

    // Visual settings
    double expand_factor = 1.0;
    double note_scale_x = 2.5;
    double note_scale_y = 1.0;
    double note_flow_speed_multiplier = 1.0;
    bool note_speed_mul_affects_travel = false;

    // Line settings
    std::optional<double> force_line_alpha01;
    std::optional<std::unordered_map<int, double>> force_line_alpha01_by_lid;

    // Rendering
    double approach = 3.0;
    double chart_speed = 1.0;
    bool no_cull = false;
    bool no_cull_screen = false;
    bool no_cull_enter_time = true;
    double overrender = 1.0;
    bool note_outline = false;
    std::string line_alpha_affects_notes = "negative_only";

    // Trail effect
    std::optional<double> trail_alpha;
    std::optional<int> trail_frames;
    std::optional<double> trail_decay;
    std::optional<int> trail_blur;
    std::optional<int> trail_dim;
    std::optional<bool> trail_blur_ramp;
    std::optional<std::string> trail_blend;

    // Motion blur
    std::optional<int> motion_blur_samples;
    std::optional<double> motion_blur_shutter;

    // Assets
    std::string respack_path = "./respack.zip";
    std::string bg_path;
    int bg_blur = 10;
    int bg_dim = 120;

    // Gameplay
    bool autoplay = true;
    double hold_tail_tol = 0.8;
    int hold_fx_interval_ms = 200;

    // RPE
    int rpe_easing_shift = 0;

    // Debug
    bool basic_debug = false;

    // Backend
    std::string backend = "sdl3_bgfx";
};

// Strip // and # comments from JSONC
inline std::string strip_jsonc_comments(const std::string& src) {
    std::istringstream in(src);
    std::string out, line;
    while (std::getline(in, line)) {
        // Remove // comments (not inside strings — simplified)
        bool in_str = false;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '"' && (i == 0 || line[i - 1] != '\\'))
                in_str = !in_str;
            if (!in_str && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
                line = line.substr(0, i);
                break;
            }
        }
        out += line + '\n';
    }
    return out;
}

inline RenderConfig load_config(const std::string& path) {
    std::ifstream f(path);
    std::string text((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    auto j = nlohmann::json::parse(strip_jsonc_comments(text));

    RenderConfig cfg;

    if (j.contains("window")) {
        auto& w = j["window"];
        if (w.contains("w")) cfg.window_w = w["w"].get<int>();
        if (w.contains("h")) cfg.window_h = w["h"].get<int>();
    }

    if (j.contains("render")) {
        auto& r = j["render"];
        auto get_d = [&](const char* k, double& v) { if (r.contains(k) && !r[k].is_null()) v = r[k].get<double>(); };
        auto get_b = [&](const char* k, bool& v) { if (r.contains(k) && !r[k].is_null()) v = r[k].get<bool>(); };
        auto get_s = [&](const char* k, std::string& v) { if (r.contains(k) && !r[k].is_null()) v = r[k].get<std::string>(); };

        get_d("approach", cfg.approach);
        get_d("chart_speed", cfg.chart_speed);
        get_b("no_cull", cfg.no_cull);
        get_b("no_cull_screen", cfg.no_cull_screen);
        get_b("no_cull_enter_time", cfg.no_cull_enter_time);
        get_d("expand", cfg.expand_factor);
        get_d("note_scale_x", cfg.note_scale_x);
        get_d("note_scale_y", cfg.note_scale_y);
        get_d("note_flow_speed_multiplier", cfg.note_flow_speed_multiplier);
        get_d("overrender", cfg.overrender);
        get_b("note_outline", cfg.note_outline);
        get_s("line_alpha_affects_notes", cfg.line_alpha_affects_notes);

        if (r.contains("trail_alpha") && !r["trail_alpha"].is_null()) cfg.trail_alpha = r["trail_alpha"].get<double>();
        if (r.contains("trail_frames") && !r["trail_frames"].is_null()) cfg.trail_frames = r["trail_frames"].get<int>();
        if (r.contains("trail_decay") && !r["trail_decay"].is_null()) cfg.trail_decay = r["trail_decay"].get<double>();
        if (r.contains("trail_blur") && !r["trail_blur"].is_null()) cfg.trail_blur = r["trail_blur"].get<int>();
        if (r.contains("trail_dim") && !r["trail_dim"].is_null()) cfg.trail_dim = r["trail_dim"].get<int>();
        if (r.contains("trail_blur_ramp") && !r["trail_blur_ramp"].is_null()) cfg.trail_blur_ramp = r["trail_blur_ramp"].get<bool>();
        if (r.contains("trail_blend") && !r["trail_blend"].is_null()) cfg.trail_blend = r["trail_blend"].get<std::string>();
        if (r.contains("motion_blur_samples") && !r["motion_blur_samples"].is_null()) cfg.motion_blur_samples = r["motion_blur_samples"].get<int>();
        if (r.contains("motion_blur_shutter") && !r["motion_blur_shutter"].is_null()) cfg.motion_blur_shutter = r["motion_blur_shutter"].get<double>();
    }

    if (j.contains("assets")) {
        auto& a = j["assets"];
        if (a.contains("respack") && !a["respack"].is_null()) cfg.respack_path = a["respack"].get<std::string>();
        if (a.contains("bg") && !a["bg"].is_null()) cfg.bg_path = a["bg"].get<std::string>();
        if (a.contains("bg_blur") && !a["bg_blur"].is_null()) cfg.bg_blur = a["bg_blur"].get<int>();
        if (a.contains("bg_dim") && !a["bg_dim"].is_null()) cfg.bg_dim = a["bg_dim"].get<int>();
    }

    if (j.contains("gameplay")) {
        auto& g = j["gameplay"];
        if (g.contains("autoplay") && !g["autoplay"].is_null()) cfg.autoplay = g["autoplay"].get<bool>();
        if (g.contains("hold_tail_tol") && !g["hold_tail_tol"].is_null()) cfg.hold_tail_tol = g["hold_tail_tol"].get<double>();
        if (g.contains("hold_fx_interval_ms") && !g["hold_fx_interval_ms"].is_null()) cfg.hold_fx_interval_ms = g["hold_fx_interval_ms"].get<int>();
    }

    if (j.contains("rpe")) {
        auto& rr = j["rpe"];
        if (rr.contains("rpe_easing_shift") && !rr["rpe_easing_shift"].is_null()) cfg.rpe_easing_shift = rr["rpe_easing_shift"].get<int>();
    }

    if (j.contains("debug")) {
        auto& d = j["debug"];
        if (d.contains("basic_debug") && !d["basic_debug"].is_null()) cfg.basic_debug = d["basic_debug"].get<bool>();
    }

    return cfg;
}

} // namespace phigros::config
