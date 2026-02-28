#include "phic/core/parser.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <set>
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
// Reference screen dimensions for scroll_px (matches Python at H=900, W=1350)
constexpr double kRefH = 900.0;
constexpr double kRefW = 1350.0;

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
        auto it = std::upper_bound(segs.begin(), segs.end(), beat,
            [](double b, const BpmSeg& s) { return b < s.beat0; });
        if (it != segs.begin()) {
            --it;
        }
        const auto& s = *it;
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

// ================================================================
// Line-event track builders
// ================================================================

// Sort segs and prepend a constant segment from t=0 when the first seg starts late.
static void finalise_eased_track(EasedTrack& trk) {
    std::sort(trk.segs.begin(), trk.segs.end(),
        [](const EasedSeg& a, const EasedSeg& b) { return a.t0 < b.t0; });
    if (!trk.segs.empty() && trk.segs.front().t0 > 1e-9) {
        EasedSeg fill{};
        fill.t0 = 0.0; fill.t1 = trk.segs.front().t0;
        fill.v0 = trk.segs.front().v0; fill.v1 = trk.segs.front().v0;
        trk.segs.insert(trk.segs.begin(), fill);
    }
}

// Sort segments and recompute prefix integrals.
static void finalise_integral_track(IntegralTrack& trk) {
    std::sort(trk.segs.begin(), trk.segs.end(),
        [](const Seg1D& a, const Seg1D& b) { return a.t0 < b.t0; });
    double prefix = 0.0;
    for (auto& s : trk.segs) {
        s.prefix = prefix;
        prefix += 0.5 * (s.v0 + s.v1) * std::max(0.0, s.t1 - s.t0);
    }
}

// ────────────────── Official format helpers ──────────────────

// Convert official judgeLineMoveEvents start/end values to RPE-internal X/Y units.
// fmt 3: start=x(0..1), start2=y(0..1, bottom-origin)
// fmt 1: start=packed 1000*xu+yu in [0,880]×[0,520] space
static std::pair<double,double> official_xy_to_rpe(double packed_or_x, double y_f3, int fmt) {
    if (fmt == 3) {
        // RPE-X = x_norm*1350-675; RPE-Y = y_norm*900-450
        return { packed_or_x * 1350.0 - 675.0, y_f3 * 900.0 - 450.0 };
    }
    // fmt 1: packed
    const double xu = std::floor(packed_or_x / 1000.0);
    const double yu = packed_or_x - xu * 1000.0;
    return { (xu / 880.0) * 1350.0 - 675.0, (yu / 520.0) * 900.0 - 450.0 };
}

static void parse_official_move_events(const json* events, double bpm, int fmt,
                                        EasedTrack& out_x, EasedTrack& out_y) {
    if (!events || !events->is_array()) return;
    for (const auto& e : *events) {
        if (!e.is_object()) continue;
        const double t0 = official_u_to_sec(get_number(find_ptr(e, "startTime")), bpm);
        const double t1 = official_u_to_sec(get_number(find_ptr(e, "endTime")),   bpm);
        const auto [x0, y0] = official_xy_to_rpe(
            get_number(find_ptr(e, "start")), get_number(find_ptr(e, "start2")), fmt);
        const auto [x1, y1] = official_xy_to_rpe(
            get_number(find_ptr(e, "end")),   get_number(find_ptr(e, "end2")),   fmt);
        out_x.segs.push_back({t0, t1, x0, x1});
        out_y.segs.push_back({t0, t1, y0, y1});
    }
    finalise_eased_track(out_x);
    finalise_eased_track(out_y);
}

static void parse_official_rotate_events(const json* events, double bpm, EasedTrack& out) {
    if (!events || !events->is_array()) return;
    constexpr double kD2R = 3.14159265358979323846 / 180.0;
    for (const auto& e : *events) {
        if (!e.is_object()) continue;
        const double t0 = official_u_to_sec(get_number(find_ptr(e, "startTime")), bpm);
        const double t1 = official_u_to_sec(get_number(find_ptr(e, "endTime")),   bpm);
        out.segs.push_back({t0, t1,
            -get_number(find_ptr(e, "start")) * kD2R,
            -get_number(find_ptr(e, "end"))   * kD2R});
    }
    finalise_eased_track(out);
}

