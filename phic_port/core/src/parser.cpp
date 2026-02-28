#include "phic/core/parser.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <sstream>
#include <utility>
#include <vector>

namespace phic {

namespace {

using json = nlohmann::json;

enum class ChartFlavor : uint8_t {
    OfficialLike = 0,
    Rpe = 1,
};

constexpr double kDefaultOfficialBpm = 120.0;
constexpr double kDefaultRpeBpm = 120.0;

double get_number(const json* j, double fallback = 0.0) {
    if (j == nullptr || j->is_null()) {
        return fallback;
    }
    if (j->is_number()) {
        return j->get<double>();
    }
    if (j->is_array() && !j->empty() && (*j)[0].is_number()) {
        return (*j)[0].get<double>();
    }
    if (j->is_object()) {
        auto it = j->find("value");
        if (it != j->end() && it->is_number()) {
            return it->get<double>();
        }
    }
    return fallback;
}

int get_int(const json* j, int fallback = 0) {
    return static_cast<int>(get_number(j, static_cast<double>(fallback)));
}

bool get_bool(const json* j, bool fallback = false) {
    if (j == nullptr || j->is_null()) {
        return fallback;
    }
    if (j->is_boolean()) {
        return j->get<bool>();
    }
    if (j->is_number_integer()) {
        return j->get<int>() != 0;
    }
    if (j->is_string()) {
        std::string s = j->get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (s == "true" || s == "1" || s == "yes" || s == "on") {
            return true;
        }
        if (s == "false" || s == "0" || s == "no" || s == "off") {
            return false;
        }
    }
    return fallback;
}

double clamp01(double v) {
    return std::clamp(v, 0.0, 1.0);
}

double parse_alpha01(const json* j, double fallback = 1.0) {
    if (j == nullptr || j->is_null()) {
        return fallback;
    }
    const double raw = get_number(j, fallback);
    if (!std::isfinite(raw)) {
        return fallback;
    }
    if (raw <= 1.000001) {
        return clamp01(raw);
    }
    return clamp01(raw / 255.0);
}

double get_beat_value(const json* j, double fallback = 0.0) {
    if (j == nullptr || j->is_null()) {
        return fallback;
    }
    if (j->is_number()) {
        return j->get<double>();
    }
    if (j->is_array()) {
        if (j->size() >= 3 && (*j)[0].is_number() && (*j)[1].is_number() && (*j)[2].is_number()) {
            const double bar = (*j)[0].get<double>();
            const double num = (*j)[1].get<double>();
            const double den = (*j)[2].get<double>();
            if (std::abs(den) <= 1e-12) {
                return bar;
            }
            return bar + (num / den);
        }
        if (!j->empty() && (*j)[0].is_number()) {
            return (*j)[0].get<double>();
        }
    }
    if (j->is_object()) {
        auto it_bar = j->find("bar");
        auto it_num = j->find("num");
        auto it_den = j->find("den");
        if (it_bar != j->end() && it_num != j->end() && it_den != j->end() &&
            it_bar->is_number() && it_num->is_number() && it_den->is_number()) {
            const double bar = it_bar->get<double>();
            const double num = it_num->get<double>();
            const double den = it_den->get<double>();
            if (std::abs(den) <= 1e-12) {
                return bar;
            }
            return bar + (num / den);
        }
        auto it = j->find("value");
        if (it != j->end() && it->is_number()) {
            return it->get<double>();
        }
    }
    return fallback;
}

std::string get_string(const json* j, const std::string& fallback = "") {
    if (j != nullptr && j->is_string()) {
        return j->get<std::string>();
    }
    return fallback;
}

const json* find_ptr(const json& obj, const char* key) {
    if (!obj.is_object()) {
        return nullptr;
    }
    auto it = obj.find(key);
    if (it == obj.end()) {
        return nullptr;
    }
    return &(*it);
}

int lane_from_raw(const json* lane_j, const json* pos_x_j, int lane_count = 8) {
    const int lane_default = 0;
    if (lane_j != nullptr && lane_j->is_number_integer()) {
        return std::clamp(lane_j->get<int>(), 0, lane_count - 1);
    }

    const double raw = get_number(pos_x_j, static_cast<double>(lane_default));
    if (std::abs(raw) <= static_cast<double>(lane_count - 1)) {
        return std::clamp(static_cast<int>(std::lround(raw)), 0, lane_count - 1);
    }

    // Common chart coordinate range is roughly [-675, 675].
    const double normalized = (raw + 675.0) / 1350.0;
    const int lane = static_cast<int>(std::lround(normalized * static_cast<double>(lane_count - 1)));
    return std::clamp(lane, 0, lane_count - 1);
}

NoteKind parse_kind(const json* j, ChartFlavor flavor) {
    if (j == nullptr || j->is_null()) {
        return NoteKind::Tap;
    }
    if (j->is_number_integer()) {
        const int n = j->get<int>();
        if (flavor == ChartFlavor::Rpe) {
            // RPE: 1 Tap, 2 Hold, 3 Flick, 4 Drag.
            if (n == 2) return NoteKind::Hold;
            if (n == 3) return NoteKind::Flick;
            if (n == 4) return NoteKind::Drag;
            return NoteKind::Tap;
        }
        // Official/internal: 1 Tap, 2 Drag, 3 Hold, 4 Flick.
        if (n == 2) return NoteKind::Drag;
        if (n == 3) return NoteKind::Hold;
        if (n == 4) return NoteKind::Flick;
        return NoteKind::Tap;
    }
    if (j->is_string()) {
        std::string s = j->get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (s.find("drag") != std::string::npos) return NoteKind::Drag;
        if (s.find("hold") != std::string::npos) return NoteKind::Hold;
        if (s.find("flick") != std::string::npos) return NoteKind::Flick;
    }
    return NoteKind::Tap;
}

double official_unit_sec(double bpm) {
    return 1.875 / std::max(1e-9, bpm);
}

double official_u_to_sec(double unit, double bpm) {
    return unit * official_unit_sec(bpm);
}

struct BpmSeg {
    double beat0 = 0.0;
    double bpm = kDefaultRpeBpm;
    double sec_prefix = 0.0;
};

struct BpmMap {
    std::vector<BpmSeg> segs{};

