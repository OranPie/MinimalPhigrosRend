#include "phic/core/engine.hpp"
#include "phic/core/mod_config_json.hpp"
#include "phic/core/parser.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct AppOptions {
    std::string input;
    std::string format;
    std::string config;
    std::string config_old;
    std::string save_config;
    std::string backend = "sdl_gl";
    std::string audio_backend = "sdl";
    std::string respack;
    std::string bg;
    std::string bgm;
    std::string lang;
    std::string line_alpha_affects_notes = "negative_only";
    std::string font_path;

    int w = 1280;
    int h = 720;
    int bg_blur = 10;
    int bg_dim = 120;
    int hitsound_min_interval_ms = 30;
    int hold_fx_interval_ms = 200;
    int trail_blur = 0;
    int trail_dim = 0;
    int rpe_easing_shift = 0;

    double approach = 3.0;
    double chart_speed = 1.0;
    double note_scale_x = 1.0;
    double note_scale_y = 1.0;
    double note_flow_speed_multiplier = 1.0;
    double expand = 1.0;
    double overrender = 2.0;
    double trail_alpha = 0.0;
    double bgm_volume = 0.8;
    double hitfx_scale_mul = 1.0;
    double font_size_multiplier = 1.0;
    double hold_tail_tol = 0.8;
    double judge_width = 0.12;
    double judge_height = 0.06;
    double flick_threshold = 0.02;

    bool autoplay = false;
    bool no_cull = false;
    bool no_cull_screen = false;
    bool no_cull_enter_time = false;
    bool note_outline = false;
    bool no_note_outline = false;
    bool force = false;
    bool quiet = false;
    bool no_color = false;
    bool multicolor_lines = false;
    bool no_title_overlay = false;
    bool advance_seq_overlay = false;
    bool basic_debug = false;
    bool debug_line_label = false;
    bool debug_line_stats = false;
    bool debug_judge_windows = false;
    bool debug_pointer = false;
    bool debug_note_info = false;
    bool debug_particles = false;
    bool hit_debug = false;
    bool judge_events = false;

    double start_time = 0.0;
    bool has_start_time = false;
    double end_time = 0.0;
    bool has_end_time = false;
    double seconds = 10.0;
    bool has_seconds = false;

    // advance + playlist + simulateplay
    std::string advance;
    std::string playlist_script;
    std::string playlist_charts_dir = "charts";
    std::string playlist_switch_mode = "hit";
    std::string playlist_filter_levels;
    std::string playlist_filter_name_contains;
    std::string playlist_start_mode = "fresh";
    std::string simulateplay_mode = "conservative";
    std::string judge_script;
    std::string export_pcc;
    std::string export_pcc_password;
    std::string ipad_bundle_id;
    std::string ipad_udid;
    std::string ipad_device_name = "iPad";
    std::string ipad_appium_server = "http://127.0.0.1:4723";
    std::string ipad_mjpeg_url;

    bool advance_lazy_load = false;
    bool advance_lazy_preload = false;
    bool advance_lazy_scan_total_notes = false;
    bool gui = false;
    bool export_pcc_no_compress = false;
    bool simulateplay = false;
    bool simulateplay_ipad = false;
    bool ipad_reconnect = false;
    bool ipad_activate_app = false;
    bool playlist_no_shuffle = false;

    int advance_lazy_cache = 1;
    int simulateplay_max_pointers = 2;
    int playlist_notes_per_chart = 10;
    int playlist_seed = 0;
    int playlist_filter_min_total_notes = 0;
    int playlist_filter_max_total_notes = 0;
    int playlist_filter_limit = 0;
    int playlist_start_index = 0;
    int playlist_start_from_hit_total = 0;
    int playlist_start_from_combo_total = 0;
    int ipad_max_retries = 2;

    double playlist_tail_time = 0.5;
    double ipad_move_hz = 25.0;
    double ipad_preview_fps = 15.0;
    double ipad_retry_backoff_s = 0.35;
    double ipad_viewport_x0 = 0.0;
    double ipad_viewport_y0 = 0.0;
    double ipad_viewport_x1 = 1.0;
    double ipad_viewport_y1 = 1.0;

    phic::ModConfig mods{};
};

struct RunItem {
    std::string input;
    std::string format;
    std::string label;
    bool has_start_time = false;
    double start_time = 0.0;
    bool has_end_time = false;
    double end_time = 0.0;
    bool has_mods_override = false;
    phic::ModConfig mods_override{};
};

struct SimulateState {
    std::size_t cursor = 0;
    std::unordered_set<int> fired;
    std::vector<double> lane_cooldown_until;
    std::mt19937 rng{12345};
    bool seeded = false;
};

struct GlobalRuntimeOptions {
    std::string respack_path;
    bool has_respack = false;
};

bool path_exists(const std::string& p);

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string infer_format(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return "official";
    }
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (ext == "pec") return "pec";
    if (ext == "rpe") return "rpe";
    return "official";
}

void usage() {
    std::cout
        << "phic_cli --input <chart> [--config <file>] [--format official|rpe|pec] [--autoplay]\n"
        << "         [--simulateplay --simulateplay_mode conservative|aggressive|extreme]\n"
        << "         [--advance <json>] [--playlist_charts_dir <dir>]\n"
        << "         [--mod_transpose S --mod_stretch F --mod_quantize STEP --mod_wave_amp A --mod_stutter_repeat N --mod_stutter_alpha_decay X]\n"
        << "         [--mod_full_blue --mod_lane_scale X --mod_compress_zip_count N]\n"
        << "         [--judge_events]\n";
}

std::string strip_json_comments(const std::string& in) {
    std::string out;
    out.reserve(in.size());

    bool in_string = false;
    bool escape = false;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (in_string) {
            out.push_back(c);
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (c == '"') {
            in_string = true;
            out.push_back(c);
            continue;
        }

        if (c == '/' && i + 1 < in.size()) {
            if (in[i + 1] == '/') {
                i += 2;
                while (i < in.size() && in[i] != '\n') {
                    ++i;
                }
                if (i < in.size()) out.push_back('\n');
                continue;
            }
            if (in[i + 1] == '*') {
                i += 2;
                while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/')) {
                    ++i;
                }
                if (i + 1 < in.size()) ++i;
                continue;
            }
        }

        out.push_back(c);
    }

    return out;
}

std::string strip_trailing_commas(const std::string& in) {
    std::string out;
    out.reserve(in.size());

    bool in_string = false;
    bool escape = false;

    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (in_string) {
            out.push_back(c);
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (c == '"') {
            in_string = true;
            out.push_back(c);
            continue;
        }

        if (c == ',') {
            std::size_t j = i + 1;
            while (j < in.size() && std::isspace(static_cast<unsigned char>(in[j]))) ++j;
            if (j < in.size() && (in[j] == '}' || in[j] == ']')) {
                continue;
            }
        }

        out.push_back(c);
    }

    return out;
}