static void parse_official_alpha_events(const json* events, double bpm, EasedTrack& out) {
    if (!events || !events->is_array()) return;
    for (const auto& e : *events) {
        if (!e.is_object()) continue;
        const double t0 = official_u_to_sec(get_number(find_ptr(e, "startTime")), bpm);
        const double t1 = official_u_to_sec(get_number(find_ptr(e, "endTime")),   bpm);
        out.segs.push_back({t0, t1,
            get_number(find_ptr(e, "start"), 1.0),
            get_number(find_ptr(e, "end"),   1.0)});
    }
    finalise_eased_track(out);
}

// Official scroll: speed events are constant-velocity segments.
// Uh_px = 0.6 * kRefH = 540 — matches Python's Uh = 0.6*H at H=900.
static IntegralTrack build_official_scroll(const json* speed_events, double bpm) {
    constexpr double Uh = 0.6 * kRefH;
    IntegralTrack trk;
    if (!speed_events || !speed_events->is_array()) return trk;
    for (const auto& e : *speed_events) {
        if (!e.is_object()) continue;
        const double t0 = official_u_to_sec(get_number(find_ptr(e, "startTime")), bpm);
        const double t1 = official_u_to_sec(get_number(find_ptr(e, "endTime")),   bpm);
        const double v  = get_number(find_ptr(e, "value"), 1.0) * Uh;
        trk.segs.push_back({t0, t1, v, v, 0.0});
    }
    // Extend from t=0 with first speed value (matches Python).
    if (!trk.segs.empty()) {
        std::sort(trk.segs.begin(), trk.segs.end(),
            [](const Seg1D& a, const Seg1D& b){ return a.t0 < b.t0; });
        if (trk.segs.front().t0 > 1e-9) {
            const double v0 = trk.segs.front().v0;
            trk.segs.insert(trk.segs.begin(), {0.0, trk.segs.front().t0, v0, v0, 0.0});
        }
    }
    finalise_integral_track(trk);
    return trk;
}

// Build official LineAnim and attach to a RuntimeLine.
static void build_official_line_anim(const json& jl, double bpm, int fmt, RuntimeLine& out_line) {
    LineAnim& a = out_line.anim;
    // pos_x, pos_y — single layer each
    {
        EasedTrack tx, ty;
        parse_official_move_events(find_ptr(jl, "judgeLineMoveEvents"), bpm, fmt, tx, ty);
        if (!tx.segs.empty()) a.pos_x.layers.push_back(std::move(tx));
        if (!ty.segs.empty()) a.pos_y.layers.push_back(std::move(ty));
    }
    // rot
    {
        EasedTrack tr;
        parse_official_rotate_events(find_ptr(jl, "judgeLineRotateEvents"), bpm, tr);
        if (!tr.segs.empty()) a.rot_rad.layers.push_back(std::move(tr));
    }
    // alpha — official stores 0..1, default 1.0
    a.alpha_raw.default_val = 1.0;
    {
        EasedTrack ta;
        parse_official_alpha_events(find_ptr(jl, "judgeLineDisappearEvents"), bpm, ta);
        if (!ta.segs.empty()) a.alpha_raw.layers.push_back(std::move(ta));
    }
    // scroll
    a.scroll_px = build_official_scroll(find_ptr(jl, "speedEvents"), bpm);

    // Tally total segments for bench reporting
    for (const auto& l : a.pos_x.layers)   a.total_event_segs += static_cast<int>(l.segs.size());
    for (const auto& l : a.pos_y.layers)   a.total_event_segs += static_cast<int>(l.segs.size());
    for (const auto& l : a.rot_rad.layers) a.total_event_segs += static_cast<int>(l.segs.size());
    for (const auto& l : a.alpha_raw.layers) a.total_event_segs += static_cast<int>(l.segs.size());
    a.total_event_segs += static_cast<int>(a.scroll_px.segs.size());
}