    double beat_to_sec(double beat, double bpmfactor) const {
        if (segs.empty()) {
            return 0.0;
        }
        std::size_t lo = 0;
        std::size_t hi = segs.size();
        while (lo + 1 < hi) {
            const std::size_t mid = (lo + hi) / 2;
            if (segs[mid].beat0 <= beat) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        const auto& s = segs[lo];
        const double bpm = std::max(1e-9, s.bpm);
        const double factor = std::max(1e-9, bpmfactor);
        return (s.sec_prefix + (beat - s.beat0) * 60.0 / bpm) * factor;
    }
};

BpmMap build_bpm_map(const json* bpm_list) {
    std::vector<std::pair<double, double>> items;
    if (bpm_list != nullptr && bpm_list->is_array()) {
        for (const auto& e : *bpm_list) {
            if (!e.is_object()) {
                continue;
            }
            const double beat = get_beat_value(find_ptr(e, "startTime"), 0.0);
            const double bpm = std::max(1e-9, get_number(find_ptr(e, "bpm"), kDefaultRpeBpm));
            items.emplace_back(beat, bpm);
        }
    }
    if (items.empty()) {
        items.emplace_back(0.0, kDefaultRpeBpm);
    }
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    BpmMap out;
    out.segs.reserve(items.size());
    double sec_prefix = 0.0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const double beat0 = items[i].first;
        const double bpm = items[i].second;
        out.segs.push_back(BpmSeg{beat0, bpm, sec_prefix});
        if (i + 1 < items.size()) {
            const double beat1 = items[i + 1].first;
            sec_prefix += (beat1 - beat0) * 60.0 / std::max(1e-9, bpm);
        }
    }

