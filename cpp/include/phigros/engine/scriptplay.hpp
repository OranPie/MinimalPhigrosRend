#pragma once

#include "phigros/core/logger.hpp"
#include "phigros/core/types.hpp"
#include "phigros/engine/judge.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace phigros::engine {

namespace detail {

inline std::string strip_jsonc_comments_local(const std::string& src) {
    std::istringstream in(src);
    std::string out;
    std::string line;
    while (std::getline(in, line)) {
        bool in_str = false;
        for (size_t i = 0; i + 1 < line.size(); ++i) {
            if (line[i] == '"' && (i == 0 || line[i - 1] != '\\'))
                in_str = !in_str;
            if (!in_str && line[i] == '/' && line[i + 1] == '/') {
                line.resize(i);
                break;
            }
        }
        out += line;
        out.push_back('\n');
    }
    return out;
}

inline std::string upper_ascii(std::string s) {
    for (char& ch : s)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

inline int parse_kind_name(const std::string& raw) {
    const std::string s = upper_ascii(raw);
    if (s == "TAP") return 1;
    if (s == "DRAG") return 2;
    if (s == "HOLD") return 3;
    if (s == "FLICK") return 4;
    if (s == "ANY") return 0;
    throw std::runtime_error("scriptplay: unknown kind '" + raw + "'");
}

inline std::string normalize_grade(const std::string& raw) {
    const std::string s = upper_ascii(raw);
    if (s == "PERFECT" || s == "GOOD" || s == "BAD" || s == "MISS")
        return s;
    throw std::runtime_error("scriptplay: unknown judge grade '" + raw + "'");
}

inline bool contains_int(const std::vector<int>& values, int needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

} // namespace detail

struct ScriptPlayValue {
    enum class Kind : uint8_t { Fixed, Range, Sequence };

    Kind kind = Kind::Fixed;
    double fixed = 0.0;
    double min = 0.0;
    double max = 0.0;
    std::vector<double> sequence;

    double sample(size_t ordinal, size_t total) const {
        switch (kind) {
        case Kind::Fixed:
            return fixed;
        case Kind::Range:
            if (total <= 1) return (min + max) * 0.5;
            return min + (max - min) *
                (static_cast<double>(ordinal) / static_cast<double>(total - 1));
        case Kind::Sequence:
            if (sequence.empty()) return 0.0;
            return sequence[ordinal % sequence.size()];
        }
        return fixed;
    }
};

struct ScriptPlayFilter {
    std::optional<int> start_note_index;
    std::optional<int> end_note_index;
    std::vector<int> note_indexes;
    std::vector<int> note_ids;
    std::vector<int> line_ids;
    std::vector<int> kinds;
    std::optional<bool> above;
    std::optional<bool> fake;
    std::optional<bool> mh;
    std::optional<bool> playable;
};

struct ScriptPlayEntry {
    ScriptPlayFilter filter;
    std::optional<std::string> grade;
    std::optional<ScriptPlayValue> dt_ms;
    std::optional<ScriptPlayValue> hold_percent;
    std::optional<ScriptPlayValue> hold_ms;
};

struct ScriptPlayScript {
    int version = 1;
    std::string name;
    std::string index_mode = "playable";
    std::optional<int> require_total_notes;
    std::optional<int> require_playable_notes;
    std::vector<ScriptPlayEntry> entries;
};

struct ScriptPlayPlan {
    struct NotePlan {
        bool scripted = false;
        std::string grade = "PERFECT";
        double dt_ms = 0.0;
        std::optional<double> hold_percent;
        std::optional<double> hold_ms;
    };

    struct Event {
        enum class Type : uint8_t { Judge, HoldRelease };

        double t = 0.0;
        int note_idx = -1;
        Type type = Type::Judge;
        std::string grade;
    };

    ScriptPlayScript script;
    std::vector<NotePlan> note_plans;
    std::vector<Event> events;
};

inline ScriptPlayValue parse_scriptplay_value(const nlohmann::json& j,
                                              const std::string& field_name) {
    ScriptPlayValue out;
    if (j.is_number()) {
        out.kind = ScriptPlayValue::Kind::Fixed;
        out.fixed = j.get<double>();
        return out;
    }
    if (!j.is_object())
        throw std::runtime_error("scriptplay: field '" + field_name + "' must be a number or object");

    if (j.contains("values")) {
        if (!j["values"].is_array() || j["values"].empty())
            throw std::runtime_error("scriptplay: field '" + field_name + ".values' must be a non-empty array");
        out.kind = ScriptPlayValue::Kind::Sequence;
        if (j.contains("weights") && !j["weights"].is_null()) {
            const auto& values = j["values"];
            const auto& weights = j["weights"];
            if (!weights.is_array() || weights.size() != values.size())
                throw std::runtime_error("scriptplay: '" + field_name + ".weights' must match values length");
            for (size_t i = 0; i < values.size(); ++i) {
                const int w = std::max(0, weights[i].get<int>());
                for (int k = 0; k < w; ++k)
                    out.sequence.push_back(values[i].get<double>());
            }
            if (out.sequence.empty())
                throw std::runtime_error("scriptplay: '" + field_name + ".weights' produced an empty sequence");
        } else {
            for (const auto& value : j["values"])
                out.sequence.push_back(value.get<double>());
        }
        return out;
    }

    if (j.contains("min") || j.contains("max")) {
        if (!j.contains("min") || !j.contains("max"))
            throw std::runtime_error("scriptplay: field '" + field_name + "' range needs both min and max");
        out.kind = ScriptPlayValue::Kind::Range;
        out.min = j["min"].get<double>();
        out.max = j["max"].get<double>();
        return out;
    }

    throw std::runtime_error("scriptplay: unsupported field shape for '" + field_name + "'");
}

inline void parse_scriptplay_kind_filter(ScriptPlayFilter& filter,
                                         const nlohmann::json& value) {
    if (value.is_string()) {
        const int kind = detail::parse_kind_name(value.get<std::string>());
        if (kind != 0) filter.kinds = {kind};
        return;
    }
    if (!value.is_array())
        throw std::runtime_error("scriptplay: kind filter must be a string or array");
    filter.kinds.clear();
    for (const auto& item : value) {
        if (item.is_number_integer()) {
            filter.kinds.push_back(item.get<int>());
            continue;
        }
        const int kind = detail::parse_kind_name(item.get<std::string>());
        if (kind == 0) {
            filter.kinds.clear();
            return;
        }
        filter.kinds.push_back(kind);
    }
}

inline std::vector<int> parse_int_list(const nlohmann::json& value,
                                       const std::string& field_name) {
    std::vector<int> out;
    if (value.is_number_integer()) {
        out.push_back(value.get<int>());
        return out;
    }
    if (!value.is_array())
        throw std::runtime_error("scriptplay: field '" + field_name + "' must be an int or array");
    out.reserve(value.size());
    for (const auto& item : value)
        out.push_back(item.get<int>());
    return out;
}

inline ScriptPlayFilter parse_scriptplay_filter(const nlohmann::json& entry) {
    ScriptPlayFilter filter;

    auto apply_filter_object = [&](const nlohmann::json& obj) {
        if (obj.contains("start_note_index")) filter.start_note_index = obj["start_note_index"].get<int>();
        if (obj.contains("end_note_index")) filter.end_note_index = obj["end_note_index"].get<int>();
        if (obj.contains("startNoteIndex")) filter.start_note_index = obj["startNoteIndex"].get<int>();
        if (obj.contains("endNoteIndex")) filter.end_note_index = obj["endNoteIndex"].get<int>();
        if (obj.contains("note_indexes")) filter.note_indexes = parse_int_list(obj["note_indexes"], "note_indexes");
        if (obj.contains("noteIndexes")) filter.note_indexes = parse_int_list(obj["noteIndexes"], "noteIndexes");
        if (obj.contains("note_ids")) filter.note_ids = parse_int_list(obj["note_ids"], "note_ids");
        if (obj.contains("noteIds")) filter.note_ids = parse_int_list(obj["noteIds"], "noteIds");
        if (obj.contains("line_ids")) filter.line_ids = parse_int_list(obj["line_ids"], "line_ids");
        if (obj.contains("lineIds")) filter.line_ids = parse_int_list(obj["lineIds"], "lineIds");
        if (obj.contains("kind")) parse_scriptplay_kind_filter(filter, obj["kind"]);
        if (obj.contains("kinds")) parse_scriptplay_kind_filter(filter, obj["kinds"]);
        if (obj.contains("above")) filter.above = obj["above"].get<bool>();
        if (obj.contains("fake")) filter.fake = obj["fake"].get<bool>();
        if (obj.contains("mh")) filter.mh = obj["mh"].get<bool>();
        if (obj.contains("playable")) filter.playable = obj["playable"].get<bool>();
    };

    if (entry.contains("filter") && entry["filter"].is_object())
        apply_filter_object(entry["filter"]);
    apply_filter_object(entry);
    return filter;
}

inline ScriptPlayEntry parse_scriptplay_entry(const nlohmann::json& entry) {
    ScriptPlayEntry out;
    out.filter = parse_scriptplay_filter(entry);

    const nlohmann::json* judge_obj = &entry;
    if (entry.contains("judge") && entry["judge"].is_object())
        judge_obj = &entry["judge"];
    if (judge_obj->contains("grade"))
        out.grade = detail::normalize_grade((*judge_obj)["grade"].get<std::string>());
    if (judge_obj->contains("dt_ms"))
        out.dt_ms = parse_scriptplay_value((*judge_obj)["dt_ms"], "dt_ms");
    if (judge_obj->contains("dt"))
        out.dt_ms = parse_scriptplay_value((*judge_obj)["dt"], "dt");

    if (entry.contains("hold") && entry["hold"].is_object()) {
        const auto& hold = entry["hold"];
        if (hold.contains("percent")) out.hold_percent = parse_scriptplay_value(hold["percent"], "hold.percent");
        if (hold.contains("ms")) out.hold_ms = parse_scriptplay_value(hold["ms"], "hold.ms");
    }

    if (entry.contains("hold_percent"))
        out.hold_percent = parse_scriptplay_value(entry["hold_percent"], "hold_percent");
    if (entry.contains("holdPercent"))
        out.hold_percent = parse_scriptplay_value(entry["holdPercent"], "holdPercent");
    if (entry.contains("hold_ms"))
        out.hold_ms = parse_scriptplay_value(entry["hold_ms"], "hold_ms");
    if (entry.contains("holdMs"))
        out.hold_ms = parse_scriptplay_value(entry["holdMs"], "holdMs");

    if (out.hold_percent && out.hold_ms)
        throw std::runtime_error("scriptplay: an entry cannot specify both hold_percent and hold_ms");
    return out;
}

inline ScriptPlayScript load_scriptplay_json(const nlohmann::json& j) {
    ScriptPlayScript script;
    script.version = j.value("version", 1);
    if (j.contains("meta") && j["meta"].is_object()) {
        const auto& meta = j["meta"];
        script.name = meta.value("name", std::string{});
        script.index_mode = meta.value("index_mode", std::string("playable"));
        if (meta.contains("require_total_notes") && !meta["require_total_notes"].is_null())
            script.require_total_notes = meta["require_total_notes"].get<int>();
        if (meta.contains("require_playable_notes") && !meta["require_playable_notes"].is_null())
            script.require_playable_notes = meta["require_playable_notes"].get<int>();
    }
    if (j.contains("index_mode") && !j["index_mode"].is_null())
        script.index_mode = j["index_mode"].get<std::string>();
    if (j.contains("entries") && j["entries"].is_array()) {
        script.entries.reserve(j["entries"].size());
        for (const auto& entry : j["entries"])
            script.entries.push_back(parse_scriptplay_entry(entry));
    }
    return script;
}

inline ScriptPlayScript load_scriptplay_text(const std::string& text) {
    return load_scriptplay_json(
        nlohmann::json::parse(detail::strip_jsonc_comments_local(text)));
}

inline ScriptPlayScript load_scriptplay(const std::string& path) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("scriptplay: cannot open '" + path + "'");
    const std::string text((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    return load_scriptplay_text(text);
}

inline bool scriptplay_filter_matches(const ScriptPlayFilter& filter,
                                      const Note& note,
                                      int note_idx,
                                      int index_pos) {
    if (filter.start_note_index && index_pos < *filter.start_note_index) return false;
    if (filter.end_note_index && index_pos > *filter.end_note_index) return false;
    if (!filter.note_indexes.empty() && !detail::contains_int(filter.note_indexes, index_pos)) return false;
    if (!filter.note_ids.empty() && !detail::contains_int(filter.note_ids, note.nid)) return false;
    if (!filter.line_ids.empty() && !detail::contains_int(filter.line_ids, note.line_id)) return false;
    if (!filter.kinds.empty() && !detail::contains_int(filter.kinds, note.kind)) return false;
    if (filter.above && note.above != *filter.above) return false;
    if (filter.fake && note.fake != *filter.fake) return false;
    if (filter.mh && note.mh != *filter.mh) return false;
    if (filter.playable && *filter.playable != !note.fake) return false;
    (void)note_idx;
    return true;
}

inline void validate_scriptplay_requirements(const ScriptPlayScript& script,
                                             const ChartData& chart) {
    if (script.require_total_notes && *script.require_total_notes != static_cast<int>(chart.notes.size())) {
        throw std::runtime_error("scriptplay: require_total_notes mismatch");
    }
    if (script.require_playable_notes && *script.require_playable_notes != chart.playable_count) {
        throw std::runtime_error("scriptplay: require_playable_notes mismatch");
    }
}

inline std::optional<std::string> scriptplay_grade_for_dt(double dt_sec) {
    const double adt = std::abs(dt_sec);
    if (adt <= Judge::PERFECT) return std::string("PERFECT");
    if (adt <= Judge::GOOD) return std::string("GOOD");
    if (adt <= Judge::BAD) return std::string("BAD");
    return std::nullopt;
}

inline ScriptPlayPlan compile_scriptplay(const ScriptPlayScript& script,
                                         const ChartData& chart,
                                         double hold_tail_tol) {
    validate_scriptplay_requirements(script, chart);

    ScriptPlayPlan plan;
    plan.script = script;
    plan.note_plans.resize(chart.notes.size());
    for (size_t i = 0; i < chart.notes.size(); ++i) {
        plan.note_plans[i].scripted = !chart.notes[i].fake;
    }

    std::vector<int> note_index_space;
    note_index_space.reserve(chart.notes.size());
    const bool playable_index = (detail::upper_ascii(script.index_mode) != "ALL");
    for (size_t i = 0; i < chart.notes.size(); ++i) {
        if (playable_index && chart.notes[i].fake) continue;
        note_index_space.push_back(static_cast<int>(i));
    }

    for (const auto& entry : script.entries) {
        std::vector<int> matched;
        matched.reserve(note_index_space.size());
        for (size_t pos = 0; pos < note_index_space.size(); ++pos) {
            const int note_idx = note_index_space[pos];
            const auto& note = chart.notes[note_idx];
            if (!scriptplay_filter_matches(entry.filter, note, note_idx, static_cast<int>(pos)))
                continue;
            matched.push_back(note_idx);
        }

        for (size_t ordinal = 0; ordinal < matched.size(); ++ordinal) {
            auto& np = plan.note_plans[matched[ordinal]];
            np.scripted = true;
            if (entry.grade) np.grade = *entry.grade;
            if (entry.dt_ms) np.dt_ms = entry.dt_ms->sample(ordinal, matched.size());
            if (entry.hold_percent) {
                np.hold_percent = entry.hold_percent->sample(ordinal, matched.size());
                np.hold_ms.reset();
            }
            if (entry.hold_ms) {
                np.hold_ms = entry.hold_ms->sample(ordinal, matched.size());
                np.hold_percent.reset();
            }
        }
    }

    for (size_t i = 0; i < chart.notes.size(); ++i) {
        const auto& note = chart.notes[i];
        if (note.fake) continue;
        const auto& np = plan.note_plans[i];

        if (note.kind != 3) {
            if (np.grade == "MISS") continue;
            const double judge_t = note.t_hit + np.dt_ms / 1000.0;
            auto actual = scriptplay_grade_for_dt(judge_t - note.t_hit);
            if (!actual || *actual != np.grade) {
                throw std::runtime_error("scriptplay: non-hold grade/dt mismatch on note " +
                    std::to_string(note.nid));
            }
            plan.events.push_back({judge_t, static_cast<int>(i),
                                   ScriptPlayPlan::Event::Type::Judge, np.grade});
            continue;
        }

        if (np.grade == "MISS") {
            if (np.hold_percent || np.hold_ms)
                throw std::runtime_error("scriptplay: hold note cannot use grade MISS with hold timing on note " +
                    std::to_string(note.nid));
            continue;
        }

        const double judge_t = note.t_hit + np.dt_ms / 1000.0;
        auto start_grade = scriptplay_grade_for_dt(judge_t - note.t_hit);
        if (!start_grade || *start_grade != np.grade) {
            throw std::runtime_error("scriptplay: hold start grade/dt mismatch on note " +
                std::to_string(note.nid));
        }
        plan.events.push_back({judge_t, static_cast<int>(i),
                               ScriptPlayPlan::Event::Type::Judge, np.grade});

        if (!np.hold_percent && !np.hold_ms) continue;

        const double hold_dur = std::max(0.0, note.t_end - note.t_hit);
        double release_t = note.t_end;
        if (np.hold_percent) {
            const double pct = std::clamp(*np.hold_percent, 0.0, 1.0);
            release_t = note.t_hit + hold_dur * pct;
        } else if (np.hold_ms) {
            release_t = note.t_hit + std::max(0.0, *np.hold_ms) / 1000.0;
        }
        release_t = std::max(judge_t, std::min(release_t, note.t_end));
        if (release_t >= note.t_end - 1e-9) continue;

        const double progress = (hold_dur <= 1e-9)
            ? 1.0
            : std::clamp((release_t - note.t_hit) / hold_dur, 0.0, 1.0);
        if (progress >= hold_tail_tol) {
            PHLOG_TRACE(Engine, "scriptplay hold release still passes note=" << note.nid
                << " progress=" << progress << " tol=" << hold_tail_tol);
        }
        plan.events.push_back({release_t, static_cast<int>(i),
                               ScriptPlayPlan::Event::Type::HoldRelease, "hold_release"});
    }

    std::stable_sort(plan.events.begin(), plan.events.end(),
        [](const ScriptPlayPlan::Event& a, const ScriptPlayPlan::Event& b) {
            if (a.t != b.t) return a.t < b.t;
            return static_cast<int>(a.type) < static_cast<int>(b.type);
        });
    return plan;
}

struct ScriptPlayPlayer {
    ScriptPlayPlan plan;
    int cursor = 0;

    bool enabled() const { return !plan.events.empty() || !plan.note_plans.empty(); }

    void reset() { cursor = 0; }

    void load(const ScriptPlayScript& script,
              const ChartData& chart,
              double hold_tail_tol) {
        plan = compile_scriptplay(script, chart, hold_tail_tol);
        cursor = 0;
    }

    void load(const std::string& path,
              const ChartData& chart,
              double hold_tail_tol) {
        load(load_scriptplay(path), chart, hold_tail_tol);
    }

    void tick(double t,
              const std::vector<Note>& notes,
              std::vector<NoteState>& states,
              Judge& judge,
              const std::function<void(int, float, const std::string&)>& on_judgment = {}) {
        while (cursor < static_cast<int>(plan.events.size()) &&
               plan.events[cursor].t <= t) {
            const auto& ev = plan.events[cursor++];
            if (ev.note_idx < 0 || ev.note_idx >= static_cast<int>(states.size())) continue;
            auto& ns = states[ev.note_idx];
            if (ns.hold_finalized) continue;

            if (ev.type == ScriptPlayPlan::Event::Type::HoldRelease) {
                if (ns.holding) {
                    ns.holding = false;
                    ns.released_early = true;
                    ns.release_t = ev.t;
                    if (on_judgment) on_judgment(ev.note_idx, static_cast<float>(ev.t), "hold_release");
                }
                continue;
            }

            if (notes[ev.note_idx].kind == 3) {
                auto grade = judge.start_hold(ns, ev.t);
                if (grade && on_judgment)
                    on_judgment(ev.note_idx, static_cast<float>(ev.t), "hold_start:" + *grade);
            } else if (!ns.judged) {
                auto grade = judge.try_hit(ns, ev.t);
                if (grade && on_judgment)
                    on_judgment(ev.note_idx, static_cast<float>(ev.t), *grade);
            }
        }
    }
};

} // namespace phigros::engine
