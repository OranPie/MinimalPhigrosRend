#pragma once
#include <string>
#include <optional>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace phigros::config {

// How judge-line alpha modulates visible note alpha.
enum class LineAlphaMode : uint8_t {
    Off,          // never affects notes
    NegativeOnly, // dims notes only when line alpha < 0.5 (default Phigros behaviour)
    Always        // always multiplies note alpha by line alpha
};

struct RenderConfig {
    struct SimulatePlayConfig {
        bool enabled = false;
        std::string mode = "aggressive";
        int max_pointers = 2;
        double jitter_ms = 12.0;
        bool render_pointer = true;
        bool render_trail = true;
        double trail_seconds = 0.16;
        double cursor_radius_px = 20.0;
    };

    // Window
    int window_w = 1280;
    int window_h = 720;

    // Visual settings
    double expand_factor = 1.0;
    double note_scale_x = 2.5;
    double note_scale_y = 1.0;
    double note_flow_speed_multiplier = 1.0;
    bool note_speed_mul_affects_travel = false;
    double note_alpha = 1.0;        // Global note alpha multiplier [0,1]

    // Line settings
    std::optional<double> force_line_alpha01;
    std::optional<std::unordered_map<int, double>> force_line_alpha01_by_lid;
    LineAlphaMode line_alpha_mode = LineAlphaMode::NegativeOnly;

    // Rendering
    double approach = 3.0;
    double chart_speed = 1.0;
    bool no_cull = false;
    bool no_cull_screen = false;
    bool no_cull_enter_time = true;
    double overrender = 1.0;
    bool note_outline = false;

    // Hit effects
    bool show_hitfx = true;
    bool show_particles = true;
    int  particle_count = 8;         // particles per hit burst
    double hitfx_intensity = 1.0;    // alpha multiplier for all hit effects [0,1]
    bool hitfx_effect_apply = true;  // true: hitfx participates in trail/motion blur

    // Trail effect
    std::optional<double> trail_alpha;
    std::optional<int> trail_frames;
    std::optional<double> trail_decay;
    std::optional<int> trail_blur;
    std::optional<int> trail_dim;
    std::optional<bool> trail_blur_ramp;
    std::optional<std::string> trail_blend;
    // Enhanced trail visuals
    std::optional<int> trail_blur_quality;       // downscale passes for blur (1-4, default 2)
    std::optional<double> trail_chromatic;       // chromatic offset in px per age (0=off)
    std::optional<std::string> trail_decay_curve; // "exponential" (default) or "gaussian"
    std::optional<double> trail_glow;            // additive glow intensity (0=off, 0-1)

    // Motion blur
    std::optional<int> motion_blur_samples;
    std::optional<double> motion_blur_shutter;
    std::optional<std::string> motion_blur_curve; // "uniform" (default) or "gaussian"

    // Assets
    std::string respack_path = "./respack.zip";
    std::string bg_path;
    int bg_blur = 10;
    int bg_dim = 120;

    // Gameplay
    bool autoplay = true;
    double hold_tail_tol = 0.8;
    int hold_fx_interval_ms = 200;
    double audio_offset_ms = 0.0; // positive = advance notes relative to audio
    SimulatePlayConfig simulateplay;

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
        auto get_i = [&](const char* k, int& v) { if (r.contains(k) && !r[k].is_null()) v = r[k].get<int>(); };

        get_d("approach", cfg.approach);
        get_d("chart_speed", cfg.chart_speed);
        get_b("no_cull", cfg.no_cull);
        get_b("no_cull_screen", cfg.no_cull_screen);
        get_b("no_cull_enter_time", cfg.no_cull_enter_time);
        get_d("expand", cfg.expand_factor);
        get_d("note_scale_x", cfg.note_scale_x);
        get_d("note_scale_y", cfg.note_scale_y);
        get_d("note_flow_speed_multiplier", cfg.note_flow_speed_multiplier);
        get_d("note_alpha", cfg.note_alpha);
        get_d("overrender", cfg.overrender);
        get_b("note_outline", cfg.note_outline);
        get_b("show_hitfx", cfg.show_hitfx);
        get_b("show_particles", cfg.show_particles);
        get_i("particle_count", cfg.particle_count);
        get_d("hitfx_intensity", cfg.hitfx_intensity);
        get_b("hitfx_effect_apply", cfg.hitfx_effect_apply);
        get_s("backend", cfg.backend);

        if (r.contains("line_alpha_affects_notes") && !r["line_alpha_affects_notes"].is_null()) {
            std::string s = r["line_alpha_affects_notes"].get<std::string>();
            if (s == "always")   cfg.line_alpha_mode = LineAlphaMode::Always;
            else if (s == "off") cfg.line_alpha_mode = LineAlphaMode::Off;
            else                 cfg.line_alpha_mode = LineAlphaMode::NegativeOnly;
        }