// ────────────────── RPE format helpers ──────────────────

// Build a single-layer EasedTrack from an RPE eventLayer sub-array (moveXEvents, etc.).
static EasedTrack build_rpe_eased_layer(const json* events,
                                         const BpmMap& bpm_map, double bpmfactor,
                                         double default_val) {
    EasedTrack trk;
    trk.default_val = default_val;
    if (!events || !events->is_array()) return trk;
    for (const auto& e : *events) {
        if (!e.is_object()) continue;
        const double t0 = bpm_map.beat_to_sec(get_beat_value(find_ptr(e, "startTime")), bpmfactor);
        const double t1 = bpm_map.beat_to_sec(get_beat_value(find_ptr(e, "endTime")),   bpmfactor);
        const double v0 = get_number(find_ptr(e, "start"), default_val);
        const double v1 = get_number(find_ptr(e, "end"),   v0);
        const double L  = get_number(find_ptr(e, "easingLeft"),  0.0);
        const double R  = get_number(find_ptr(e, "easingRight"), 1.0);
        const int bez   = get_int(find_ptr(e, "bezier"), 0);

        EasedSeg seg{t0, t1, v0, v1, L, R, 0};
        if (bez == 1) {
            const json* bp = find_ptr(e, "bezierPoints");
            if (bp && bp->is_array() && bp->size() >= 4) {
                seg.easing_type = -1;
                seg.bx1 = (*bp)[0].get<double>(); seg.by1 = (*bp)[1].get<double>();
                seg.bx2 = (*bp)[2].get<double>(); seg.by2 = (*bp)[3].get<double>();
            }
        } else {
            seg.easing_type = get_int(find_ptr(e, "easingType"), 0);
        }
        trk.segs.push_back(seg);
    }
    finalise_eased_track(trk);
    return trk;
}

// Collapse all speed layers into one combined IntegralTrack.
// Matches Python's build_rpe_scroll_px: cuts at all boundaries, samples sum at midpoints.
static IntegralTrack build_rpe_scroll(
        const std::vector<const json*>& speed_layers,
        const BpmMap& bpm_map, double bpmfactor) {
    constexpr double px_per_unit_per_sec = 120.0 * (kRefH / 900.0);  // =120 at H=900
    IntegralTrack trk;

    // Collect all events and cut times
    std::vector<std::pair<double,double>> all_evs;  // (t0, t1)
    std::set<double> cuts = {0.0};
    for (const json* layer : speed_layers) {
        if (!layer || !layer->is_array()) continue;
        for (const auto& e : *layer) {
            if (!e.is_object()) continue;
            const double t0 = bpm_map.beat_to_sec(get_beat_value(find_ptr(e, "startTime")), bpmfactor);
            const double t1 = bpm_map.beat_to_sec(get_beat_value(find_ptr(e, "endTime")),   bpmfactor);
            cuts.insert(t0); cuts.insert(t1);
        }
    }
    if (cuts.size() <= 1) return trk;

    // Extend with a far-future cut so the last event is covered
    const double t_max = *cuts.rbegin() + 1e6;
    cuts.insert(t_max);
    const std::vector<double> cut_vec(cuts.begin(), cuts.end());

    // Helper: sample speed sum at a midpoint across all layers
    auto sample_sum = [&](double t_mid) -> double {
        double total = 0.0;
        for (const json* layer : speed_layers) {
            if (!layer || !layer->is_array()) continue;
            // Find the event covering t_mid or the last event before it
            double val = 0.0;
            bool covered = false;
            const json* last_before = nullptr;
            for (const auto& e : *layer) {
                if (!e.is_object()) continue;
                const double et0 = bpm_map.beat_to_sec(get_beat_value(find_ptr(e, "startTime")), bpmfactor);
                const double et1 = bpm_map.beat_to_sec(get_beat_value(find_ptr(e, "endTime")),   bpmfactor);
                if (t_mid < et0) break;
                last_before = &e;
                if (t_mid < et1) {
                    const double s0 = get_number(find_ptr(e, "start"), 0.0);
                    const double s1 = get_number(find_ptr(e, "end"),   s0);
                    const double u  = (t_mid - et0) / std::max(1e-9, et1 - et0);
                    val   += s0 + std::clamp(u, 0.0, 1.0) * (s1 - s0);
                    covered = true;
                }
            }
            if (!covered && last_before) {
                val += get_number(find_ptr(*last_before, "end"),
                                  get_number(find_ptr(*last_before, "start"), 0.0));
            }
            total += val;
        }
        return total * px_per_unit_per_sec;
    };

    for (std::size_t i = 0; i + 1 < cut_vec.size(); ++i) {
        const double t0 = cut_vec[i], t1 = cut_vec[i + 1];
        if (t1 <= t0 + 1e-12) continue;
        const double v = sample_sum((t0 + t1) * 0.5);
        trk.segs.push_back({t0, t1, v, v, 0.0});
    }
    finalise_integral_track(trk);
    return trk;
}