nlohmann::json parse_json_or_jsonc(const std::string& text) {
    try {
        return nlohmann::json::parse(text);
    } catch (...) {
        const std::string no_comments = strip_json_comments(text);
        const std::string no_trailing = strip_trailing_commas(no_comments);
        return nlohmann::json::parse(no_trailing);
    }
}

bool to_bool(const nlohmann::json& v, bool fallback) {
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number_integer()) return v.get<int>() != 0;
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
        if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    }
    return fallback;
}

double to_num(const nlohmann::json& v, double fallback) {
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) {
        try {
            return std::stod(v.get<std::string>());
        } catch (...) {
        }
    }
    return fallback;
}

int to_int(const nlohmann::json& v, int fallback) {
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_number()) return static_cast<int>(std::lround(v.get<double>()));
    if (v.is_string()) {
        try {
            return std::stoi(v.get<std::string>());
        } catch (...) {
        }
    }
    return fallback;
}

bool parse_bool(const nlohmann::json& j, const char* key, bool fallback) {
    const auto it = j.find(key);
    if (it == j.end()) return fallback;
    return to_bool(*it, fallback);
}

double parse_num(const nlohmann::json& j, const char* key, double fallback) {
    const auto it = j.find(key);
    if (it == j.end()) return fallback;
    return to_num(*it, fallback);
}

int parse_int(const nlohmann::json& j, const char* key, int fallback) {
    const auto it = j.find(key);
    if (it == j.end()) return fallback;
    return to_int(*it, fallback);
}

std::string parse_str(const nlohmann::json& j, const char* key, const std::string& fallback) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}


void apply_mods_from_json_obj(phic::ModConfig& mods, const nlohmann::json& m) {
    phic::merge_mod_config_from_json(mods, m);
}


void apply_config_file(AppOptions& opts) {
    if (opts.config.empty()) return;

    const std::string text = read_file(opts.config);
    if (text.empty()) {
        std::cerr << "warning: failed to read config " << opts.config << "\n";
        return;
    }

    nlohmann::json root;
    try {
        root = parse_json_or_jsonc(text);
    } catch (const std::exception& e) {
        std::cerr << "warning: config parse failed: " << e.what() << "\n";
        return;
    }

    opts.input = parse_str(root, "input", opts.input);
    opts.format = parse_str(root, "format", opts.format);
    opts.backend = parse_str(root, "backend", opts.backend);
    opts.audio_backend = parse_str(root, "audio_backend", opts.audio_backend);
    opts.respack = parse_str(root, "respack", opts.respack);
    opts.bg = parse_str(root, "bg", opts.bg);
    opts.bgm = parse_str(root, "bgm", opts.bgm);
    opts.lang = parse_str(root, "lang", opts.lang);
    opts.line_alpha_affects_notes = parse_str(root, "line_alpha_affects_notes", opts.line_alpha_affects_notes);
    opts.font_path = parse_str(root, "font_path", opts.font_path);

    opts.w = parse_int(root, "w", opts.w);
    opts.h = parse_int(root, "h", opts.h);
    opts.bg_blur = parse_int(root, "bg_blur", opts.bg_blur);
    opts.bg_dim = parse_int(root, "bg_dim", opts.bg_dim);
    opts.hitsound_min_interval_ms = parse_int(root, "hitsound_min_interval_ms", opts.hitsound_min_interval_ms);
    opts.hold_fx_interval_ms = parse_int(root, "hold_fx_interval_ms", opts.hold_fx_interval_ms);
    opts.trail_blur = parse_int(root, "trail_blur", opts.trail_blur);
    opts.trail_dim = parse_int(root, "trail_dim", opts.trail_dim);
    opts.rpe_easing_shift = parse_int(root, "rpe_easing_shift", opts.rpe_easing_shift);

    opts.approach = parse_num(root, "approach", opts.approach);
    opts.chart_speed = parse_num(root, "chart_speed", opts.chart_speed);
    opts.note_scale_x = parse_num(root, "note_scale_x", opts.note_scale_x);
    opts.note_scale_y = parse_num(root, "note_scale_y", opts.note_scale_y);
    opts.note_flow_speed_multiplier = parse_num(root, "note_flow_speed_multiplier", opts.note_flow_speed_multiplier);
    opts.expand = parse_num(root, "expand", opts.expand);
    opts.overrender = parse_num(root, "overrender", opts.overrender);
    opts.trail_alpha = parse_num(root, "trail_alpha", opts.trail_alpha);
    opts.bgm_volume = parse_num(root, "bgm_volume", opts.bgm_volume);
    opts.hitfx_scale_mul = parse_num(root, "hitfx_scale_mul", opts.hitfx_scale_mul);
    opts.font_size_multiplier = parse_num(root, "font_size_multiplier", opts.font_size_multiplier);
    opts.hold_tail_tol = parse_num(root, "hold_tail_tol", opts.hold_tail_tol);
    opts.judge_width = parse_num(root, "judge_width", opts.judge_width);
    opts.judge_height = parse_num(root, "judge_height", opts.judge_height);
    opts.flick_threshold = parse_num(root, "flick_threshold", opts.flick_threshold);

    opts.autoplay = parse_bool(root, "autoplay", opts.autoplay);
    opts.no_cull = parse_bool(root, "no_cull", opts.no_cull);
    opts.no_cull_screen = parse_bool(root, "no_cull_screen", opts.no_cull_screen);
    opts.no_cull_enter_time = parse_bool(root, "no_cull_enter_time", opts.no_cull_enter_time);
    opts.note_outline = parse_bool(root, "note_outline", opts.note_outline);
    opts.no_note_outline = parse_bool(root, "no_note_outline", opts.no_note_outline);
    opts.force = parse_bool(root, "force", opts.force);
    opts.quiet = parse_bool(root, "quiet", opts.quiet);
    opts.no_color = parse_bool(root, "no_color", opts.no_color);
    opts.multicolor_lines = parse_bool(root, "multicolor_lines", opts.multicolor_lines);
    opts.no_title_overlay = parse_bool(root, "no_title_overlay", opts.no_title_overlay);
    opts.advance_seq_overlay = parse_bool(root, "advance_seq_overlay", opts.advance_seq_overlay);
    opts.basic_debug = parse_bool(root, "basic_debug", opts.basic_debug);
    opts.debug_line_label = parse_bool(root, "debug_line_label", opts.debug_line_label);
    opts.debug_line_stats = parse_bool(root, "debug_line_stats", opts.debug_line_stats);
    opts.debug_judge_windows = parse_bool(root, "debug_judge_windows", opts.debug_judge_windows);
    opts.debug_pointer = parse_bool(root, "debug_pointer", opts.debug_pointer);
    opts.debug_note_info = parse_bool(root, "debug_note_info", opts.debug_note_info);
    opts.debug_particles = parse_bool(root, "debug_particles", opts.debug_particles);
    opts.hit_debug = parse_bool(root, "hit_debug", opts.hit_debug);
    opts.judge_events = parse_bool(root, "judge_events", opts.judge_events);
    opts.simulateplay = parse_bool(root, "simulateplay", opts.simulateplay);
    opts.simulateplay_ipad = parse_bool(root, "simulateplay_ipad", opts.simulateplay_ipad);

    opts.advance = parse_str(root, "advance", opts.advance);
    opts.playlist_script = parse_str(root, "playlist_script", opts.playlist_script);
    opts.playlist_charts_dir = parse_str(root, "playlist_charts_dir", opts.playlist_charts_dir);
    opts.playlist_switch_mode = parse_str(root, "playlist_switch_mode", opts.playlist_switch_mode);
    opts.playlist_filter_levels = parse_str(root, "playlist_filter_levels", opts.playlist_filter_levels);
    opts.playlist_filter_name_contains = parse_str(root, "playlist_filter_name_contains", opts.playlist_filter_name_contains);
    opts.playlist_start_mode = parse_str(root, "playlist_start_mode", opts.playlist_start_mode);
    opts.simulateplay_mode = parse_str(root, "simulateplay_mode", opts.simulateplay_mode);
    opts.judge_script = parse_str(root, "judge_script", opts.judge_script);

    if (auto it = root.find("start_time"); it != root.end() && it->is_number()) {
        opts.start_time = it->get<double>();
        opts.has_start_time = true;
    }
    if (auto it = root.find("end_time"); it != root.end() && it->is_number()) {
        opts.end_time = it->get<double>();
        opts.has_end_time = true;
    }
    if (auto it = root.find("seconds"); it != root.end() && it->is_number()) {
        opts.seconds = it->get<double>();
        opts.has_seconds = true;
    }

    opts.simulateplay_max_pointers = parse_int(root, "simulateplay_max_pointers", opts.simulateplay_max_pointers);
    opts.playlist_notes_per_chart = parse_int(root, "playlist_notes_per_chart", opts.playlist_notes_per_chart);
    opts.playlist_seed = parse_int(root, "playlist_seed", opts.playlist_seed);
    opts.playlist_filter_min_total_notes = parse_int(root, "playlist_filter_min_total_notes", opts.playlist_filter_min_total_notes);
    opts.playlist_filter_max_total_notes = parse_int(root, "playlist_filter_max_total_notes", opts.playlist_filter_max_total_notes);
    opts.playlist_filter_limit = parse_int(root, "playlist_filter_limit", opts.playlist_filter_limit);
    opts.playlist_start_index = parse_int(root, "playlist_start_index", opts.playlist_start_index);
    opts.playlist_start_from_hit_total = parse_int(root, "playlist_start_from_hit_total", opts.playlist_start_from_hit_total);
    opts.playlist_start_from_combo_total = parse_int(root, "playlist_start_from_combo_total", opts.playlist_start_from_combo_total);
    opts.playlist_tail_time = parse_num(root, "playlist_tail_time", opts.playlist_tail_time);

    if (auto it = root.find("mods"); it != root.end() && it->is_object()) {
        apply_mods_from_json_obj(opts.mods, *it);
    }
}