    return out;
}

void collect_notes_official_from_array(
    const json* arr,
    int line_id,
    int& next_id,
    double bpm,
    bool default_above,
    std::vector<RuntimeNote>& out
) {
    if (arr == nullptr || !arr->is_array()) {
        return;
    }
    for (const auto& item : *arr) {
        if (!item.is_object()) {
            continue;
        }

        RuntimeNote note;
        note.id = next_id++;
        note.line_id = line_id;
        const double t_hit_u = get_number(find_ptr(item, "time"), get_number(find_ptr(item, "startTime"), 0.0));
        const double hold_u = get_number(find_ptr(item, "holdTime"), 0.0);
        const double end_u = get_number(find_ptr(item, "endTime"), t_hit_u);
        note.t_hit = official_u_to_sec(t_hit_u, bpm);
        if (hold_u > 1e-9) {
            note.hold_end = note.t_hit + official_u_to_sec(hold_u, bpm);
        } else {
            note.hold_end = official_u_to_sec(end_u, bpm);
            if (note.hold_end < note.t_hit) {
                note.hold_end = note.t_hit;
            }
        }
        note.lane = lane_from_raw(find_ptr(item, "lane"), find_ptr(item, "positionX"));
        note.above = default_above;
        note.fake = get_bool(find_ptr(item, "fake"), false) || get_int(find_ptr(item, "isFake"), 0) == 1;
        note.speed_mul = get_number(find_ptr(item, "speed"), 1.0);
        note.alpha01 = parse_alpha01(find_ptr(item, "alpha"), 1.0);
        note.kind = parse_kind(find_ptr(item, "type"), ChartFlavor::OfficialLike);
        if (note.hold_end > note.t_hit + 1e-9) {
            note.kind = NoteKind::Hold;
        }
        out.push_back(note);
    }
}

void collect_notes_rpe_from_array(
    const json* arr,
    int line_id,
    int& next_id,
    const BpmMap& bpm_map,
    double bpmfactor,
    bool default_above,
    std::vector<RuntimeNote>& out
) {
    if (arr == nullptr || !arr->is_array()) {
        return;
    }
    for (const auto& item : *arr) {
        if (!item.is_object()) {
            continue;
        }

        RuntimeNote note;
        note.id = next_id++;
        note.line_id = line_id;
        const double beat0 = get_beat_value(find_ptr(item, "startTime"), get_beat_value(find_ptr(item, "time"), 0.0));
        const double beat1 = get_beat_value(find_ptr(item, "endTime"), beat0);
        note.t_hit = bpm_map.beat_to_sec(beat0, bpmfactor);
        note.hold_end = bpm_map.beat_to_sec(beat1, bpmfactor);
        if (note.hold_end < note.t_hit) {
            note.hold_end = note.t_hit;
        }
        note.lane = lane_from_raw(find_ptr(item, "lane"), find_ptr(item, "positionX"));
        const int above_raw = get_int(find_ptr(item, "above"), default_above ? 0 : 1);
        note.above = (above_raw != 1);
        note.fake = get_bool(find_ptr(item, "fake"), false) || get_int(find_ptr(item, "isFake"), 0) == 1;
        note.speed_mul = get_number(find_ptr(item, "speed"), 1.0);
        note.alpha01 = parse_alpha01(find_ptr(item, "alpha"), 1.0);
        note.kind = parse_kind(find_ptr(item, "type"), ChartFlavor::Rpe);
        if (note.hold_end > note.t_hit + 1e-9) {
            note.kind = NoteKind::Hold;
        }
        out.push_back(note);
    }
}

ParseChartResult parse_official_chart(const json& root) {
    ParseChartResult out;
    out.chart.title = get_string(find_ptr(root, "name"), get_string(find_ptr(root, "title"), "Untitled"));

    int next_note_id = 1;
    const json* lines = find_ptr(root, "judgeLineList");
    if (lines != nullptr && lines->is_array()) {
        int line_id = 0;
        for (const auto& line : *lines) {
            out.chart.lines.push_back(RuntimeLine{line_id});
            const double bpm = std::max(1e-9, get_number(find_ptr(line, "bpm"), kDefaultOfficialBpm));
            // Keep Python-side "official above/below reversed for Y-axis flip" semantics.
            collect_notes_official_from_array(find_ptr(line, "notesAbove"), line_id, next_note_id, bpm, false, out.chart.notes);
            collect_notes_official_from_array(find_ptr(line, "notesBelow"), line_id, next_note_id, bpm, true, out.chart.notes);
            collect_notes_official_from_array(find_ptr(line, "notes"), line_id, next_note_id, bpm, true, out.chart.notes);
            ++line_id;
        }
    }

    if (out.chart.notes.empty()) {
        out.chart.lines.push_back(RuntimeLine{0});
        collect_notes_official_from_array(find_ptr(root, "notes"), 0, next_note_id, kDefaultOfficialBpm, true, out.chart.notes);
    }

    if (out.chart.notes.empty()) {
        out.error = "no notes found in chart payload";
        return out;
    }

    std::sort(out.chart.notes.begin(), out.chart.notes.end(), [](const RuntimeNote& a, const RuntimeNote& b) {
        return a.t_hit < b.t_hit;
    });

    out.ok = true;
    return out;
}

ParseChartResult parse_rpe_chart(const json& root) {
    ParseChartResult out;
    out.chart.title = get_string(find_ptr(root, "name"), get_string(find_ptr(root, "title"), "Untitled"));

    const BpmMap bpm_map = build_bpm_map(find_ptr(root, "BPMList"));
    int next_note_id = 1;

    const json* lines = find_ptr(root, "judgeLineList");
    if (lines != nullptr && lines->is_array()) {
        int line_id = 0;
        for (const auto& line : *lines) {
            out.chart.lines.push_back(RuntimeLine{line_id});
            const double bpmfactor = std::max(1e-9, get_number(find_ptr(line, "bpmfactor"), 1.0));
            collect_notes_rpe_from_array(find_ptr(line, "notes"), line_id, next_note_id, bpm_map, bpmfactor, false, out.chart.notes);
            collect_notes_rpe_from_array(find_ptr(line, "notesAbove"), line_id, next_note_id, bpm_map, bpmfactor, false, out.chart.notes);
            collect_notes_rpe_from_array(find_ptr(line, "notesBelow"), line_id, next_note_id, bpm_map, bpmfactor, true, out.chart.notes);
            ++line_id;
        }
    }

    if (out.chart.notes.empty()) {
        out.chart.lines.push_back(RuntimeLine{0});
        collect_notes_rpe_from_array(find_ptr(root, "notes"), 0, next_note_id, bpm_map, 1.0, false, out.chart.notes);
    }

    if (out.chart.notes.empty()) {
        out.error = "no notes found in chart payload";
        return out;
    }

    std::sort(out.chart.notes.begin(), out.chart.notes.end(), [](const RuntimeNote& a, const RuntimeNote& b) {
        return a.t_hit < b.t_hit;
    });

    out.ok = true;
    return out;
}

ParseChartResult parse_pec_text(const std::string& payload) {
    ParseChartResult out;
    out.chart.title = "PEC Chart";
    out.chart.lines.push_back(RuntimeLine{0});

    std::istringstream ss(payload);
    std::string line;
    int next_note_id = 1;
    while (std::getline(ss, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream ls(line);
        std::string op;
        ls >> op;
        if (op.empty() || op[0] != 'n') {
            continue;
        }

        double time = 0.0;
        int lane = 0;
        ls >> time >> lane;

        RuntimeNote note;
        note.id = next_note_id++;
        note.line_id = 0;
        note.lane = lane;
        note.above = true;
        note.fake = false;
        note.t_hit = time;
        note.hold_end = time;
        note.speed_mul = 1.0;
        note.alpha01 = 1.0;
        note.kind = NoteKind::Tap;
        out.chart.notes.push_back(note);
    }

    if (out.chart.notes.empty()) {
        out.error = "PEC parser could not extract notes";
        return out;
    }

    std::sort(out.chart.notes.begin(), out.chart.notes.end(), [](const RuntimeNote& a, const RuntimeNote& b) {
        return a.t_hit < b.t_hit;
    });

    out.ok = true;
    return out;
}

}  // namespace

ParseChartResult parse_chart_bytes(const std::string& payload, const std::string& format_hint) {
    std::string fmt = format_hint;
    std::transform(fmt.begin(), fmt.end(), fmt.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (fmt == "pec") {
        return parse_pec_text(payload);
    }

    json root;
    try {
        root = json::parse(payload);
    } catch (const std::exception& e) {
        ParseChartResult out;
        out.error = std::string("JSON parse failed: ") + e.what();
        return out;
    }
    if (!root.is_object()) {
        ParseChartResult out;
        out.error = "chart root is not an object";
        return out;
    }

    if (fmt == "rpe") {
        return parse_rpe_chart(root);
    }
    return parse_official_chart(root);
}

}  // namespace phic
