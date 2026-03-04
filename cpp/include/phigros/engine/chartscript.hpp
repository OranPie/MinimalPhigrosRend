#pragma once
// chartscript.hpp — Declarative chart playback scripting system.
//
// Replaces/improves the Python advance.json + playlist_script system
// with a unified, feature-rich JSON DSL for scripted chart playback.
//
// Features over Python advance mode:
//   - Per-item config overrides (chart_speed, trail settings, etc.)
//   - Per-item mod pipelines (inline or file references)
//   - Playlist modes: sequence, shuffle, loop, repeat
//   - Filters: by difficulty, note count, name pattern
//   - Transitions: fade, crossfade with configurable duration
//   - Variables and conditions for dynamic playlists
//   - Auto-discovery of charts from directories
//
// .chartscript.json format:
//   {
//     "version": 1,
//     "name": "My Playlist",
//     "mode": "sequence",          // sequence | shuffle | loop
//     "shuffle_seed": 42,          // optional, for reproducible shuffle
//     "repeat": 1,                 // how many times to loop (0 = infinite)
//     "transition": { "type": "fade", "duration": 0.5 },
//     "defaults": {                // applied to all items unless overridden
//       "chart_speed": 1.0,
//       "trail_alpha": 0.5
//     },
//     "discover": {                // auto-discover charts (alternative to items)
//       "directory": "charts/",
//       "levels": ["AT", "IN"],
//       "recursive": true,
//       "sort_by": "name"          // name | notes | difficulty | random
//     },
//     "items": [
//       {
//         "input": "chart.json",
//         "start": 0.0,
//         "end": 30.0,
//         "bgm": "song.ogg",
//         "bg": "bg.png",
//         "config": { "chart_speed": 1.5, "trail_alpha": 0.8 },
//         "mods": [
//           { "type": "colorize", "mode": "hue", "hue_s": 1.0 }
//         ],
//         "mod_file": "my.mod.json",
//         "filter": {
//           "min_notes": 100,
//           "max_notes": 2000,
//           "name_contains": "CHAOS"
//         }
//       }
//     ]
//   }

#include "phigros/config/render_config.hpp"
#include "phigros/core/mods.hpp"
#include "phigros/core/mod_loader.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace phigros::chartscript {

// ── Transition types ─────────────────────────────────────────────────────────

enum class TransitionType : uint8_t { None, Fade, Crossfade };

struct Transition {
    TransitionType type = TransitionType::None;
    double duration = 0.5;  // seconds
};

// ── Filter (per-item or global) ──────────────────────────────────────────────

struct Filter {
    std::vector<std::string> levels;       // e.g. ["AT", "IN"]
    int min_notes = -1;
    int max_notes = -1;
    std::string name_contains;

    bool empty() const {
        return levels.empty() && min_notes < 0 && max_notes < 0 && name_contains.empty();
    }
};

// ── Playlist item ────────────────────────────────────────────────────────────

struct Item {
    std::string input;                     // chart path (json/pec/phbc/zip)
    std::string bgm;
    std::string bg;
    double start = 0.0;                    // segment start time (seconds)
    double end   = -1.0;                   // segment end time (-1 = full chart)
    double start_at = 0.0;                 // when to begin in the playlist timeline

    // Per-item config overrides (merged on top of defaults)
    nlohmann::json config;                 // partial RenderConfig fields

    // Per-item mods (inline ops array)
    std::vector<mods::AnyOp> inline_mods;
    std::string mod_file;                  // path to .mod.json

    // Metadata (populated during discovery/load)
    std::string name;
    std::string level;
    int total_notes = -1;

    Filter filter;                         // per-item filter (for discovered items)
};

// ── Discover configuration ───────────────────────────────────────────────────

enum class SortBy : uint8_t { Name, Notes, Difficulty, Random };

struct Discover {
    std::string directory;
    std::vector<std::string> levels;
    bool recursive = true;
    SortBy sort_by = SortBy::Name;
    bool enabled = false;
};

// ── Playlist modes ───────────────────────────────────────────────────────────

enum class PlayMode : uint8_t { Sequence, Shuffle, Loop };

// ── Top-level script ─────────────────────────────────────────────────────────

struct Script {
    int version = 1;
    std::string name;
    PlayMode mode = PlayMode::Sequence;
    int shuffle_seed = 0;                  // 0 = random seed
    int repeat = 1;                        // 0 = infinite loop
    Transition transition;
    nlohmann::json defaults;               // default config overrides for all items
    Discover discover;
    std::vector<Item> items;
};

// ── Parsing ──────────────────────────────────────────────────────────────────