void scan_config_path(AppOptions& opts, int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--config" || arg == "--config_old") && i + 1 < argc) {
            opts.config = argv[++i];
            break;
        }
    }
}

void apply_cli_overrides(AppOptions& opts, int argc, char** argv) {
    std::unordered_set<std::string> unsupported;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto next_s = [&](std::string& out) {
            if (i + 1 < argc) out = argv[++i];
        };
        auto next_d = [&](double& out) {
            if (i + 1 < argc) out = std::stod(argv[++i]);
        };
        auto next_i = [&](int& out) {
            if (i + 1 < argc) out = std::stoi(argv[++i]);
        };

        if (arg == "--input") next_s(opts.input);
        else if (arg == "--format") next_s(opts.format);
        else if (arg == "--config") next_s(opts.config);
        else if (arg == "--config_old") next_s(opts.config_old);
        else if (arg == "--save_config") next_s(opts.save_config);
        else if (arg == "--backend") next_s(opts.backend);
        else if (arg == "--audio_backend") next_s(opts.audio_backend);
        else if (arg == "--respack") next_s(opts.respack);
        else if (arg == "--bg") next_s(opts.bg);
        else if (arg == "--bgm") next_s(opts.bgm);
        else if (arg == "--lang") next_s(opts.lang);
        else if (arg == "--line_alpha_affects_notes") next_s(opts.line_alpha_affects_notes);
        else if (arg == "--font_path") next_s(opts.font_path);

        else if (arg == "--w") next_i(opts.w);
        else if (arg == "--h") next_i(opts.h);
        else if (arg == "--bg_blur") next_i(opts.bg_blur);
        else if (arg == "--bg_dim") next_i(opts.bg_dim);
        else if (arg == "--hitsound_min_interval_ms") next_i(opts.hitsound_min_interval_ms);
        else if (arg == "--hold_fx_interval_ms") next_i(opts.hold_fx_interval_ms);
        else if (arg == "--trail_blur") next_i(opts.trail_blur);
        else if (arg == "--trail_dim") next_i(opts.trail_dim);
        else if (arg == "--rpe_easing_shift") next_i(opts.rpe_easing_shift);

        else if (arg == "--approach") next_d(opts.approach);
        else if (arg == "--chart_speed") next_d(opts.chart_speed);
        else if (arg == "--note_scale_x") next_d(opts.note_scale_x);
        else if (arg == "--note_scale_y") next_d(opts.note_scale_y);
        else if (arg == "--note_flow_speed_multiplier") next_d(opts.note_flow_speed_multiplier);
        else if (arg == "--expand") next_d(opts.expand);
        else if (arg == "--overrender") next_d(opts.overrender);
        else if (arg == "--trail_alpha") next_d(opts.trail_alpha);
        else if (arg == "--bgm_volume") next_d(opts.bgm_volume);
        else if (arg == "--hitfx_scale_mul") next_d(opts.hitfx_scale_mul);
        else if (arg == "--font_size_multiplier") next_d(opts.font_size_multiplier);
        else if (arg == "--hold_tail_tol") next_d(opts.hold_tail_tol);
        else if (arg == "--judge_width") next_d(opts.judge_width);
        else if (arg == "--judge_height") next_d(opts.judge_height);
        else if (arg == "--flick_threshold") next_d(opts.flick_threshold);

        else if (arg == "--autoplay") opts.autoplay = true;
        else if (arg == "--no_cull") opts.no_cull = true;
        else if (arg == "--no_cull_screen") opts.no_cull_screen = true;
        else if (arg == "--no_cull_enter_time") opts.no_cull_enter_time = true;
        else if (arg == "--note_outline") opts.note_outline = true;
        else if (arg == "--no_note_outline") opts.no_note_outline = true;
        else if (arg == "--force") opts.force = true;
        else if (arg == "--quiet") opts.quiet = true;
        else if (arg == "--no_color") opts.no_color = true;
        else if (arg == "--multicolor_lines") opts.multicolor_lines = true;
        else if (arg == "--no_title_overlay") opts.no_title_overlay = true;
        else if (arg == "--advance_seq_overlay") opts.advance_seq_overlay = true;
        else if (arg == "--basic_debug") opts.basic_debug = true;
        else if (arg == "--debug_line_label") opts.debug_line_label = true;
        else if (arg == "--debug_line_stats") opts.debug_line_stats = true;
        else if (arg == "--debug_judge_windows") opts.debug_judge_windows = true;
        else if (arg == "--debug_pointer") opts.debug_pointer = true;
        else if (arg == "--debug_note_info") opts.debug_note_info = true;
        else if (arg == "--debug_particles") opts.debug_particles = true;
        else if (arg == "--hit_debug") opts.hit_debug = true;
        else if (arg == "--judge_events") opts.judge_events = true;

        else if (arg == "--start_time") { next_d(opts.start_time); opts.has_start_time = true; }
        else if (arg == "--end_time") { next_d(opts.end_time); opts.has_end_time = true; }
        else if (arg == "--seconds") { next_d(opts.seconds); opts.has_seconds = true; }

        else if (arg == "--mod_mirror") opts.mods.mirror = true;
        else if (arg == "--mod_reverse") opts.mods.reverse_time = true;
        else if (arg == "--mod_randomize") opts.mods.randomize_lane = true;
        else if (arg == "--mod_full_blue") opts.mods.full_blue = true;
        else if (arg == "--mod_hold_convert") opts.mods.hold_convert_tap = true;
        else if (arg == "--mod_quantize") { opts.mods.quantize = true; next_d(opts.mods.quantize_step_sec); }
        else if (arg == "--mod_wave") opts.mods.wave = true;
        else if (arg == "--mod_stutter") opts.mods.stutter = true;
        else if (arg == "--mod_seed") next_i(opts.mods.random_seed);
        else if (arg == "--mod_lane_count") next_i(opts.mods.lane_count);
        else if (arg == "--mod_lane_scale") next_d(opts.mods.lane_scale);
        else if (arg == "--mod_lane_scale_center") next_d(opts.mods.lane_scale_center);
        else if (arg == "--mod_compress_zip_count") next_i(opts.mods.compress_zip_count);
        else if (arg == "--mod_thin_out_every") next_i(opts.mods.thin_out_every);
        else if (arg == "--mod_transpose") next_d(opts.mods.transpose_sec);
        else if (arg == "--mod_stretch") next_d(opts.mods.stretch_factor);
        else if (arg == "--mod_stretch_anchor") next_d(opts.mods.stretch_anchor_sec);
        else if (arg == "--mod_wave_amp") next_d(opts.mods.wave_amplitude_lane);
        else if (arg == "--mod_wave_period") next_d(opts.mods.wave_period_sec);
        else if (arg == "--mod_stutter_repeat") next_i(opts.mods.stutter_repeat);
        else if (arg == "--mod_stutter_interval") next_d(opts.mods.stutter_interval_sec);
        else if (arg == "--mod_stutter_alpha_decay") next_d(opts.mods.stutter_alpha_decay);

        else if (arg == "--advance") next_s(opts.advance);
        else if (arg == "--advance_lazy_load") opts.advance_lazy_load = true;
        else if (arg == "--advance_lazy_cache") next_i(opts.advance_lazy_cache);
        else if (arg == "--advance_lazy_preload") opts.advance_lazy_preload = true;
        else if (arg == "--advance_lazy_scan_total_notes") opts.advance_lazy_scan_total_notes = true;

        else if (arg == "--playlist_script") next_s(opts.playlist_script);
        else if (arg == "--playlist_charts_dir") next_s(opts.playlist_charts_dir);
        else if (arg == "--playlist_notes_per_chart") next_i(opts.playlist_notes_per_chart);
        else if (arg == "--playlist_tail_time") next_d(opts.playlist_tail_time);
        else if (arg == "--playlist_seed") next_i(opts.playlist_seed);
        else if (arg == "--playlist_no_shuffle") opts.playlist_no_shuffle = true;
        else if (arg == "--playlist_switch_mode") next_s(opts.playlist_switch_mode);
        else if (arg == "--playlist_filter_levels") next_s(opts.playlist_filter_levels);
        else if (arg == "--playlist_filter_name_contains") next_s(opts.playlist_filter_name_contains);
        else if (arg == "--playlist_filter_min_total_notes") next_i(opts.playlist_filter_min_total_notes);
        else if (arg == "--playlist_filter_max_total_notes") next_i(opts.playlist_filter_max_total_notes);
        else if (arg == "--playlist_filter_limit") next_i(opts.playlist_filter_limit);
        else if (arg == "--playlist_start_mode") next_s(opts.playlist_start_mode);
        else if (arg == "--playlist_start_index") next_i(opts.playlist_start_index);
        else if (arg == "--playlist_start_from_hit_total") next_i(opts.playlist_start_from_hit_total);
        else if (arg == "--playlist_start_from_combo_total") next_i(opts.playlist_start_from_combo_total);

        else if (arg == "--simulateplay") opts.simulateplay = true;
        else if (arg == "--simulateplay_ipad") opts.simulateplay_ipad = true;
        else if (arg == "--simulateplay_mode") next_s(opts.simulateplay_mode);
        else if (arg == "--simulateplay_max_pointers") next_i(opts.simulateplay_max_pointers);
        else if (arg == "--judge_script") next_s(opts.judge_script);

        else if (arg == "--ipad_bundle_id") next_s(opts.ipad_bundle_id);
        else if (arg == "--ipad_udid") next_s(opts.ipad_udid);
        else if (arg == "--ipad_device_name") next_s(opts.ipad_device_name);
        else if (arg == "--ipad_appium_server") next_s(opts.ipad_appium_server);
        else if (arg == "--ipad_mjpeg_url") next_s(opts.ipad_mjpeg_url);
        else if (arg == "--ipad_move_hz") next_d(opts.ipad_move_hz);
        else if (arg == "--ipad_preview_fps") next_d(opts.ipad_preview_fps);
        else if (arg == "--ipad_max_retries") next_i(opts.ipad_max_retries);
        else if (arg == "--ipad_retry_backoff_s") next_d(opts.ipad_retry_backoff_s);
        else if (arg == "--ipad_reconnect") opts.ipad_reconnect = true;
        else if (arg == "--ipad_activate_app") opts.ipad_activate_app = true;
        else if (arg == "--ipad_viewport_x0") next_d(opts.ipad_viewport_x0);
        else if (arg == "--ipad_viewport_y0") next_d(opts.ipad_viewport_y0);
        else if (arg == "--ipad_viewport_x1") next_d(opts.ipad_viewport_x1);
        else if (arg == "--ipad_viewport_y1") next_d(opts.ipad_viewport_y1);

        else if (arg == "--gui") opts.gui = true;
        else if (arg == "--export_pcc") next_s(opts.export_pcc);
        else if (arg == "--export_pcc_password") next_s(opts.export_pcc_password);
        else if (arg == "--export_pcc_no_compress") opts.export_pcc_no_compress = true;

        else if (arg == "--help" || arg == "-h") {
            usage();
            std::exit(0);
        } else if (!arg.empty() && arg[0] == '-') {
            unsupported.insert(arg);
        }
    }

    for (const auto& opt : unsupported) {
        std::cerr << "warning: option currently ignored in C++ phase1: " << opt << "\n";
    }
}