// Build RPE LineAnim from one judgeLineList entry.
static void build_rpe_line_anim(const json& jl,
                                  const BpmMap& bpm_map, double bpmfactor,
                                  RuntimeLine& out_line) {
    LineAnim& a = out_line.anim;
    a.alpha_raw.default_val = 255.0;  // RPE uses 0..255

    const json* layers_arr = find_ptr(jl, "eventLayers");
    std::vector<const json*> speed_layers;

    if (layers_arr && layers_arr->is_array()) {
        for (const auto& layer : *layers_arr) {
            if (layer.is_null()) continue;
            // moveXEvents → pos_x
            {
                EasedTrack t = build_rpe_eased_layer(find_ptr(layer,"moveXEvents"), bpm_map, bpmfactor, 0.0);
                if (!t.segs.empty()) a.pos_x.layers.push_back(std::move(t));
            }
            // moveYEvents → pos_y
            {
                EasedTrack t = build_rpe_eased_layer(find_ptr(layer,"moveYEvents"), bpm_map, bpmfactor, 0.0);
                if (!t.segs.empty()) a.pos_y.layers.push_back(std::move(t));
            }
            // rotateEvents → rot_rad (degrees in JSON, convert to radians at parse time)
            {
                constexpr double kD2R = 3.14159265358979323846 / 180.0;
                EasedTrack t = build_rpe_eased_layer(find_ptr(layer,"rotateEvents"), bpm_map, bpmfactor, 0.0);
                for (auto& s : t.segs) { s.v0 *= kD2R; s.v1 *= kD2R; }
                if (!t.segs.empty()) a.rot_rad.layers.push_back(std::move(t));
            }
            // alphaEvents → alpha_raw (0..255 raw)
            {
                EasedTrack t = build_rpe_eased_layer(find_ptr(layer,"alphaEvents"), bpm_map, bpmfactor, 255.0);
                if (!t.segs.empty()) a.alpha_raw.layers.push_back(std::move(t));
            }
            speed_layers.push_back(find_ptr(layer, "speedEvents"));
        }
    }
    // Fallback: chart-level speedEvents (older RPE exporters)
    if (speed_layers.empty() || std::all_of(speed_layers.begin(), speed_layers.end(),
            [](const json* p){ return p == nullptr || !p->is_array() || p->empty(); })) {
        speed_layers.clear();
        speed_layers.push_back(find_ptr(jl, "speedEvents"));
    }
    a.scroll_px = build_rpe_scroll(speed_layers, bpm_map, bpmfactor);

    // extended block: colorEvents, scaleXEvents, scaleYEvents — stored in extra SumTracks
    // (not exposed in LineAnim yet; just count segments for stats)
    const json* ext = find_ptr(jl, "extended");
    if (ext && ext->is_object()) {
        for (const char* key : {"colorEvents","scaleXEvents","scaleYEvents","textEvents","gifEvents"}) {
            const json* ev = find_ptr(*ext, key);
            if (ev && ev->is_array()) a.total_event_segs += static_cast<int>(ev->size());
        }
    }

    // father / rotate_with_father
    out_line.father             = get_int(find_ptr(jl, "father"), -1);
    out_line.rotate_with_father = get_bool(find_ptr(jl, "rotateWithFather"), true);

    // Tally segments
    for (const auto& l : a.pos_x.layers)    a.total_event_segs += static_cast<int>(l.segs.size());
    for (const auto& l : a.pos_y.layers)    a.total_event_segs += static_cast<int>(l.segs.size());
    for (const auto& l : a.rot_rad.layers)  a.total_event_segs += static_cast<int>(l.segs.size());
    for (const auto& l : a.alpha_raw.layers)a.total_event_segs += static_cast<int>(l.segs.size());
    a.total_event_segs += static_cast<int>(a.scroll_px.segs.size());
}

