#pragma once
// chartscript.hpp — Declarative chart playback scripting system (v2).
//
// Features:
//   Playlist modes  — sequence | shuffle (weighted) | loop
//   Per-item        — segments[], notes_window, config overrides, inline mods,
//                     on_complete action, weight, tags
//   Groups          — named shared config+mods; items reference by group name
//   Variables       — "$name" substitution in string config values
//   Presets         — named config shortcuts; item config can extend a preset
//   Filters         — global_filter + per-item filter; applied after discovery
//   Discovery       — directory scan, level filter, recursive, limit, sort
//   Resume          — save/restore cursor position to a JSON file across runs
//
// .chartscript.json format (JSONC — // comments allowed):
//   {
//     "version": 2,
//     "name": "My Playlist",
//     "mode": "shuffle",
//     "shuffle_seed": 0,           // 0 = random each run
//     "repeat": 0,                 // 0 = infinite, N = play N full passes
//     "discover_limit": 20,        // cap auto-discovered items
//     "resume_file": "resume.json",// save/restore cursor position
//
//     "variables": { "spd": 1.4, "glow": 0.4 },
//
//     "presets": {
//       "vibrant": { "trail_alpha": 0.7, "trail_glow": "$glow", "chart_speed": "$spd" }
//     },
//
//     "groups": {
//       "hype": {
//         "config": { "preset": "vibrant", "motion_blur_samples": 4 },
//         "mods": [{ "type": "colorize", "mode": "hue" }]
//       }
//     },
//
//     "global_filter": { "min_notes": 100, "levels": ["AT","IN"] },
//
//     "defaults": { "chart_speed": 1.0, "trail_alpha": 0.5 },
//
//     "transition": { "type": "fade", "duration": 0.5 },
//
//     "discover": {
//       "directory": "charts/",
//       "levels": ["AT","IN"],
//       "recursive": true,
//       "sort_by": "name",          // name|notes|difficulty|random
//       "limit": 50
//     },
//
//     "items": [
//       {
//         "input": "charts/song/AT.json",
//         "name": "Song",
//         "group": "hype",
//         "tags": ["fast", "featured"],
//         "weight": 3,
//         "notes_window": 200,     // auto-compute end from first 200 notes
//         "tail_time": 1.0,
//         "segments": [            // multiple play windows (overrides start/end)
//           { "start": 0.0,  "end": 15.0 },
//           { "start": 60.0, "end": 75.0 }
//         ],
//         "config": { "preset": "vibrant", "chart_speed": 1.8 },
//         "mods": [{ "type": "mirror" }],
//         "on_complete": {
//           "action": "next",
//           "min_score": 900000,   // if score >= this: action; else: else_action
//           "else_action": "repeat"
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
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace phigros::chartscript {

// ── Transition ───────────────────────────────────────────────────────────────

enum class TransitionType : uint8_t { None, Fade, Crossfade };

struct Transition {
    TransitionType type = TransitionType::None;
    double duration = 0.5;
};

// ── Filter ───────────────────────────────────────────────────────────────────

struct Filter {
    std::vector<std::string> levels;
    int min_notes = -1;
    int max_notes = -1;
    std::string name_contains;
    std::vector<std::string> tags_any;  // item must have at least one of these tags

    bool empty() const {
        return levels.empty() && min_notes < 0 && max_notes < 0
            && name_contains.empty() && tags_any.empty();
    }
};

// ── Segment ──────────────────────────────────────────────────────────────────

struct Segment {
    double start = 0.0;
    double end   = -1.0;     // -1 = run to chart end
    int    notes_window = -1; // auto-compute end from first N notes
    double tail_time    = 0.5;
};

// ── OnComplete action ────────────────────────────────────────────────────────

enum class CompleteAction : uint8_t { Next, Loop, Stop, Goto, Repeat };

struct OnComplete {
    CompleteAction action      = CompleteAction::Next;
    int            goto_index  = -1;   // for Goto
    int            min_score   = -1;   // if >= this use action; else else_action
    CompleteAction else_action = CompleteAction::Next;

    bool has_condition() const { return min_score >= 0; }
};

// ── Group ────────────────────────────────────────────────────────────────────

struct Group {
    nlohmann::json config;
    std::vector<mods::AnyOp> mods;
};

// ── Item ─────────────────────────────────────────────────────────────────────

struct Item {
    std::string input;
    std::string bgm;
    std::string bg;

    // Simple start/end (used when segments is empty)
    double start       = 0.0;
    double end         = -1.0;
    double start_at    = 0.0;
    int    notes_window = -1;  // auto-compute end from N notes
    double tail_time   = 0.5;

    // Multiple windows within one chart (overrides start/end)
    std::vector<Segment> segments;

    // Config / mods
    nlohmann::json       config;
    std::vector<mods::AnyOp> inline_mods;
    std::string          mod_file;

    // Metadata
    std::string name;
    std::string level;
    int         total_notes = -1;
    std::vector<std::string> tags;

    // Shuffle weight (higher = selected more often in weighted shuffle)
    int weight = 1;

    // Group reference (inherits config + mods from group)
    std::string group;

    // Action after this item completes
    OnComplete on_complete;

    // Filter (for use with discover — item is skipped if filter fails)
    Filter filter;

    // Internal: which group was already applied
    bool _group_applied = false;
};

// ── Discover ─────────────────────────────────────────────────────────────────

enum class SortBy : uint8_t { Name, Notes, Difficulty, Random };

struct Discover {
    std::string directory;
    std::vector<std::string> levels;
    bool    recursive = true;
    SortBy  sort_by   = SortBy::Name;
    int     limit     = -1;
    bool    enabled   = false;
};

// ── PlayMode ─────────────────────────────────────────────────────────────────

enum class PlayMode : uint8_t { Sequence, Shuffle, Loop };

// ── Script ───────────────────────────────────────────────────────────────────

struct Script {
    int         version      = 2;
    std::string name;
    PlayMode    mode         = PlayMode::Sequence;
    int         shuffle_seed = 0;
    int         repeat       = 1;
    int         discover_limit = -1;
    std::string resume_file;

    Transition  transition;
    Filter      global_filter;
    nlohmann::json defaults;
    Discover    discover;
    std::vector<Item> items;

    std::map<std::string, Group>          groups;
    std::map<std::string, nlohmann::json> presets;
    std::map<std::string, nlohmann::json> variables;
};

// ─────────────────────────────────────────────────────────────────────────────
// ── Parsing helpers ───────────────────────────────────────────────────────────

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
    if (s == "difficulty") return SortBy::Difficulty;
    if (s == "random")     return SortBy::Random;
    return SortBy::Name;
}