void maybe_save_config(const AppOptions& opts) {
    if (opts.save_config.empty()) return;

    nlohmann::json out;
    out["input"] = opts.input;
    out["format"] = opts.format;
    out["backend"] = opts.backend;
    out["audio_backend"] = opts.audio_backend;
    out["respack"] = opts.respack;
    out["bg"] = opts.bg;
    out["bgm"] = opts.bgm;
    out["lang"] = opts.lang;
    out["w"] = opts.w;
    out["h"] = opts.h;
    out["approach"] = opts.approach;
    out["chart_speed"] = opts.chart_speed;
    out["autoplay"] = opts.autoplay;
    out["simulateplay"] = opts.simulateplay;
    out["simulateplay_mode"] = opts.simulateplay_mode;
    out["simulateplay_max_pointers"] = opts.simulateplay_max_pointers;
    out["start_time"] = opts.has_start_time ? nlohmann::json(opts.start_time) : nlohmann::json(nullptr);
    out["end_time"] = opts.has_end_time ? nlohmann::json(opts.end_time) : nlohmann::json(nullptr);

    out["mods"] = {
        {"mirror", opts.mods.mirror},
        {"reverse", opts.mods.reverse_time},
        {"randomize", opts.mods.randomize_lane},
        {"hold_convert", opts.mods.hold_convert_tap},
        {"transpose", opts.mods.transpose_sec},
        {"stretch", opts.mods.stretch_factor},
        {"stretch_anchor", opts.mods.stretch_anchor_sec},
        {"quantize", opts.mods.quantize},
        {"quantize_step", opts.mods.quantize_step_sec},
        {"wave", opts.mods.wave},
        {"wave_amp", opts.mods.wave_amplitude_lane},
        {"wave_period", opts.mods.wave_period_sec},
        {"stutter", opts.mods.stutter},
        {"stutter_repeat", opts.mods.stutter_repeat},
        {"stutter_interval", opts.mods.stutter_interval_sec},
        {"stutter_alpha_decay", opts.mods.stutter_alpha_decay},
        {"thin_out_every", opts.mods.thin_out_every},
        {"seed", opts.mods.random_seed},
        {"lane_count", opts.mods.lane_count},
    };

    std::ofstream ofs(opts.save_config, std::ios::binary);
    if (!ofs) {
        std::cerr << "warning: failed to write --save_config target: " << opts.save_config << "\n";
        return;
    }
    ofs << out.dump(2) << "\n";
}