        // Clamp to sane ranges
        cfg.approach       = std::max(0.1, std::min(30.0, cfg.approach));
        cfg.chart_speed    = std::max(0.1, std::min(20.0, cfg.chart_speed));
        cfg.note_alpha     = std::max(0.0, std::min(1.0,  cfg.note_alpha));
        cfg.hitfx_intensity= std::max(0.0, std::min(2.0,  cfg.hitfx_intensity));
        cfg.particle_count = std::max(0,   std::min(64,   cfg.particle_count));

        if (r.contains("trail_alpha") && !r["trail_alpha"].is_null()) cfg.trail_alpha = r["trail_alpha"].get<double>();
        if (r.contains("trail_frames") && !r["trail_frames"].is_null()) cfg.trail_frames = r["trail_frames"].get<int>();
        if (r.contains("trail_decay") && !r["trail_decay"].is_null()) cfg.trail_decay = r["trail_decay"].get<double>();
        if (r.contains("trail_blur") && !r["trail_blur"].is_null()) cfg.trail_blur = r["trail_blur"].get<int>();
        if (r.contains("trail_dim") && !r["trail_dim"].is_null()) cfg.trail_dim = r["trail_dim"].get<int>();
        if (r.contains("trail_blur_ramp") && !r["trail_blur_ramp"].is_null()) cfg.trail_blur_ramp = r["trail_blur_ramp"].get<bool>();
        if (r.contains("trail_blend") && !r["trail_blend"].is_null()) cfg.trail_blend = r["trail_blend"].get<std::string>();
        if (r.contains("trail_blur_quality") && !r["trail_blur_quality"].is_null()) cfg.trail_blur_quality = r["trail_blur_quality"].get<int>();
        if (r.contains("trail_chromatic") && !r["trail_chromatic"].is_null()) cfg.trail_chromatic = r["trail_chromatic"].get<double>();
        if (r.contains("trail_decay_curve") && !r["trail_decay_curve"].is_null()) cfg.trail_decay_curve = r["trail_decay_curve"].get<std::string>();
        if (r.contains("trail_glow") && !r["trail_glow"].is_null()) cfg.trail_glow = r["trail_glow"].get<double>();
        if (r.contains("motion_blur_samples") && !r["motion_blur_samples"].is_null()) cfg.motion_blur_samples = r["motion_blur_samples"].get<int>();
        if (r.contains("motion_blur_shutter") && !r["motion_blur_shutter"].is_null()) cfg.motion_blur_shutter = r["motion_blur_shutter"].get<double>();
        if (r.contains("motion_blur_curve") && !r["motion_blur_curve"].is_null()) cfg.motion_blur_curve = r["motion_blur_curve"].get<std::string>();
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
        if (g.contains("audio_offset_ms") && !g["audio_offset_ms"].is_null()) cfg.audio_offset_ms = g["audio_offset_ms"].get<double>();
        if (g.contains("simulateplay") && g["simulateplay"].is_object()) {
            auto& s = g["simulateplay"];
            if (s.contains("enabled") && !s["enabled"].is_null()) cfg.simulateplay.enabled = s["enabled"].get<bool>();
            if (s.contains("mode") && !s["mode"].is_null()) cfg.simulateplay.mode = s["mode"].get<std::string>();
            if (s.contains("max_pointers") && !s["max_pointers"].is_null()) cfg.simulateplay.max_pointers = s["max_pointers"].get<int>();
            if (s.contains("jitter_ms") && !s["jitter_ms"].is_null()) cfg.simulateplay.jitter_ms = s["jitter_ms"].get<double>();
            if (s.contains("render_pointer") && !s["render_pointer"].is_null()) cfg.simulateplay.render_pointer = s["render_pointer"].get<bool>();
            if (s.contains("render_trail") && !s["render_trail"].is_null()) cfg.simulateplay.render_trail = s["render_trail"].get<bool>();
            if (s.contains("trail_seconds") && !s["trail_seconds"].is_null()) cfg.simulateplay.trail_seconds = s["trail_seconds"].get<double>();
            if (s.contains("cursor_radius_px") && !s["cursor_radius_px"].is_null()) cfg.simulateplay.cursor_radius_px = s["cursor_radius_px"].get<double>();
            cfg.simulateplay.max_pointers = std::max(1, std::min(8, cfg.simulateplay.max_pointers));
            cfg.simulateplay.jitter_ms = std::max(0.0, std::min(80.0, cfg.simulateplay.jitter_ms));
            cfg.simulateplay.trail_seconds = std::max(0.02, std::min(1.0, cfg.simulateplay.trail_seconds));
            cfg.simulateplay.cursor_radius_px = std::max(4.0, std::min(80.0, cfg.simulateplay.cursor_radius_px));
        }
    }

    if (j.contains("rpe")) {
        auto& rr = j["rpe"];
        if (rr.contains("rpe_easing_shift") && !rr["rpe_easing_shift"].is_null()) cfg.rpe_easing_shift = rr["rpe_easing_shift"].get<int>();
    }

    if (j.contains("debug")) {
        auto& d = j["debug"];
        if (d.contains("basic_debug") && !d["basic_debug"].is_null()) cfg.basic_debug = d["basic_debug"].get<bool>();
    }

    if (j.contains("backend") && !j["backend"].is_null())
        cfg.backend = j["backend"].get<std::string>();

    return cfg;
}