namespace detail {

inline TransitionType parse_transition_type(const std::string& s) {
    if (s == "fade")      return TransitionType::Fade;
    if (s == "crossfade") return TransitionType::Crossfade;
    return TransitionType::None;
}

inline PlayMode parse_play_mode(const std::string& s) {
    if (s == "shuffle") return PlayMode::Shuffle;
    if (s == "loop")    return PlayMode::Loop;
    return PlayMode::Sequence;
}

inline SortBy parse_sort_by(const std::string& s) {
    if (s == "notes")      return SortBy::Notes;
    if (s == "difficulty")  return SortBy::Difficulty;
    if (s == "random")     return SortBy::Random;
    return SortBy::Name;
}

inline Filter parse_filter(const nlohmann::json& j) {
    Filter f;
    if (j.contains("levels") && j["levels"].is_array()) {
        for (const auto& lv : j["levels"])
            f.levels.push_back(lv.get<std::string>());
    }
    if (j.contains("min_notes")) f.min_notes = j["min_notes"].get<int>();
    if (j.contains("max_notes")) f.max_notes = j["max_notes"].get<int>();
    if (j.contains("name_contains")) f.name_contains = j["name_contains"].get<std::string>();
    return f;
}

inline std::vector<mods::AnyOp> parse_inline_mods(const nlohmann::json& arr) {
    std::vector<mods::AnyOp> ops;
    for (const auto& j : arr) {
        try {
            // Re-wrap with "type" key if using shorthand { "mirror": {...} }
            if (j.contains("type")) {
                ops.push_back(mods::detail::parse_op(j));
            } else {
                // Shorthand: first key is the op type
                for (auto it = j.begin(); it != j.end(); ++it) {
                    nlohmann::json wrapped = it.value();
                    wrapped["type"] = it.key();
                    ops.push_back(mods::detail::parse_op(wrapped));
                    break;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[ChartScript] Warning: skipping invalid mod op: " << e.what() << "\n";
        }
    }
    return ops;
}

inline Item parse_item(const nlohmann::json& j) {
    Item item;
    item.input    = j.value("input", std::string(""));
    item.bgm      = j.value("bgm", std::string(""));
    item.bg        = j.value("bg", std::string(""));
    item.start     = j.value("start", 0.0);
    item.end       = j.value("end", -1.0);
    item.start_at  = j.value("start_at", 0.0);
    item.name      = j.value("name", std::string(""));
    item.level     = j.value("level", std::string(""));
    item.mod_file  = j.value("mod_file", std::string(""));

    if (j.contains("config") && j["config"].is_object())
        item.config = j["config"];

    if (j.contains("mods") && j["mods"].is_array())
        item.inline_mods = parse_inline_mods(j["mods"]);

    if (j.contains("filter") && j["filter"].is_object())
        item.filter = parse_filter(j["filter"]);

    return item;
}

inline Discover parse_discover(const nlohmann::json& j) {
    Discover d;
    d.enabled   = true;
    d.directory = j.value("directory", std::string("charts/"));
    d.recursive = j.value("recursive", true);

    if (j.contains("levels") && j["levels"].is_array()) {
        for (const auto& lv : j["levels"])
            d.levels.push_back(lv.get<std::string>());
    }

    d.sort_by = parse_sort_by(j.value("sort_by", std::string("name")));
    return d;
}

} // namespace detail

// Parse a Script from a JSON object.
inline Script parse_script(const nlohmann::json& j) {
    Script s;
    s.version      = j.value("version", 1);
    s.name         = j.value("name", std::string("Untitled"));
    s.mode         = detail::parse_play_mode(j.value("mode", std::string("sequence")));
    s.shuffle_seed = j.value("shuffle_seed", 0);
    s.repeat       = j.value("repeat", 1);

    if (j.contains("transition") && j["transition"].is_object()) {
        auto& t = j["transition"];
        s.transition.type     = detail::parse_transition_type(t.value("type", std::string("none")));
        s.transition.duration = t.value("duration", 0.5);
    }

    if (j.contains("defaults") && j["defaults"].is_object())
        s.defaults = j["defaults"];

    if (j.contains("discover") && j["discover"].is_object())
        s.discover = detail::parse_discover(j["discover"]);

    if (j.contains("items") && j["items"].is_array()) {
        for (const auto& item_j : j["items"])
            s.items.push_back(detail::parse_item(item_j));
    }

    return s;
}

// Load a Script from a .chartscript.json file.
inline Script load_script(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("chartscript: cannot open '" + path + "'");
    std::string text((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    // Support JSONC comments
    auto j = nlohmann::json::parse(config::strip_jsonc_comments(text));
    return parse_script(j);
}

// ── Discovery ────────────────────────────────────────────────────────────────
// Scan a directory for chart files and generate Items.

inline std::vector<Item> discover_charts(const Discover& d) {
    namespace fs = std::filesystem;
    std::vector<Item> items;

    if (!fs::is_directory(d.directory)) {
        std::cerr << "[ChartScript] discover directory not found: " << d.directory << "\n";
        return items;
    }

    auto is_chart_file = [](const fs::path& p) {
        auto ext = p.extension().string();
        return ext == ".json" || ext == ".pec" || ext == ".phbc";
    };

    auto is_pack_file = [](const fs::path& p) {
        auto ext = p.extension().string();
        return ext == ".zip" || ext == ".pez";
    };

    auto level_allowed = [&](const std::string& stem) {
        if (d.levels.empty()) return true;
        std::string upper = stem;
        for (auto& c : upper) c = static_cast<char>(std::toupper(c));
        for (const auto& lv : d.levels)
            if (upper == lv) return true;
        return false;
    };

    auto walker = d.recursive ? fs::recursive_directory_iterator(d.directory)
                              : fs::recursive_directory_iterator(d.directory,
                                    fs::directory_options::none);

    for (const auto& entry : fs::recursive_directory_iterator(d.directory)) {
        if (!entry.is_regular_file()) continue;
        auto p = entry.path();

        if (is_pack_file(p)) {
            Item it;
            it.input = p.string();
            it.name  = p.stem().string();
            items.push_back(std::move(it));
            continue;
        }

        if (is_chart_file(p)) {
            auto stem = p.stem().string();
            // Skip metadata files
            std::string lower = stem;
            for (auto& c : lower) c = static_cast<char>(std::tolower(c));
            if (lower == "info" || lower == "meta") continue;

            if (!level_allowed(stem)) continue;

            Item it;
            it.input = p.string();
            it.name  = p.parent_path().filename().string();
            it.level = stem;
            items.push_back(std::move(it));
        }
    }

    return items;
}

// ── Ordering / Shuffling ─────────────────────────────────────────────────────

inline void apply_ordering(Script& script) {
    // Merge discovered items into script.items
    if (script.discover.enabled) {
        auto discovered = discover_charts(script.discover);
        script.items.insert(script.items.end(), discovered.begin(), discovered.end());
    }

    switch (script.mode) {
    case PlayMode::Shuffle: {
        unsigned seed = script.shuffle_seed
            ? static_cast<unsigned>(script.shuffle_seed)
            : std::random_device{}();
        std::mt19937 rng(seed);
        std::shuffle(script.items.begin(), script.items.end(), rng);
        break;
    }
    case PlayMode::Loop:
    case PlayMode::Sequence:
    default:
        // Sort by name for deterministic order
        if (script.discover.enabled) {
            switch (script.discover.sort_by) {
            case SortBy::Name:
                std::sort(script.items.begin(), script.items.end(),
                    [](const Item& a, const Item& b) { return a.input < b.input; });
                break;
            case SortBy::Notes:
                std::sort(script.items.begin(), script.items.end(),
                    [](const Item& a, const Item& b) { return a.total_notes < b.total_notes; });
                break;
            case SortBy::Difficulty:
                std::sort(script.items.begin(), script.items.end(),
                    [](const Item& a, const Item& b) { return a.level < b.level; });
                break;
            case SortBy::Random: {
                unsigned seed = script.shuffle_seed
                    ? static_cast<unsigned>(script.shuffle_seed)
                    : std::random_device{}();
                std::mt19937 rng(seed);
                std::shuffle(script.items.begin(), script.items.end(), rng);
                break;
            }
            }
        }
        break;
    }
}

// ── Config merging ───────────────────────────────────────────────────────────
// Apply partial JSON overrides onto a base RenderConfig.

inline void apply_config_overrides(config::RenderConfig& cfg, const nlohmann::json& overrides) {
    if (overrides.is_null() || overrides.empty()) return;

    auto get_d = [&](const char* k, double& v) {
        if (overrides.contains(k) && !overrides[k].is_null()) v = overrides[k].get<double>();
    };
    auto get_b = [&](const char* k, bool& v) {
        if (overrides.contains(k) && !overrides[k].is_null()) v = overrides[k].get<bool>();
    };
    auto get_i = [&](const char* k, int& v) {
        if (overrides.contains(k) && !overrides[k].is_null()) v = overrides[k].get<int>();
    };

    get_d("chart_speed", cfg.chart_speed);
    get_d("approach", cfg.approach);
    get_d("expand", cfg.expand_factor);
    get_d("note_scale_x", cfg.note_scale_x);
    get_d("note_scale_y", cfg.note_scale_y);
    get_d("note_alpha", cfg.note_alpha);
    get_d("overrender", cfg.overrender);
    get_b("no_cull", cfg.no_cull);
    get_b("note_outline", cfg.note_outline);
    get_b("show_hitfx", cfg.show_hitfx);
    get_b("show_particles", cfg.show_particles);
    get_i("particle_count", cfg.particle_count);
    get_d("hitfx_intensity", cfg.hitfx_intensity);

    // Trail overrides
    auto opt_d = [&](const char* k, std::optional<double>& v) {
        if (overrides.contains(k) && !overrides[k].is_null()) v = overrides[k].get<double>();
    };
    auto opt_i = [&](const char* k, std::optional<int>& v) {
        if (overrides.contains(k) && !overrides[k].is_null()) v = overrides[k].get<int>();
    };
    auto opt_s = [&](const char* k, std::optional<std::string>& v) {
        if (overrides.contains(k) && !overrides[k].is_null()) v = overrides[k].get<std::string>();
    };

    opt_d("trail_alpha", cfg.trail_alpha);
    opt_i("trail_frames", cfg.trail_frames);
    opt_d("trail_decay", cfg.trail_decay);
    opt_i("trail_blur", cfg.trail_blur);
    opt_i("trail_dim", cfg.trail_dim);
    opt_s("trail_blend", cfg.trail_blend);
    opt_d("trail_chromatic", cfg.trail_chromatic);
    opt_d("trail_glow", cfg.trail_glow);
    opt_s("trail_decay_curve", cfg.trail_decay_curve);
    opt_i("trail_blur_quality", cfg.trail_blur_quality);
}

// Build a per-item RenderConfig by merging base → defaults → item config.
inline config::RenderConfig build_item_config(
    const config::RenderConfig& base,
    const nlohmann::json& defaults,
    const nlohmann::json& item_config)
{
    config::RenderConfig cfg = base;
    apply_config_overrides(cfg, defaults);
    apply_config_overrides(cfg, item_config);
    return cfg;
}

// ── Mod resolution ───────────────────────────────────────────────────────────
// Collect all mods for an item (inline + file reference).

inline mods::Mod resolve_item_mods(const Item& item) {
    mods::Mod mod;
    mod.name = "chartscript:" + item.name;
    mod.ops  = item.inline_mods;

    if (!item.mod_file.empty()) {
        try {
            auto file_mod = mods::load_mod(item.mod_file);
            mod.ops.insert(mod.ops.end(), file_mod.ops.begin(), file_mod.ops.end());
        } catch (const std::exception& e) {
            std::cerr << "[ChartScript] Warning: failed to load mod '"
                      << item.mod_file << "': " << e.what() << "\n";
        }
    }

    return mod;
}

// ── Script summary ───────────────────────────────────────────────────────────

inline void print_script_summary(const Script& s) {
    const char* mode_str = "sequence";
    if (s.mode == PlayMode::Shuffle) mode_str = "shuffle";
    if (s.mode == PlayMode::Loop)    mode_str = "loop";

    std::cout << "[ChartScript] \"" << s.name << "\" — "
              << s.items.size() << " items, mode=" << mode_str;
    if (s.repeat != 1) std::cout << ", repeat=" << (s.repeat == 0 ? "∞" : std::to_string(s.repeat));
    if (s.transition.type != TransitionType::None) {
        const char* t = s.transition.type == TransitionType::Fade ? "fade" : "crossfade";
        std::cout << ", transition=" << t << "(" << s.transition.duration << "s)";
    }
    std::cout << "\n";

    for (size_t i = 0; i < std::min(s.items.size(), size_t(10)); ++i) {
        const auto& it = s.items[i];
        std::cout << "  [" << (i + 1) << "] " << it.input;
        if (!it.name.empty()) std::cout << " (" << it.name << ")";
        if (!it.level.empty()) std::cout << " [" << it.level << "]";
        if (it.end > 0) std::cout << " " << it.start << "s-" << it.end << "s";
        if (!it.inline_mods.empty()) std::cout << " +" << it.inline_mods.size() << " mods";
        if (!it.config.empty()) std::cout << " +config";
        std::cout << "\n";
    }
    if (s.items.size() > 10)
        std::cout << "  ... and " << (s.items.size() - 10) << " more\n";
}

} // namespace phigros::chartscript