void warn_placeholder_features(const AppOptions& opts) {
    std::vector<std::string> active;
    if (opts.simulateplay_ipad || !opts.ipad_bundle_id.empty() || !opts.ipad_udid.empty()) {
        active.push_back("simulateplay-ipad bridge");
    }
    if (opts.gui) {
        active.push_back("gui launcher");
    }
    if (!opts.export_pcc.empty() || !opts.export_pcc_password.empty() || opts.export_pcc_no_compress) {
        active.push_back("pcc export");
    }
    if (!opts.playlist_script.empty()) {
        active.push_back("playlist_script python hook");
    }
    if (!opts.judge_script.empty()) {
        active.push_back("judge_script behavior profile");
    }

    if (!active.empty()) {
        std::cerr << "warning: these features are parsed but still simplified/unimplemented: ";
        for (std::size_t i = 0; i < active.size(); ++i) {
            if (i > 0) std::cerr << ", ";
            std::cerr << active[i];
        }
        std::cerr << "\n";
    }
}

void normalize_runtime_options(AppOptions& opts) {
    auto clamp_pos = [](double& v, double fallback, double min_v, const char* name) {
        if (!std::isfinite(v) || v < min_v) {
            std::cerr << "warning: invalid " << name << ", fallback to " << fallback << "\n";
            v = fallback;
        }
    };

    auto clamp_range = [](double& v, double fallback, double min_v, double max_v, const char* name) {
        if (!std::isfinite(v)) {
            std::cerr << "warning: invalid " << name << ", fallback to " << fallback << "\n";
            v = fallback;
            return;
        }
        if (v < min_v || v > max_v) {
            std::cerr << "warning: clamp " << name << " to [" << min_v << ", " << max_v << "]\n";
            v = std::clamp(v, min_v, max_v);
        }
    };

    if (opts.no_note_outline) opts.note_outline = false;
    if (opts.simulateplay) opts.autoplay = false;

    clamp_pos(opts.approach, 3.0, 0.1, "approach");
    clamp_pos(opts.chart_speed, 1.0, 0.01, "chart_speed");
    clamp_pos(opts.note_scale_x, 1.0, 0.01, "note_scale_x");
    clamp_pos(opts.note_scale_y, 1.0, 0.01, "note_scale_y");
    clamp_pos(opts.note_flow_speed_multiplier, 1.0, 0.01, "note_flow_speed_multiplier");
    clamp_pos(opts.expand, 1.0, 1.0, "expand");
    clamp_pos(opts.overrender, 2.0, 1.0, "overrender");
    clamp_pos(opts.hitfx_scale_mul, 1.0, 0.01, "hitfx_scale_mul");
    clamp_pos(opts.font_size_multiplier, 1.0, 0.01, "font_size_multiplier");
    clamp_pos(opts.hold_tail_tol, 0.8, 0.001, "hold_tail_tol");
    clamp_pos(opts.judge_width, 0.12, 0.001, "judge_width");
    clamp_pos(opts.judge_height, 0.06, 0.001, "judge_height");
    clamp_pos(opts.flick_threshold, 0.02, 0.00001, "flick_threshold");
    clamp_range(opts.trail_alpha, 0.0, 0.0, 1.0, "trail_alpha");
    clamp_range(opts.bgm_volume, 0.8, 0.0, 2.0, "bgm_volume");

    if (opts.simulateplay_max_pointers < 1) {
        std::cerr << "warning: simulateplay_max_pointers < 1, fallback to 1\n";
        opts.simulateplay_max_pointers = 1;
    }
    if (opts.mods.lane_count < 1) {
        std::cerr << "warning: mod_lane_count < 1, fallback to 8\n";
        opts.mods.lane_count = 8;
    }
    if (!std::isfinite(opts.mods.lane_scale) || opts.mods.lane_scale <= 0.0) {
        std::cerr << "warning: invalid mod_lane_scale, fallback to 1.0\n";
        opts.mods.lane_scale = 1.0;
    }
    if (!std::isfinite(opts.mods.lane_scale_center)) {
        opts.mods.lane_scale_center = -1.0;
    }
    if (opts.mods.compress_zip_count < 1) {
        std::cerr << "warning: mod_compress_zip_count < 1, fallback to 1\n";
        opts.mods.compress_zip_count = 1;
    }
    clamp_range(opts.mods.stutter_alpha_decay, 0.8, 0.0, 1.0, "mod_stutter_alpha_decay");
}