inline CompleteAction parse_complete_action(const std::string& s) {
    if (s == "loop")   return CompleteAction::Loop;
    if (s == "stop")   return CompleteAction::Stop;
    if (s == "goto")   return CompleteAction::Goto;
    if (s == "repeat") return CompleteAction::Repeat;
    return CompleteAction::Next;
}

inline Filter parse_filter(const nlohmann::json& j) {
    Filter f;
    if (j.contains("levels") && j["levels"].is_array())
        for (const auto& lv : j["levels"])
            f.levels.push_back(lv.get<std::string>());
    if (j.contains("min_notes") && !j["min_notes"].is_null()) f.min_notes = j["min_notes"].get<int>();
    if (j.contains("max_notes") && !j["max_notes"].is_null()) f.max_notes = j["max_notes"].get<int>();
    if (j.contains("name_contains") && !j["name_contains"].is_null())
        f.name_contains = j["name_contains"].get<std::string>();
    if (j.contains("tags_any") && j["tags_any"].is_array())
        for (const auto& t : j["tags_any"])
            f.tags_any.push_back(t.get<std::string>());
    return f;
}

inline Segment parse_segment(const nlohmann::json& j) {
    Segment s;
    s.start        = j.value("start", 0.0);
    s.end          = j.value("end", -1.0);
    s.notes_window = j.value("notes_window", -1);
    s.tail_time    = j.value("tail_time", 0.5);
    return s;
}

inline OnComplete parse_on_complete(const nlohmann::json& j) {
    OnComplete oc;
    oc.action      = parse_complete_action(j.value("action",      std::string("next")));
    oc.else_action = parse_complete_action(j.value("else_action", std::string("next")));
    oc.goto_index  = j.value("goto", -1);
    oc.min_score   = j.value("min_score", -1);
    return oc;
}