// ────────────────── PEC format helpers ──────────────────

// PEC x units (−1024..1024 centre-origin) → RPE-X internal units
static double pec_x_to_rpe(double x) noexcept {
    // Python: (fx + 1024) * W/2048; normalised: (x+1024)/2048; RPE: norm*1350-675
    return ((x + 1024.0) / 2048.0) * 1350.0 - 675.0;
}
// PEC y units (−700..700 centre-origin) → RPE-Y internal units
// Python: H*0.5 - fy*H/1400; norm_y = 0.5 - y/1400; RPE-Y = (0.5-norm_y)*900 = y*900/1400
static double pec_y_to_rpe(double y) noexcept { return y * (kRefH / 1400.0); }

struct PecBpmMap {
    std::vector<std::pair<double,double>> items; // (beat, bpm) sorted
    double beat_to_sec(double beat) const {
        if (items.empty()) return beat * 60.0 / 120.0;
        double prefix = 0.0;
        std::size_t lo = 0;
        while (lo + 1 < items.size() && items[lo + 1].first <= beat) ++lo;
        // sum prefix up to segment lo
        double b_prev = 0.0, s_prev = 0.0;
        for (std::size_t i = 0; i < lo; ++i) {
            const double db = items[i + 1].first - items[i].first;
            s_prev += db * 60.0 / std::max(1e-9, items[i].second);
        }
        return s_prev + (beat - items[lo].first) * 60.0 / std::max(1e-9, items[lo].second);
    }
};