GlobalRuntimeOptions build_global_runtime_options(const AppOptions& opts) {
    GlobalRuntimeOptions g;
    g.respack_path = opts.respack;
    g.has_respack = path_exists(opts.respack);
    if (!opts.respack.empty() && !g.has_respack) {
        std::cerr << "warning: respack path not found: " << opts.respack << "\n";
    }
    return g;
}

bool path_exists(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::exists(fs::path(p), ec);
}

std::vector<std::string> discover_playlist_inputs(const std::string& charts_dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(charts_dir, ec)) return out;

    for (auto it = fs::recursive_directory_iterator(charts_dir, ec); it != fs::recursive_directory_iterator(); ++it) {
        if (ec) break;
        if (!it->is_regular_file()) continue;
        const auto ext = it->path().extension().string();
        std::string low = ext;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (low == ".json" || low == ".rpe" || low == ".pec") {
            out.push_back(it->path().string());
        }
    }

    std::sort(out.begin(), out.end());
    return out;
}

bool level_filter_match(const std::string& path, const std::string& levels_csv) {
    if (levels_csv.empty()) return true;
    std::string up = path;
    std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    std::size_t start = 0;
    while (start < levels_csv.size()) {
        std::size_t comma = levels_csv.find_first_of(",;", start);
        if (comma == std::string::npos) comma = levels_csv.size();
        std::string token = levels_csv.substr(start, comma - start);
        token.erase(std::remove_if(token.begin(), token.end(), [](unsigned char c) { return std::isspace(c); }), token.end());
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (!token.empty() && up.find(token) != std::string::npos) {
            return true;
        }
        start = comma + 1;
    }
    return false;
}

std::vector<RunItem> build_playlist_items(const AppOptions& opts) {
    const bool playlist_requested = !opts.playlist_script.empty() || opts.playlist_no_shuffle || opts.playlist_seed != 0 ||
        !opts.playlist_filter_levels.empty() || !opts.playlist_filter_name_contains.empty() ||
        opts.playlist_filter_min_total_notes > 0 || opts.playlist_filter_max_total_notes > 0 ||
        opts.playlist_filter_limit > 0 || opts.playlist_start_index > 0 ||
        opts.playlist_start_from_combo_total > 0 || opts.playlist_start_from_hit_total > 0 ||
        opts.playlist_start_mode == "resume";

    if (!playlist_requested) return {};

    std::vector<std::string> files = discover_playlist_inputs(opts.playlist_charts_dir);

    if (!opts.playlist_filter_name_contains.empty()) {
        const std::string needle = opts.playlist_filter_name_contains;
        files.erase(std::remove_if(files.begin(), files.end(), [&](const std::string& p) {
            return p.find(needle) == std::string::npos;
        }), files.end());
    }

    if (!opts.playlist_filter_levels.empty()) {
        files.erase(std::remove_if(files.begin(), files.end(), [&](const std::string& p) {
            return !level_filter_match(p, opts.playlist_filter_levels);
        }), files.end());
    }

    if (!opts.playlist_no_shuffle) {
        std::mt19937 rng(static_cast<std::mt19937::result_type>(opts.playlist_seed == 0 ? 42 : opts.playlist_seed));
        std::shuffle(files.begin(), files.end(), rng);
    }

    if (opts.playlist_start_index > 0 && static_cast<std::size_t>(opts.playlist_start_index) < files.size()) {
        files.erase(files.begin(), files.begin() + opts.playlist_start_index);
    }

    if (opts.playlist_filter_limit > 0 && static_cast<std::size_t>(opts.playlist_filter_limit) < files.size()) {
        files.resize(static_cast<std::size_t>(opts.playlist_filter_limit));
    }

    std::vector<RunItem> out;
    out.reserve(files.size());
    for (std::size_t i = 0; i < files.size(); ++i) {
        RunItem item;
        item.input = files[i];
        item.format = infer_format(item.input);
        item.label = "playlist[" + std::to_string(i) + "]";
        out.push_back(item);
    }
    return out;
}

std::vector<RunItem> build_advance_items(const AppOptions& opts) {
    if (opts.advance.empty()) return {};

    const std::string text = read_file(opts.advance);
    if (text.empty()) {
        std::cerr << "warning: --advance file not found/readable: " << opts.advance << "\n";
        return {};
    }

    nlohmann::json root;
    try {
        root = parse_json_or_jsonc(text);
    } catch (const std::exception& e) {
        std::cerr << "warning: --advance parse failed: " << e.what() << "\n";
        return {};
    }

    std::vector<RunItem> out;
    const fs::path base_dir = fs::absolute(fs::path(opts.advance)).parent_path();

    auto parse_track = [&](const nlohmann::json& t, std::size_t idx) {
        if (!t.is_object()) return;
        const std::string in = parse_str(t, "input", "");
        if (in.empty()) return;

        fs::path in_path = fs::path(in);
        if (in_path.is_relative()) {
            fs::path rel_to_adv = base_dir / in_path;
            if (fs::exists(rel_to_adv)) {
                in_path = rel_to_adv;
            }
        }

        RunItem item;
        item.input = in_path.string();
        item.format = parse_str(t, "format", infer_format(item.input));
        item.label = "advance[" + std::to_string(idx) + "]";

        if (auto it = t.find("start_at"); it != t.end() && it->is_number()) {
            item.has_start_time = true;
            item.start_time = it->get<double>();
        }
        if (auto it = t.find("end_at"); it != t.end() && it->is_number()) {
            item.has_end_time = true;
            item.end_time = it->get<double>();
        }
        if (auto it = t.find("mods"); it != t.end() && it->is_object()) {
            item.has_mods_override = true;
            item.mods_override = opts.mods;
            apply_mods_from_json_obj(item.mods_override, *it);
        }

        out.push_back(item);
    };

    if (auto it = root.find("tracks"); it != root.end() && it->is_array()) {
        std::size_t idx = 0;
        for (const auto& t : *it) {
            parse_track(t, idx++);
        }
    } else if (auto it = root.find("sequence"); it != root.end() && it->is_array()) {
        std::size_t idx = 0;
        for (const auto& t : *it) {
            parse_track(t, idx++);
        }
    }

    return out;
}