// Serialize a RenderConfig back to a JSON object.
inline nlohmann::json config_to_json(const RenderConfig& cfg) {
    using json = nlohmann::json;
    json j;

    j["window"]["w"] = cfg.window_w;
    j["window"]["h"] = cfg.window_h;

    auto& r = j["render"];
    r["approach"]                   = cfg.approach;
    r["chart_speed"]                = cfg.chart_speed;
    r["expand"]                     = cfg.expand_factor;
    r["note_scale_x"]               = cfg.note_scale_x;
    r["note_scale_y"]               = cfg.note_scale_y;
    r["note_flow_speed_multiplier"] = cfg.note_flow_speed_multiplier;
    r["note_alpha"]                 = cfg.note_alpha;
    r["note_outline"]               = cfg.note_outline;
    r["no_cull"]                    = cfg.no_cull;
    r["no_cull_screen"]             = cfg.no_cull_screen;
    r["overrender"]                 = cfg.overrender;
    r["show_hitfx"]                 = cfg.show_hitfx;
    r["show_particles"]             = cfg.show_particles;
    r["particle_count"]             = cfg.particle_count;
    r["hitfx_intensity"]            = cfg.hitfx_intensity;
    r["hitfx_effect_apply"]         = cfg.hitfx_effect_apply;
    switch (cfg.line_alpha_mode) {
        case LineAlphaMode::Always:  r["line_alpha_affects_notes"] = "always"; break;
        case LineAlphaMode::Off:     r["line_alpha_affects_notes"] = "off"; break;
        default:                     r["line_alpha_affects_notes"] = "negative_only"; break;
    }
    if (cfg.trail_alpha)       r["trail_alpha"]        = *cfg.trail_alpha;
    if (cfg.trail_frames)      r["trail_frames"]       = *cfg.trail_frames;
    if (cfg.trail_decay)       r["trail_decay"]        = *cfg.trail_decay;
    if (cfg.trail_blur)        r["trail_blur"]         = *cfg.trail_blur;
    if (cfg.trail_dim)         r["trail_dim"]          = *cfg.trail_dim;
    if (cfg.trail_blur_ramp)   r["trail_blur_ramp"]    = *cfg.trail_blur_ramp;
    if (cfg.trail_blend)       r["trail_blend"]        = *cfg.trail_blend;
    if (cfg.trail_blur_quality) r["trail_blur_quality"] = *cfg.trail_blur_quality;
    if (cfg.trail_chromatic)   r["trail_chromatic"]    = *cfg.trail_chromatic;
    if (cfg.trail_decay_curve) r["trail_decay_curve"]  = *cfg.trail_decay_curve;
    if (cfg.trail_glow)        r["trail_glow"]         = *cfg.trail_glow;
    if (cfg.motion_blur_samples) r["motion_blur_samples"] = *cfg.motion_blur_samples;
    if (cfg.motion_blur_shutter) r["motion_blur_shutter"] = *cfg.motion_blur_shutter;
    if (cfg.motion_blur_curve) r["motion_blur_curve"]  = *cfg.motion_blur_curve;

    j["assets"]["respack"]  = cfg.respack_path;
    if (!cfg.bg_path.empty()) j["assets"]["bg"] = cfg.bg_path;
    j["assets"]["bg_blur"]  = cfg.bg_blur;
    j["assets"]["bg_dim"]   = cfg.bg_dim;

    j["gameplay"]["autoplay"]          = cfg.autoplay;
    j["gameplay"]["hold_tail_tol"]     = cfg.hold_tail_tol;
    j["gameplay"]["hold_fx_interval_ms"] = cfg.hold_fx_interval_ms;
    j["gameplay"]["audio_offset_ms"]   = cfg.audio_offset_ms;
    j["gameplay"]["simulateplay"] = {
        {"enabled", cfg.simulateplay.enabled},
        {"mode", cfg.simulateplay.mode},
        {"max_pointers", cfg.simulateplay.max_pointers},
        {"jitter_ms", cfg.simulateplay.jitter_ms},
        {"render_pointer", cfg.simulateplay.render_pointer},
        {"render_trail", cfg.simulateplay.render_trail},
        {"trail_seconds", cfg.simulateplay.trail_seconds},
        {"cursor_radius_px", cfg.simulateplay.cursor_radius_px}
    };

    j["rpe"]["rpe_easing_shift"] = cfg.rpe_easing_shift;
    j["debug"]["basic_debug"]    = cfg.basic_debug;
    j["backend"]                 = cfg.backend;

    return j;
}

// Write config to a JSON file (4-space indent).
inline bool save_config(const std::string& path, const RenderConfig& cfg) {
    try {
        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << config_to_json(cfg).dump(4) << "\n";
        return f.good();
    } catch (...) { return false; }
}

} // namespace phigros::config