static void build_pec_line_anims(const std::string& payload,
                                   std::vector<RuntimeLine>& lines_out) {
    // Parse BPM map and line event commands from raw PEC text
    constexpr double kD2R = 3.14159265358979323846 / 180.0;
    constexpr double px_per_unit_per_sec = 120.0;  // at reference H=900

    std::vector<std::string> raw;
    {
        std::istringstream ss(payload);
        std::string ln;
        while (std::getline(ss, ln)) {
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
            if (!ln.empty() && ln.substr(0,2) != "//") raw.push_back(ln);
        }
    }
    if (raw.empty()) return;

    PecBpmMap bpm_map;
    for (const auto& ln : raw) {
        if (ln.size() > 3 && ln.substr(0,3) == "bp ") {
            std::istringstream ls(ln.substr(3));
            double beat, bpm; ls >> beat >> bpm;
            if (ls) bpm_map.items.emplace_back(beat, bpm);
        }
    }
    if (bpm_map.items.empty()) bpm_map.items.emplace_back(0.0, 120.0);
    std::sort(bpm_map.items.begin(), bpm_map.items.end());

    // Determine max line id from event and note commands
    int max_lid = static_cast<int>(lines_out.size()) - 1;

    // Collect all event lines by line id
    // commands: cp (instant pos), cd (instant rot), ca (instant alpha),
    //           cm (move+easing), cr (rot+easing), cf (fade+easing), cv (speed)
    struct PecCmd { double t0; std::string op; std::vector<std::string> parts; };
    std::vector<std::vector<PecCmd>> cmds_by_line(std::max(0, max_lid + 1));

    auto split_parts = [](const std::string& s) {
        std::vector<std::string> out;
        std::istringstream ss(s);
        std::string w;
        while (ss >> w) out.push_back(w);
        return out;
    };

    for (const auto& ln : raw) {
        auto parts = split_parts(ln);
        if (parts.size() < 3) continue;
        const std::string& op = parts[0];
        if (op != "cp" && op != "cd" && op != "ca" && op != "cm" && op != "cr" && op != "cf" && op != "cv") continue;
        int lid = std::stoi(parts[1]);
        if (lid < 0 || lid > max_lid) continue;
        double beat = std::stod(parts[2]);
        cmds_by_line[lid].push_back({bpm_map.beat_to_sec(beat), op,
            std::vector<std::string>(parts.begin() + 3, parts.end())});
    }

    for (int lid = 0; lid <= max_lid && lid < static_cast<int>(lines_out.size()); ++lid) {
        auto& cmds = cmds_by_line[lid];
        std::sort(cmds.begin(), cmds.end(), [](const PecCmd& a, const PecCmd& b){ return a.t0 < b.t0; });

        double cur_x = 0.0, cur_y = 0.0, cur_rot_deg = 0.0, cur_alpha = 255.0, cur_speed = 1.0;
        double t_cur = 0.0;

        EasedTrack tx, ty, tr, ta;
        IntegralTrack scroll;

        auto emit_const = [&](double t0, double t1) {
            if (t1 <= t0 + 1e-9) return;
            tx.segs.push_back({t0, t1, cur_x,       cur_x,       0.0, 1.0, 0});
            ty.segs.push_back({t0, t1, cur_y,       cur_y,       0.0, 1.0, 0});
            tr.segs.push_back({t0, t1, cur_rot_deg * kD2R, cur_rot_deg * kD2R, 0.0, 1.0, 0});
            ta.segs.push_back({t0, t1, cur_alpha,   cur_alpha,   0.0, 1.0, 0});
        };

        for (const auto& cmd : cmds) {
            emit_const(t_cur, cmd.t0);
            t_cur = cmd.t0;

            if (cmd.op == "cp" && cmd.parts.size() >= 2) {
                cur_x = pec_x_to_rpe(std::stod(cmd.parts[0]));
                cur_y = pec_y_to_rpe(std::stod(cmd.parts[1]));
            } else if (cmd.op == "cd" && !cmd.parts.empty()) {
                cur_rot_deg = std::stod(cmd.parts[0]);
            } else if (cmd.op == "ca" && !cmd.parts.empty()) {
                cur_alpha = std::clamp(std::stod(cmd.parts[0]), 0.0, 255.0);
            } else if (cmd.op == "cv" && !cmd.parts.empty()) {
                cur_speed = std::stod(cmd.parts[0]);
            } else if (cmd.op == "cm" && cmd.parts.size() >= 4) {
                const double t1 = bpm_map.beat_to_sec(std::stod(cmd.parts[0]));
                const double x1 = pec_x_to_rpe(std::stod(cmd.parts[1]));
                const double y1 = pec_y_to_rpe(std::stod(cmd.parts[2]));
                const int    et = static_cast<int>(std::stod(cmd.parts[3]));
                if (t1 > t_cur + 1e-9) {
                    tx.segs.push_back({t_cur, t1, cur_x, x1, 0.0, 1.0, et});
                    ty.segs.push_back({t_cur, t1, cur_y, y1, 0.0, 1.0, et});
                    tr.segs.push_back({t_cur, t1, cur_rot_deg*kD2R, cur_rot_deg*kD2R, 0.0, 1.0, 0});
                    ta.segs.push_back({t_cur, t1, cur_alpha, cur_alpha, 0.0, 1.0, 0});
                    cur_x = x1; cur_y = y1; t_cur = t1;
                }
            } else if (cmd.op == "cr" && cmd.parts.size() >= 3) {
                const double t1  = bpm_map.beat_to_sec(std::stod(cmd.parts[0]));
                const double r1  = std::stod(cmd.parts[1]);
                const int    et  = static_cast<int>(std::stod(cmd.parts[2]));
                if (t1 > t_cur + 1e-9) {
                    tr.segs.push_back({t_cur, t1, cur_rot_deg*kD2R, r1*kD2R, 0.0, 1.0, et});
                    tx.segs.push_back({t_cur, t1, cur_x, cur_x, 0.0, 1.0, 0});
                    ty.segs.push_back({t_cur, t1, cur_y, cur_y, 0.0, 1.0, 0});
                    ta.segs.push_back({t_cur, t1, cur_alpha, cur_alpha, 0.0, 1.0, 0});
                    cur_rot_deg = r1; t_cur = t1;
                }
            } else if (cmd.op == "cf" && cmd.parts.size() >= 2) {
                const double t1 = bpm_map.beat_to_sec(std::stod(cmd.parts[0]));
                const double a1 = std::clamp(std::stod(cmd.parts[1]), 0.0, 255.0);
                const int    et = cmd.parts.size() >= 3 ? static_cast<int>(std::stod(cmd.parts[2])) : 0;
                if (t1 > t_cur + 1e-9) {
                    ta.segs.push_back({t_cur, t1, cur_alpha, a1, 0.0, 1.0, et});
                    tx.segs.push_back({t_cur, t1, cur_x, cur_x, 0.0, 1.0, 0});
                    ty.segs.push_back({t_cur, t1, cur_y, cur_y, 0.0, 1.0, 0});
                    tr.segs.push_back({t_cur, t1, cur_rot_deg*kD2R, cur_rot_deg*kD2R, 0.0, 1.0, 0});
                    cur_alpha = a1; t_cur = t1;
                }
            }
        }
        // Extend to at least t_cur + 2s
        emit_const(t_cur, t_cur + 2.0);

        // Build scroll from cur_speed (PEC speed events just set a constant)
        const double spx = cur_speed * px_per_unit_per_sec;
        scroll.segs.push_back({0.0, t_cur + 2.0, spx, spx, 0.0});
        finalise_integral_track(scroll);

        LineAnim& a = lines_out[lid].anim;
        a.alpha_raw.default_val = 255.0;
        if (!tx.segs.empty()) a.pos_x.layers.push_back(std::move(tx));
        if (!ty.segs.empty()) a.pos_y.layers.push_back(std::move(ty));
        if (!tr.segs.empty()) a.rot_rad.layers.push_back(std::move(tr));
        if (!ta.segs.empty()) a.alpha_raw.layers.push_back(std::move(ta));
        a.scroll_px = std::move(scroll);

        for (const auto& l : a.pos_x.layers)    a.total_event_segs += static_cast<int>(l.segs.size());
        for (const auto& l : a.pos_y.layers)    a.total_event_segs += static_cast<int>(l.segs.size());
        for (const auto& l : a.rot_rad.layers)  a.total_event_segs += static_cast<int>(l.segs.size());
        for (const auto& l : a.alpha_raw.layers)a.total_event_segs += static_cast<int>(l.segs.size());
        a.total_event_segs += static_cast<int>(a.scroll_px.segs.size());
    }
}

// ================================================================

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

    const int fmt = get_int(find_ptr(root, "formatVersion"), 3);
    int next_note_id = 1;
    const json* lines = find_ptr(root, "judgeLineList");
    if (lines != nullptr && lines->is_array()) {
        int line_id = 0;
        for (const auto& line : *lines) {
            RuntimeLine rl; rl.id = line_id;
            const double bpm = std::max(1e-9, get_number(find_ptr(line, "bpm"), kDefaultOfficialBpm));
            build_official_line_anim(line, bpm, fmt, rl);
            out.chart.lines.push_back(std::move(rl));
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
            RuntimeLine rl; rl.id = line_id;
            const double bpmfactor = std::max(1e-9, get_number(find_ptr(line, "bpmfactor"), 1.0));
            build_rpe_line_anim(line, bpm_map, bpmfactor, rl);
            out.chart.lines.push_back(std::move(rl));
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

    build_pec_line_anims(payload, out.chart.lines);

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