bool run_items_requested(const AppOptions& opts) {
    return !opts.advance.empty() || !opts.playlist_script.empty() || opts.playlist_no_shuffle || opts.playlist_seed != 0 ||
           !opts.playlist_filter_levels.empty() || !opts.playlist_filter_name_contains.empty() ||
           opts.playlist_filter_min_total_notes > 0 || opts.playlist_filter_max_total_notes > 0 || opts.playlist_filter_limit > 0 ||
           opts.playlist_start_index > 0 || opts.playlist_start_from_combo_total > 0 || opts.playlist_start_from_hit_total > 0 ||
           opts.playlist_start_mode == "resume";
}

std::vector<RunItem> build_run_plan(const AppOptions& opts) {
    std::vector<RunItem> plan;

    if (!opts.input.empty()) {
        RunItem single;
        single.input = opts.input;
        single.format = opts.format.empty() ? infer_format(opts.input) : opts.format;
        single.label = "input";
        single.has_start_time = opts.has_start_time;
        single.start_time = opts.start_time;
        single.has_end_time = opts.has_end_time;
        single.end_time = opts.end_time;
        plan.push_back(single);
    }

    auto adv = build_advance_items(opts);
    for (auto& it : adv) {
        plan.push_back(it);
    }

    auto playlist = build_playlist_items(opts);
    for (auto& it : playlist) {
        plan.push_back(it);
    }

    return plan;
}

double chart_end_time(const phic::ChartData& chart) {
    double end_t = 0.0;
    for (const auto& n : chart.notes) {
        end_t = std::max(end_t, std::max(n.t_hit, n.hold_end));
    }
    return end_t;
}

std::vector<phic::InputEvent> generate_simulate_inputs(
    const phic::Engine& engine,
    const AppOptions& opts,
    SimulateState& st,
    double now,
    double dt
) {
    std::vector<phic::InputEvent> out;
    if (!opts.simulateplay) return out;

    std::string mode = opts.simulateplay_mode;
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (mode != "aggressive" && mode != "extreme") mode = "conservative";

    const int lane_count = std::max(1, opts.mods.lane_count);
    if (st.lane_cooldown_until.empty()) {
        st.lane_cooldown_until.assign(static_cast<std::size_t>(lane_count), -1e9);
    }
    if (!st.seeded) {
        st.seeded = true;
        st.rng.seed(static_cast<std::mt19937::result_type>(opts.playlist_seed == 0 ? 12345 : opts.playlist_seed));
    }

    const auto& notes = engine.chart().notes;
    const double lookahead = (mode == "extreme") ? 0.090 : (mode == "aggressive" ? 0.055 : 0.025);
    const double lane_cooldown = (mode == "extreme") ? 0.0 : (mode == "aggressive" ? 0.010 : 0.018);
    const double jitter_abs = (mode == "extreme") ? 0.010 : (mode == "aggressive" ? 0.004 : 0.0);
    const int max_ptr = std::max(1, opts.simulateplay_max_pointers);

    while (st.cursor < notes.size() && notes[st.cursor].t_hit < now - 0.200) {
        ++st.cursor;
    }

    std::uniform_real_distribution<double> jitter(-jitter_abs, jitter_abs);

    for (std::size_t i = st.cursor; i < notes.size(); ++i) {
        const auto& n = notes[i];
        if (n.t_hit > now + lookahead) {
            break;
        }
        if (st.fired.find(n.id) != st.fired.end()) {
            continue;
        }

        const int lane = std::clamp(n.lane, 0, lane_count - 1);
        if (st.lane_cooldown_until[static_cast<std::size_t>(lane)] > now) {
            continue;
        }
        if (static_cast<int>(out.size()) >= max_ptr) {
            break;
        }

        double event_t = n.t_hit + jitter(st.rng);
        event_t = std::max(now - 0.001, event_t);

        phic::InputEvent ev;
        ev.type = phic::InputEvent::Type::PointerDown;
        ev.lane = lane;
        ev.event_time = event_t;
        out.push_back(ev);

        st.fired.insert(n.id);
        st.lane_cooldown_until[static_cast<std::size_t>(lane)] = now + lane_cooldown;
    }

    while (st.cursor < notes.size() && st.fired.find(notes[st.cursor].id) != st.fired.end()) {
        ++st.cursor;
    }

    return out;
}

}  // namespace