inline std::vector<mods::AnyOp> parse_inline_mods(const nlohmann::json& arr) {
    std::vector<mods::AnyOp> ops;
    for (const auto& j : arr) {
        try {
            if (j.contains("type")) {
                ops.push_back(mods::detail::parse_op(j));
            } else {
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
    item.input        = j.value("input",    std::string(""));
    item.bgm          = j.value("bgm",      std::string(""));
    item.bg           = j.value("bg",       std::string(""));
    item.start        = j.value("start",    0.0);
    item.end          = j.value("end",      -1.0);
    item.start_at     = j.value("start_at", 0.0);
    item.notes_window = j.value("notes_window", -1);
    item.tail_time    = j.value("tail_time", 0.5);
    item.name         = j.value("name",     std::string(""));
    item.level        = j.value("level",    std::string(""));
    item.mod_file     = j.value("mod_file", std::string(""));
    item.group        = j.value("group",    std::string(""));
    item.weight       = std::max(1, j.value("weight", 1));

    if (j.contains("tags") && j["tags"].is_array())
        for (const auto& t : j["tags"]) item.tags.push_back(t.get<std::string>());

    if (j.contains("config") && j["config"].is_object())
        item.config = j["config"];

    if (j.contains("mods") && j["mods"].is_array())
        item.inline_mods = parse_inline_mods(j["mods"]);

    if (j.contains("segments") && j["segments"].is_array())
        for (const auto& s : j["segments"])
            item.segments.push_back(parse_segment(s));

    if (j.contains("on_complete") && j["on_complete"].is_object())
        item.on_complete = parse_on_complete(j["on_complete"]);

    if (j.contains("filter") && j["filter"].is_object())
        item.filter = parse_filter(j["filter"]);

    return item;
}

inline Group parse_group(const nlohmann::json& j) {
    Group g;
    if (j.contains("config") && j["config"].is_object()) g.config = j["config"];
    if (j.contains("mods") && j["mods"].is_array()) g.mods = parse_inline_mods(j["mods"]);
    return g;
}

inline Discover parse_discover(const nlohmann::json& j) {
    Discover d;
    d.enabled   = true;
    d.directory = j.value("directory", std::string("charts/"));
    d.recursive = j.value("recursive", true);
    d.limit     = j.value("limit", -1);
    if (j.contains("levels") && j["levels"].is_array())
        for (const auto& lv : j["levels"]) d.levels.push_back(lv.get<std::string>());
    d.sort_by = parse_sort_by(j.value("sort_by", std::string("name")));
    return d;
}

} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// ── Variable substitution ─────────────────────────────────────────────────────
// Replaces "$name" string values in JSON using the variables map.

inline void apply_variable_sub(
    nlohmann::json& j,
    const std::map<std::string, nlohmann::json>& vars)
{
    if (vars.empty()) return;
    if (j.is_string()) {
        const std::string& s = j.get_ref<const std::string&>();
        if (!s.empty() && s[0] == '$') {
            auto key = s.substr(1);
            auto it = vars.find(key);
            if (it != vars.end()) j = it->second;
        }
    } else if (j.is_object()) {
        for (auto& [k, v] : j.items()) apply_variable_sub(v, vars);
    } else if (j.is_array()) {
        for (auto& elem : j) apply_variable_sub(elem, vars);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Preset resolution ─────────────────────────────────────────────────────────
// If item.config contains "preset": "name", merge the preset into the config.

inline void resolve_preset(
    nlohmann::json& cfg,
    const std::map<std::string, nlohmann::json>& presets)
{
    if (!cfg.contains("preset") || !cfg["preset"].is_string()) return;
    const std::string pname = cfg["preset"].get<std::string>();
    cfg.erase("preset");
    auto it = presets.find(pname);
    if (it == presets.end()) {
        std::cerr << "[ChartScript] Warning: preset '" << pname << "' not found\n";
        return;
    }
    // Preset values fill in only if not already set by the item
    nlohmann::json resolved = it->second;
    for (auto& [k, v] : cfg.items()) resolved[k] = v;
    cfg = resolved;
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Filter application ────────────────────────────────────────────────────────

inline bool filter_item(const Item& item, const Filter& f) {
    if (f.empty()) return true;

    if (!f.levels.empty()) {
        std::string lv_up = item.level;
        for (auto& c : lv_up) c = static_cast<char>(std::toupper(c));
        bool found = false;
        for (const auto& lv : f.levels) {
            std::string fl_up = lv;
            for (auto& c : fl_up) c = static_cast<char>(std::toupper(c));
            if (lv_up == fl_up) { found = true; break; }
        }
        if (!found) return false;
    }

    if (f.min_notes >= 0 && item.total_notes >= 0 && item.total_notes < f.min_notes)
        return false;
    if (f.max_notes >= 0 && item.total_notes >= 0 && item.total_notes > f.max_notes)
        return false;

    if (!f.name_contains.empty()) {
        std::string nm = item.name;
        std::string pat = f.name_contains;
        for (auto& c : nm)  c = static_cast<char>(std::tolower(c));
        for (auto& c : pat) c = static_cast<char>(std::tolower(c));
        if (nm.find(pat) == std::string::npos) return false;
    }

    if (!f.tags_any.empty()) {
        std::set<std::string> item_tags(item.tags.begin(), item.tags.end());
        bool any = false;
        for (const auto& t : f.tags_any)
            if (item_tags.count(t)) { any = true; break; }
        if (!any) return false;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Notes-window end time ─────────────────────────────────────────────────────
// Given a flat vector of note hit times, return time after the Nth note.

inline double notes_window_end(
    const std::vector<double>& sorted_hit_times,
    int n_notes,
    double tail_time = 0.5)
{
    if (n_notes <= 0 || sorted_hit_times.empty()) return -1.0;
    int idx = std::min(n_notes, static_cast<int>(sorted_hit_times.size())) - 1;
    return sorted_hit_times[static_cast<size_t>(idx)] + tail_time;
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Resume state ──────────────────────────────────────────────────────────────

inline int load_resume(const std::string& path) {
    if (path.empty()) return 0;
    try {
        std::ifstream f(path);
        if (!f) return 0;
        auto j = nlohmann::json::parse(f);
        return j.value("cursor", 0);
    } catch (...) { return 0; }
}

inline void save_resume(const std::string& path, int cursor) {
    if (path.empty()) return;
    try {
        nlohmann::json j;
        j["cursor"] = cursor;
        std::ofstream f(path);
        f << j.dump(2) << "\n";
    } catch (...) {}
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Discovery ─────────────────────────────────────────────────────────────────

inline std::vector<Item> discover_charts(const Discover& d) {
    namespace fs = std::filesystem;
    std::vector<Item> items;

    if (!fs::is_directory(d.directory)) {
        std::cerr << "[ChartScript] discover directory not found: " << d.directory << "\n";
        return items;
    }

    auto level_allowed = [&](const std::string& stem) {
        if (d.levels.empty()) return true;
        std::string upper = stem;
        for (auto& c : upper) c = static_cast<char>(std::toupper(c));
        for (const auto& lv : d.levels) {
            std::string lu = lv;
            for (auto& c : lu) c = static_cast<char>(std::toupper(c));
            if (upper == lu) return true;
        }
        return false;
    };

    auto try_add_chart = [&](const fs::path& p) {
        auto stem = p.stem().string();
        std::string low = stem;
        for (auto& c : low) c = static_cast<char>(std::tolower(c));
        if (low == "info" || low == "meta") return;
        if (!level_allowed(stem)) return;
        Item it;
        it.input = p.string();
        it.name  = p.parent_path().filename().string();
        it.level = stem;
        items.push_back(std::move(it));
    };

    if (d.recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(d.directory)) {
            if (!entry.is_regular_file()) continue;
            auto p = entry.path();
            auto ext = p.extension().string();
            if (ext == ".zip" || ext == ".pez") {
                Item it; it.input = p.string(); it.name = p.stem().string();
                items.push_back(std::move(it));
            } else if (ext == ".json" || ext == ".pec" || ext == ".phbc") {
                try_add_chart(p);
            }
        }
    } else {
        for (const auto& entry : fs::directory_iterator(d.directory)) {
            if (!entry.is_regular_file()) continue;
            auto p = entry.path();
            auto ext = p.extension().string();
            if (ext == ".zip" || ext == ".pez") {
                Item it; it.input = p.string(); it.name = p.stem().string();
                items.push_back(std::move(it));
            } else if (ext == ".json" || ext == ".pec" || ext == ".phbc") {
                try_add_chart(p);
            }
        }
    }

    // Apply discover limit
    if (d.limit > 0 && static_cast<int>(items.size()) > d.limit)
        items.resize(static_cast<size_t>(d.limit));

    return items;
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Parse & load ──────────────────────────────────────────────────────────────

inline Script parse_script(const nlohmann::json& j) {
    Script s;
    s.version       = j.value("version", 2);
    s.name          = j.value("name", std::string("Untitled"));
    s.mode          = detail::parse_play_mode(j.value("mode", std::string("sequence")));
    s.shuffle_seed  = j.value("shuffle_seed", 0);
    s.repeat        = j.value("repeat", 1);
    s.discover_limit = j.value("discover_limit", -1);
    s.resume_file   = j.value("resume_file", std::string(""));

    if (j.contains("transition") && j["transition"].is_object()) {
        auto& t = j["transition"];
        s.transition.type     = detail::parse_transition_type(t.value("type", std::string("none")));
        s.transition.duration = t.value("duration", 0.5);
    }
    if (j.contains("defaults") && j["defaults"].is_object()) s.defaults = j["defaults"];
    if (j.contains("global_filter") && j["global_filter"].is_object())
        s.global_filter = detail::parse_filter(j["global_filter"]);
    if (j.contains("discover") && j["discover"].is_object())
        s.discover = detail::parse_discover(j["discover"]);

    // Variables
    if (j.contains("variables") && j["variables"].is_object())
        for (auto& [k, v] : j["variables"].items())
            s.variables[k] = v;

    // Presets
    if (j.contains("presets") && j["presets"].is_object())
        for (auto& [k, v] : j["presets"].items())
            s.presets[k] = v;

    // Groups
    if (j.contains("groups") && j["groups"].is_object())
        for (auto& [k, v] : j["groups"].items())
            s.groups[k] = detail::parse_group(v);

    if (j.contains("items") && j["items"].is_array())
        for (const auto& item_j : j["items"])
            s.items.push_back(detail::parse_item(item_j));

    return s;
}

inline Script load_script(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("chartscript: cannot open '" + path + "'");
    std::string text((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    auto j = nlohmann::json::parse(config::strip_jsonc_comments(text));
    return parse_script(j);
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Post-load processing ──────────────────────────────────────────────────────

// Apply variable substitution to all string values in a JSON blob.
inline void apply_vars_to_json(
    nlohmann::json& j,
    const std::map<std::string, nlohmann::json>& vars)
{
    apply_variable_sub(j, vars);
}

// Resolve groups: merge group config+mods into each item that references a group.
inline void resolve_groups(Script& script) {
    for (auto& item : script.items) {
        if (item.group.empty() || item._group_applied) continue;
        auto it = script.groups.find(item.group);
        if (it == script.groups.end()) {
            std::cerr << "[ChartScript] Warning: group '" << item.group << "' not found\n";
            continue;
        }
        const Group& g = it->second;
        // Group config fills in only keys not set by the item
        if (!g.config.empty()) {
            nlohmann::json merged = g.config;
            for (auto& [k, v] : item.config.items()) merged[k] = v;
            item.config = merged;
        }
        // Group mods are prepended
        if (!g.mods.empty()) {
            std::vector<mods::AnyOp> combined = g.mods;
            combined.insert(combined.end(), item.inline_mods.begin(), item.inline_mods.end());
            item.inline_mods = std::move(combined);
        }
        item._group_applied = true;
    }
}

// Resolve presets in item.config and apply variable substitution.
inline void resolve_item_config(
    Item& item,
    const std::map<std::string, nlohmann::json>& presets,
    const std::map<std::string, nlohmann::json>& vars)
{
    apply_vars_to_json(item.config, vars);
    resolve_preset(item.config, presets);
}

// Apply global_filter to all items, removing those that fail.
inline void apply_global_filter(Script& script) {
    if (script.global_filter.empty()) return;
    auto& items = script.items;
    items.erase(std::remove_if(items.begin(), items.end(),
        [&](const Item& it) { return !filter_item(it, script.global_filter); }),
        items.end());
}

// Apply discover_limit to capped discovered+existing items.
inline void apply_discover_limit(Script& script) {
    if (script.discover_limit > 0 &&
        static_cast<int>(script.items.size()) > script.discover_limit)
        script.items.resize(static_cast<size_t>(script.discover_limit));
}

// Weighted shuffle: items with higher weight are picked more frequently.
// Implements a weighted sample without replacement using random_device.
inline void weighted_shuffle(std::vector<Item>& items, unsigned seed) {
    if (items.empty()) return;
    std::mt19937 rng(seed ? seed : std::random_device{}());
    // Assign each item a random key inversely proportional to weight
    // (higher weight → lower key → sorted earlier)
    std::vector<std::pair<double, size_t>> keyed;
    keyed.reserve(items.size());
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (size_t i = 0; i < items.size(); ++i) {
        double w = std::max(1, items[i].weight);
        double key = -std::log(dist(rng)) / w;  // Gumbel-max trick
        keyed.push_back({key, i});
    }
    std::sort(keyed.begin(), keyed.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Item> reordered;
    reordered.reserve(items.size());
    for (auto& [k, i] : keyed) reordered.push_back(std::move(items[i]));
    items = std::move(reordered);
}

// Main ordering entry point: discover → filter → resolve → sort/shuffle.
inline void apply_ordering(Script& script) {
    // Discover and append new items
    if (script.discover.enabled) {
        auto discovered = discover_charts(script.discover);
        script.items.insert(script.items.end(), discovered.begin(), discovered.end());
    }

    // Resolve groups + presets + variables for all items
    resolve_groups(script);
    for (auto& item : script.items)
        resolve_item_config(item, script.presets, script.variables);

    // Apply global_filter and discover_limit
    apply_global_filter(script);
    apply_discover_limit(script);

    // Sort / shuffle
    switch (script.mode) {
    case PlayMode::Shuffle:
        weighted_shuffle(script.items,
            static_cast<unsigned>(script.shuffle_seed));
        break;
    case PlayMode::Loop:
    case PlayMode::Sequence:
    default:
        if (script.discover.enabled || script.mode == PlayMode::Loop) {
            switch (script.discover.sort_by) {
            case SortBy::Notes:
                std::stable_sort(script.items.begin(), script.items.end(),
                    [](const Item& a, const Item& b) {
                        return a.total_notes < b.total_notes; });
                break;
            case SortBy::Difficulty:
                std::stable_sort(script.items.begin(), script.items.end(),
                    [](const Item& a, const Item& b) { return a.level < b.level; });
                break;
            case SortBy::Random:
                weighted_shuffle(script.items,
                    static_cast<unsigned>(script.shuffle_seed));
                break;
            case SortBy::Name:
            default:
                std::stable_sort(script.items.begin(), script.items.end(),
                    [](const Item& a, const Item& b) { return a.input < b.input; });
                break;
            }
        }
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Config merging ────────────────────────────────────────────────────────────

inline void apply_config_overrides(config::RenderConfig& cfg, const nlohmann::json& ov) {
    if (ov.is_null() || ov.empty()) return;
    auto get_d = [&](const char* k, double& v) {
        if (ov.contains(k) && !ov[k].is_null()) v = ov[k].get<double>(); };
    auto get_b = [&](const char* k, bool& v) {
        if (ov.contains(k) && !ov[k].is_null()) v = ov[k].get<bool>(); };
    auto get_i = [&](const char* k, int& v) {
        if (ov.contains(k) && !ov[k].is_null()) v = ov[k].get<int>(); };
    auto get_s = [&](const char* k, std::string& v) {
        if (ov.contains(k) && !ov[k].is_null()) v = ov[k].get<std::string>(); };
    auto opt_d = [&](const char* k, std::optional<double>& v) {
        if (ov.contains(k) && !ov[k].is_null()) v = ov[k].get<double>(); };
    auto opt_i = [&](const char* k, std::optional<int>& v) {
        if (ov.contains(k) && !ov[k].is_null()) v = ov[k].get<int>(); };
    auto opt_s = [&](const char* k, std::optional<std::string>& v) {
        if (ov.contains(k) && !ov[k].is_null()) v = ov[k].get<std::string>(); };

    get_d("chart_speed",             cfg.chart_speed);
    get_d("approach",                cfg.approach);
    get_d("expand",                  cfg.expand_factor);
    get_d("note_scale_x",            cfg.note_scale_x);
    get_d("note_scale_y",            cfg.note_scale_y);
    get_d("note_alpha",              cfg.note_alpha);
    get_d("overrender",              cfg.overrender);
    get_b("no_cull",                 cfg.no_cull);
    get_b("no_cull_screen",          cfg.no_cull_screen);
    get_b("no_cull_enter_time",      cfg.no_cull_enter_time);
    get_b("note_outline",            cfg.note_outline);
    get_b("show_hitfx",              cfg.show_hitfx);
    get_b("show_particles",          cfg.show_particles);
    get_i("particle_count",          cfg.particle_count);
    get_d("hitfx_intensity",         cfg.hitfx_intensity);
    get_i("bg_dim",                  cfg.bg_dim);
    get_s("backend",                 cfg.backend);

    opt_d("trail_alpha",             cfg.trail_alpha);
    opt_i("trail_frames",            cfg.trail_frames);
    opt_d("trail_decay",             cfg.trail_decay);
    opt_i("trail_blur",              cfg.trail_blur);
    opt_i("trail_dim",               cfg.trail_dim);
    opt_s("trail_blend",             cfg.trail_blend);
    opt_d("trail_chromatic",         cfg.trail_chromatic);
    opt_d("trail_glow",              cfg.trail_glow);
    opt_s("trail_decay_curve",       cfg.trail_decay_curve);
    opt_i("trail_blur_quality",      cfg.trail_blur_quality);
    opt_i("motion_blur_samples",     cfg.motion_blur_samples);
    opt_d("motion_blur_shutter",     cfg.motion_blur_shutter);
    opt_s("motion_blur_curve",       cfg.motion_blur_curve);
}

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

// ─────────────────────────────────────────────────────────────────────────────
// ── Mod resolution ────────────────────────────────────────────────────────────

inline mods::Mod resolve_item_mods(const Item& item) {
    mods::Mod mod;
    mod.name = "chartscript:" + item.name;
    mod.ops  = item.inline_mods;
    if (!item.mod_file.empty()) {
        try {
            auto fm = mods::load_mod(item.mod_file);
            mod.ops.insert(mod.ops.end(), fm.ops.begin(), fm.ops.end());
        } catch (const std::exception& e) {
            std::cerr << "[ChartScript] Warning: failed to load mod '"
                      << item.mod_file << "': " << e.what() << "\n";
        }
    }
    return mod;
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Summary ───────────────────────────────────────────────────────────────────

inline void print_script_summary(const Script& s) {
    const char* mode_str =
        s.mode == PlayMode::Shuffle ? "shuffle" :
        s.mode == PlayMode::Loop    ? "loop"    : "sequence";
    std::cout << "[ChartScript] \"" << s.name << "\"  "
              << s.items.size() << " item(s)  mode=" << mode_str;
    if (s.repeat != 1) std::cout << "  repeat=" << (s.repeat == 0 ? "∞" : std::to_string(s.repeat));
    if (!s.resume_file.empty()) std::cout << "  resume=" << s.resume_file;
    if (s.transition.type != TransitionType::None) {
        const char* t = s.transition.type == TransitionType::Fade ? "fade" : "crossfade";
        std::cout << "  transition=" << t << "(" << s.transition.duration << "s)";
    }
    std::cout << "\n";

    for (size_t i = 0; i < std::min(s.items.size(), size_t(10)); ++i) {
        const auto& it = s.items[i];
        std::cout << "  [" << (i + 1) << "] " << it.input;
        if (!it.name.empty())  std::cout << " (" << it.name << ")";
        if (!it.level.empty()) std::cout << " [" << it.level << "]";
        if (it.notes_window > 0) std::cout << " notes≤" << it.notes_window;
        else if (it.end > 0) std::cout << " " << it.start << "s-" << it.end << "s";
        if (!it.segments.empty()) std::cout << " ×" << it.segments.size() << "segs";
        if (!it.group.empty())    std::cout << " @" << it.group;
        if (it.weight > 1)        std::cout << " w=" << it.weight;
        if (!it.inline_mods.empty()) std::cout << " +" << it.inline_mods.size() << "mods";
        std::cout << "\n";
    }
    if (s.items.size() > 10)
        std::cout << "  ... and " << (s.items.size() - 10) << " more\n";
}

} // namespace phigros::chartscript