int main(int argc, char** argv) {
    AppOptions opts;

    scan_config_path(opts, argc, argv);
    apply_config_file(opts);
    apply_cli_overrides(opts, argc, argv);
    normalize_runtime_options(opts);
    maybe_save_config(opts);
    warn_placeholder_features(opts);
    const GlobalRuntimeOptions global = build_global_runtime_options(opts);

    std::vector<RunItem> plan = build_run_plan(opts);
    if (plan.empty()) {
        usage();
        return 1;
    }

    phic::EngineStats total_stats{};
    int run_ok = 0;
    int run_skipped = 0;

    std::cout << "[global] backend=" << opts.backend
              << " audio_backend=" << opts.audio_backend
              << " simulateplay=" << (opts.simulateplay ? "on" : "off")
              << " autoplay=" << (opts.autoplay ? "on" : "off")
              << " respack=" << (global.has_respack ? global.respack_path : "(none)") << "\n";

    for (std::size_t run_i = 0; run_i < plan.size(); ++run_i) {
        const RunItem& item = plan[run_i];
        if (!path_exists(item.input)) {
            std::cerr << "warning: skip missing input: " << item.input << "\n";
            ++run_skipped;
            continue;
        }

        const std::string payload = read_file(item.input);
        if (payload.empty()) {
            std::cerr << "warning: skip unreadable input: " << item.input << "\n";
            ++run_skipped;
            continue;
        }

        const std::string fmt = item.format.empty() ? infer_format(item.input) : item.format;
        const auto parsed = phic::parse_chart_bytes(payload, fmt);
        if (!parsed.ok) {
            std::cerr << "warning: parse failed for " << item.input << ": " << parsed.error << "\n";
            ++run_skipped;
            continue;
        }

        phic::RenderConfig cfg;
        cfg.width = opts.w;
        cfg.height = opts.h;
        cfg.approach_sec = opts.approach;
        cfg.note_speed = opts.chart_speed;
        cfg.autoplay = opts.autoplay;
        cfg.no_cull = opts.no_cull;
        cfg.no_cull_screen = opts.no_cull_screen;
        cfg.no_cull_enter_time = opts.no_cull_enter_time;
        cfg.note_outline = opts.note_outline && !opts.no_note_outline;
        cfg.note_scale_x = opts.note_scale_x;
        cfg.note_scale_y = opts.note_scale_y;
        cfg.note_flow_speed_multiplier = opts.note_flow_speed_multiplier;
        cfg.expand = opts.expand;
        cfg.overrender = opts.overrender;
        cfg.trail_alpha = opts.trail_alpha;
        cfg.trail_blur = opts.trail_blur;
        cfg.trail_dim = opts.trail_dim;
        cfg.bg_blur = opts.bg_blur;
        cfg.bg_dim = opts.bg_dim;
        cfg.bgm_volume = opts.bgm_volume;
        cfg.hitsound_min_interval_ms = opts.hitsound_min_interval_ms;
        cfg.hitfx_scale_mul = opts.hitfx_scale_mul;
        cfg.multicolor_lines = opts.multicolor_lines;
        cfg.line_alpha_affects_notes = opts.line_alpha_affects_notes;
        cfg.no_title_overlay = opts.no_title_overlay;
        cfg.advance_seq_overlay = opts.advance_seq_overlay;
        cfg.font_path = opts.font_path;
        cfg.font_size_multiplier = opts.font_size_multiplier;
        cfg.hold_fx_interval_ms = opts.hold_fx_interval_ms;
        cfg.hold_tail_tol = opts.hold_tail_tol;
        cfg.judge_width = opts.judge_width;
        cfg.judge_height = opts.judge_height;
        cfg.flick_threshold = opts.flick_threshold;
        cfg.rpe_easing_shift = opts.rpe_easing_shift;
        cfg.lang = opts.lang;
        cfg.quiet = opts.quiet;
        cfg.no_color = opts.no_color;
        cfg.basic_debug = opts.basic_debug;
        cfg.debug_line_label = opts.debug_line_label;
        cfg.debug_line_stats = opts.debug_line_stats;
        cfg.debug_judge_windows = opts.debug_judge_windows;
        cfg.debug_pointer = opts.debug_pointer;
        cfg.debug_note_info = opts.debug_note_info;
        cfg.debug_particles = opts.debug_particles;
        cfg.hit_debug = opts.hit_debug;
        cfg.mods = item.has_mods_override ? item.mods_override : opts.mods;

        phic::Engine engine(cfg);
        engine.load_chart(parsed.chart);

        const bool has_start = item.has_start_time || opts.has_start_time;
        const double start_time = item.has_start_time ? item.start_time : opts.start_time;
        if (has_start) {
            engine.seek(std::max(0.0, start_time));
        }

        const double chart_end = chart_end_time(engine.chart()) + std::max(0.5, opts.playlist_tail_time);
        const double start_for_duration = has_start ? std::max(0.0, start_time) : 0.0;

        double run_for_sec = opts.has_seconds ? opts.seconds : std::max(0.0, chart_end - start_for_duration);
        const bool has_end = item.has_end_time || opts.has_end_time;
        const double end_time = item.has_end_time ? item.end_time : opts.end_time;
        if (has_end) {
            run_for_sec = std::max(0.0, end_time - start_for_duration);
        }

        constexpr double kDt = 1.0 / 120.0;
        const int ticks = static_cast<int>(std::max(0.0, run_for_sec) / kDt);
        phic::Engine::StepResult last;
        int evt_perfect = 0;
        int evt_good = 0;
        int evt_bad = 0;
        int evt_miss = 0;
        int evt_input = 0;
        int evt_auto = 0;
        int evt_timeout = 0;

        SimulateState sim_state;
        if (opts.simulateplay) {
            sim_state.lane_cooldown_until.assign(static_cast<std::size_t>(std::max(1, opts.mods.lane_count)), -1e9);
        }

        double sim_now = has_start ? start_for_duration : 0.0;
        for (int i = 0; i < ticks; ++i) {
            const auto events = generate_simulate_inputs(engine, opts, sim_state, sim_now, kDt);
            last = engine.step(kDt, events);
            for (const auto& ev : last.judge_events) {
                if (ev.kind == phic::JudgeKind::Perfect) ++evt_perfect;
                else if (ev.kind == phic::JudgeKind::Good) ++evt_good;
                else if (ev.kind == phic::JudgeKind::Bad) ++evt_bad;
                else if (ev.kind == phic::JudgeKind::Miss) ++evt_miss;

                if (ev.source == phic::JudgeSource::Input) ++evt_input;
                else if (ev.source == phic::JudgeSource::Autoplay) ++evt_auto;
                else if (ev.source == phic::JudgeSource::TimeoutMiss) ++evt_timeout;
            }
            sim_now += kDt * std::max(1e-6, cfg.note_speed);
        }

        total_stats.combo = last.stats.combo;
        total_stats.max_combo = std::max(total_stats.max_combo, last.stats.max_combo);
        total_stats.judged_cnt += last.stats.judged_cnt;
        total_stats.hit_total += last.stats.hit_total;
        total_stats.acc_sum += last.stats.acc_sum;

        std::cout << "------------------------------------------------------------\n";
        std::cout << "[run " << run_i + 1 << "/" << plan.size() << "] " << item.label << "\n";
        std::cout << "input          : " << item.input << "\n";
        std::cout << "title          : " << parsed.chart.title << "\n";
        std::cout << "notes_after_mods: " << engine.chart().notes.size() << "\n";
        std::cout << "time_sec       : " << std::fixed << std::setprecision(3) << last.time_sec << "\n";
        std::cout << "judged/hit     : " << last.stats.judged_cnt << " / " << last.stats.hit_total << "\n";
        std::cout << "combo(max)     : " << last.stats.combo << " (" << last.stats.max_combo << ")\n";
        std::cout << "accuracy       : " << std::setprecision(6) << last.stats.accuracy() << "\n";
        if (opts.judge_events) {
            std::cout << "judge_events   : P=" << evt_perfect << " G=" << evt_good << " B=" << evt_bad << " M=" << evt_miss
                      << " | input=" << evt_input << " auto=" << evt_auto << " timeout=" << evt_timeout << "\n";
        }
        std::cout << std::defaultfloat;

        ++run_ok;
    }

    if (run_ok == 0) {
        std::cerr << "no runs completed successfully\n";
        return 2;
    }

    std::cout << "============================================================\n";
    std::cout << "[total] requested=" << plan.size() << " completed=" << run_ok << " skipped=" << run_skipped << "\n";
    std::cout << "judged/hit      : " << total_stats.judged_cnt << " / " << total_stats.hit_total << "\n";
    std::cout << "max_combo       : " << total_stats.max_combo << "\n";
    std::cout << "weighted_acc    : " << std::fixed << std::setprecision(6) << total_stats.accuracy() << "\n";

    return 0;
}
