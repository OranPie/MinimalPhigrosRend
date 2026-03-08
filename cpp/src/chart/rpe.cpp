#include "phigros/chart/rpe.hpp"
#include "phigros/chart/bpm_map.hpp"
#include "phigros/math/easing.hpp"
#include "phigros/math/tracks.hpp"
#include "phigros/math/util.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <iostream>

using json = nlohmann::json;
using namespace phigros::math;
using phigros::CtrlPoint;

namespace phigros::chart {

// Convert RPE beat representation to float beats
static double beat_to_value(const json& b) {
    if (b.is_array() && b.size() == 3)
        return b[0].get<double>() + b[1].get<double>() / b[2].get<double>();
    if (b.is_object() && b.contains("bar") && b.contains("num") && b.contains("den"))
        return b["bar"].get<double>() + b["num"].get<double>() / b["den"].get<double>();
    return b.get<double>();
}

static RGB parse_rgb3(const json& v) {
    if (v.is_array() && v.size() >= 3) {
        return {
            static_cast<int>(clamp(v[0].get<double>(), 0, 255)),
            static_cast<int>(clamp(v[1].get<double>(), 0, 255)),
            static_cast<int>(clamp(v[2].get<double>(), 0, 255))
        };
    }
    return {255, 255, 255};
}

static std::optional<RGB> parse_rgb3_opt(const json& v) {
    if (v.is_null()) return std::nullopt;
    return parse_rgb3(v);
}

// Build a PiecewiseEased track from RPE events
static PiecewiseEased build_rpe_eased_track(
    const json& events, const BpmMap& bpm_map, double bpmfactor,
    int easing_shift, double def = 0.0)
{
    if (!events.is_array() || events.empty())
        return PiecewiseEased(def);

    struct Ev { double b0; json data; };
    std::vector<Ev> evs;
    for (auto& e : events)
        evs.push_back({beat_to_value(e["startTime"]), e});
    std::sort(evs.begin(), evs.end(), [](auto& a, auto& b) { return a.b0 < b.b0; });

    std::vector<EasedSeg> segs;
    for (auto& ev : evs) {
        const auto& e = ev.data;
        double b0 = beat_to_value(e["startTime"]);
        double b1 = beat_to_value(e["endTime"]);
        double t0 = bpm_map.beat_to_sec(b0, bpmfactor);
        double t1 = bpm_map.beat_to_sec(b1, bpmfactor);
        double v0 = e.value("start", 0.0);
        double v1 = e.value("end", 0.0);
        double L = e.value("easingLeft", 0.0);
        double R = e.value("easingRight", 1.0);

        EasedSeg seg{t0, t1, v0, v1, 0, L, R, 0, 0, 0, 0};

        int bez = e.value("bezier", 0);
        if (bez == 1 && e.contains("bezierPoints") &&
            e["bezierPoints"].is_array() && e["bezierPoints"].size() == 4) {
            seg.easing_type = -1; // bezier
            seg.bez_x1 = e["bezierPoints"][0].get<double>();
            seg.bez_y1 = e["bezierPoints"][1].get<double>();
            seg.bez_x2 = e["bezierPoints"][2].get<double>();
            seg.bez_y2 = e["bezierPoints"][3].get<double>();
        } else {
            int tp = e.value("easingType", 0) + easing_shift;
            seg.easing_type = tp;
        }
        segs.push_back(seg);
    }

    if (!segs.empty() && segs[0].t0 > 0)
        segs.insert(segs.begin(), {0.0, segs[0].t0, segs[0].v0, segs[0].v0, 0});

    return PiecewiseEased(std::move(segs), def);
}

// Build color track from RPE extended colorEvents
static PiecewiseColor build_rpe_color_track(
    const json& events, const BpmMap& bpm_map, double bpmfactor,
    int easing_shift, RGB def)
{
    if (!events.is_array() || events.empty())
        return PiecewiseColor({}, def);

    struct Ev { double b0; json data; };
    std::vector<Ev> evs;
    for (auto& e : events)
        evs.push_back({beat_to_value(e["startTime"]), e});
    std::sort(evs.begin(), evs.end(), [](auto& a, auto& b) { return a.b0 < b.b0; });

    std::vector<ColorSeg> segs;
    for (auto& ev : evs) {
        const auto& e = ev.data;
        double b0 = beat_to_value(e["startTime"]);
        double b1 = beat_to_value(e["endTime"]);
        double t0 = bpm_map.beat_to_sec(b0, bpmfactor);
        double t1 = bpm_map.beat_to_sec(b1, bpmfactor);
        RGB c0 = e.contains("start") ? parse_rgb3(e["start"]) : def;
        RGB c1 = e.contains("end") ? parse_rgb3(e["end"]) : c0;
        double L = e.value("easingLeft", 0.0);
        double R = e.value("easingRight", 1.0);

        ColorSeg seg{t0, t1, c0, c1, 0, L, R, 0, 0, 0, 0};
        int bez = e.value("bezier", 0);
        if (bez == 1 && e.contains("bezierPoints") &&
            e["bezierPoints"].is_array() && e["bezierPoints"].size() == 4) {
            seg.easing_type = -1;
            seg.bez_x1 = e["bezierPoints"][0].get<double>();
            seg.bez_y1 = e["bezierPoints"][1].get<double>();
            seg.bez_x2 = e["bezierPoints"][2].get<double>();
            seg.bez_y2 = e["bezierPoints"][3].get<double>();
        } else {
            seg.easing_type = e.value("easingType", 0) + easing_shift;
        }
        segs.push_back(seg);
    }
    return PiecewiseColor(std::move(segs), def);
}

// Build text track from RPE extended textEvents
static PiecewiseText build_rpe_text_track(
    const json& events, const BpmMap& bpm_map, double bpmfactor, int rpe_easing_shift)
{
    if (!events.is_array() || events.empty())
        return PiecewiseText({}, "");

    struct Ev { double b0; json data; };
    std::vector<Ev> evs;
    for (auto& e : events)
        evs.push_back({beat_to_value(e["startTime"]), e});
    std::sort(evs.begin(), evs.end(), [](auto& a, auto& b) { return a.b0 < b.b0; });

    std::vector<TextSeg> segs;
    for (auto& ev : evs) {
        const auto& e = ev.data;
        double b0 = beat_to_value(e["startTime"]);
        double b1 = beat_to_value(e["endTime"]);
        double t0 = bpm_map.beat_to_sec(b0, bpmfactor);
        double t1 = bpm_map.beat_to_sec(b1, bpmfactor);
        std::string s0 = e.value("start", "");
        std::string s1 = e.value("end", s0);
        // font: absent from v152+ when using default "cmdysj"; present before v152
        std::string font = e.value("font", "");
        // Easing for %P% interpolation
        int easing_type = e.value("easingType", 1) + rpe_easing_shift;
        double easing_L = e.value("easingLeft", 0.0);
        double easing_R = e.value("easingRight", 1.0);
        segs.push_back({t0, t1, std::move(s0), std::move(s1), std::move(font), easing_type, easing_L, easing_R});
    }
    return PiecewiseText(std::move(segs), "");
}

// Sample the value of one layer's speed events at time t_mid
static double sample_layer_value(const json& layer_events, const BpmMap& bpm_map,
                                  double bpmfactor, double t_mid)
{
    if (!layer_events.is_array() || layer_events.empty()) return 0.0;

    struct Ev { double t0; double t1; double s0; double s1; };
    std::vector<Ev> evs;
    for (auto& e : layer_events) {
        double b0 = beat_to_value(e["startTime"]);
        double b1 = beat_to_value(e["endTime"]);
        evs.push_back({
            bpm_map.beat_to_sec(b0, bpmfactor),
            bpm_map.beat_to_sec(b1, bpmfactor),
            e.value("start", 0.0),
            e.value("end", e.value("start", 0.0))
        });
    }
    std::sort(evs.begin(), evs.end(), [](auto& a, auto& b) { return a.t0 < b.t0; });

    double val = 0.0;
    bool any_cover = false;
    const Ev* last_before = nullptr;

    for (auto& ev : evs) {
        if (t_mid < ev.t0) break;
        last_before = &ev;
        if (t_mid >= ev.t0 && t_mid < ev.t1) {
            double u = (t_mid - ev.t0) / std::max(1e-9, ev.t1 - ev.t0);
            val += lerp(ev.s0, ev.s1, clamp(u, 0.0, 1.0));
            any_cover = true;
        }
    }
    if (any_cover) return val;
    if (last_before) return last_before->s1;
    return evs[0].s0;
}

// Build scroll track from multi-layer speed events
static IntegralTrack build_rpe_scroll_px(
    const std::vector<json>& speed_layers, const BpmMap& bpm_map,
    double bpmfactor, double px_per_unit_per_sec)
{
    // Collect all events
    bool has_any = false;
    for (auto& layer : speed_layers)
        if (layer.is_array() && !layer.empty()) { has_any = true; break; }
    if (!has_any) return IntegralTrack();

    // Collect cut points
    std::set<double> cuts_set;
    cuts_set.insert(0.0);
    for (auto& layer : speed_layers) {
        if (!layer.is_array()) continue;
        for (auto& e : layer) {
            double b0 = beat_to_value(e["startTime"]);
            double b1 = beat_to_value(e["endTime"]);
            cuts_set.insert(bpm_map.beat_to_sec(b0, bpmfactor));
            cuts_set.insert(bpm_map.beat_to_sec(b1, bpmfactor));
        }
    }
    std::vector<double> cuts(cuts_set.begin(), cuts_set.end());
    std::sort(cuts.begin(), cuts.end());

    if (cuts.size() < 2) cuts.push_back(cuts.back() + 1e6);
    else cuts.push_back(cuts.back() + 1e6);

    std::vector<Seg1D> segs;
    double prefix = 0.0;
    for (size_t i = 0; i + 1 < cuts.size(); ++i) {
        double t0 = cuts[i], t1 = cuts[i + 1];
        if (t1 <= t0) continue;
        double tm = (t0 + t1) * 0.5;
        double v_unit = 0.0;
        for (auto& layer : speed_layers)
            v_unit += sample_layer_value(layer, bpm_map, bpmfactor, tm);
        double v = v_unit * px_per_unit_per_sec;
        segs.push_back({t0, t1, v, v, prefix});
        prefix += v * (t1 - t0);
    }
    return IntegralTrack(std::move(segs));
}

ChartData load_rpe(const json& data, int W, int H, int rpe_easing_shift) {
    ChartData result;

    auto meta = data.value("META", json::object());
    double offset_ms = meta.value("offset", 0.0);
    result.offset = offset_ms / 1000.0;
    // RPE META: song and background paths (relative to chart root)
    if (meta.contains("song") && meta["song"].is_string())
        result.meta_song_path = meta["song"].get<std::string>();
    if (meta.contains("background") && meta["background"].is_string())
        result.meta_bg_path = meta["background"].get<std::string>();

    // Build BPM map
    auto bpm_list = data.value("BPMList", json::array());
    std::vector<std::pair<double, double>> bpm_items;
    for (auto& e : bpm_list)
        bpm_items.emplace_back(beat_to_value(e["startTime"]), e.value("bpm", 120.0));
    BpmMap bpm_map = BpmMap::build(std::move(bpm_items));

    const auto& jls = data["judgeLineList"];
    int line_count = static_cast<int>(jls.size());

    double sx = static_cast<double>(W) / 1350.0;
    double sy = static_cast<double>(H) / 900.0;
    double px_per_unit_per_sec = 120.0 * sy;

    // Track father info for post-composition
    std::vector<int> fathers;
    std::vector<bool> rot_with_fathers;

    for (int i = 0; i < line_count; ++i) {
        const auto& jl = jls[i];
        double bpmfactor = jl.value("bpmfactor", 1.0);

        auto layers = jl.value("eventLayers", json::array());

        std::vector<PiecewiseEased> move_x_tracks, move_y_tracks, rot_tracks, alpha_tracks;
        std::vector<json> speed_layers;

        for (auto& layer : layers) {
            if (layer.is_null()) continue;
            move_x_tracks.push_back(build_rpe_eased_track(
                layer.value("moveXEvents", json::array()), bpm_map, bpmfactor, rpe_easing_shift, 0.0));
            move_y_tracks.push_back(build_rpe_eased_track(
                layer.value("moveYEvents", json::array()), bpm_map, bpmfactor, rpe_easing_shift, 0.0));
            rot_tracks.push_back(build_rpe_eased_track(
                layer.value("rotateEvents", json::array()), bpm_map, bpmfactor, rpe_easing_shift, 0.0));
            alpha_tracks.push_back(build_rpe_eased_track(
                layer.value("alphaEvents", json::array()), bpm_map, bpmfactor, rpe_easing_shift, 255.0));
            speed_layers.push_back(layer.value("speedEvents", json::array()));
        }

        // Fallback: speed events at judgeLineLevel
        bool has_speed = false;
        for (auto& sl : speed_layers)
            if (sl.is_array() && !sl.empty()) { has_speed = true; break; }
        if (!has_speed && jl.contains("speedEvents") && jl["speedEvents"].is_array() && !jl["speedEvents"].empty())
            speed_layers = {jl["speedEvents"]};

        // Build SumTracks for multi-layer composition
        auto sum_x = std::make_shared<SumTrack>(std::move(move_x_tracks), 0.0);
        auto sum_y = std::make_shared<SumTrack>(std::move(move_y_tracks), 0.0);
        auto sum_rot = std::make_shared<SumTrack>(std::move(rot_tracks), 0.0);
        auto sum_alpha = std::make_shared<SumTrack>(std::move(alpha_tracks), 255.0);

        auto scroll = build_rpe_scroll_px(speed_layers, bpm_map, bpmfactor, px_per_unit_per_sec);

        RGB rgb = hsv_to_rgb(static_cast<double>(i) / std::max(1, line_count), 0.65, 0.95);

        Line line;
        line.lid = i;
        // RPE coords → pixel: x_px = (moveX + 675) * sx, y_px = (450 - moveY) * sy
        line.pos_x = [sum_x, sx](double t) { return (sum_x->eval(t) + 675.0) * sx; };
        line.pos_y = [sum_y, sy](double t) { return (450.0 - sum_y->eval(t)) * sy; };
        line.rot = [sum_rot](double t) { return sum_rot->eval(t) * M_PI / 180.0; };
        line.alpha = [sum_alpha](double t) -> double {
            double v = sum_alpha->eval(t);
            if (v <= 1.000001) return clamp(v, 0.0, 1.0);
            return clamp(v / 255.0, 0.0, 1.0);
        };
        line.scroll_px = std::move(scroll);
        line.color_rgb = rgb;
        line.name = jl.value("name", "");

        // attachUI: binds line to a HUD element; warn once per unique value
        if (jl.contains("attachUI") && !jl["attachUI"].is_null()) {
            std::string ui = jl.value("attachUI", "");
            if (!ui.empty()) {
                line.attach_ui = ui;
                static std::unordered_set<std::string> warned;
                if (warned.find(ui) == warned.end()) {
                    std::cerr << "[RPE] attachUI=\"" << ui
                              << "\" — UI element binding is not yet rendered; "
                                 "line will be hidden as per spec.\n";
                    warned.insert(ui);
                }
            }
        }

        // Extended fields
        auto ext = jl.value("extended", json::object());
        if (ext.contains("colorEvents") && ext["colorEvents"].is_array() && !ext["colorEvents"].empty())
            line.color = std::make_shared<PiecewiseColor>(
                build_rpe_color_track(ext["colorEvents"], bpm_map, bpmfactor, rpe_easing_shift, rgb));
        if (ext.contains("scaleXEvents") && ext["scaleXEvents"].is_array() && !ext["scaleXEvents"].empty())
            line.scale_x = std::make_shared<PiecewiseEased>(
                build_rpe_eased_track(ext["scaleXEvents"], bpm_map, bpmfactor, rpe_easing_shift, 1.0));
        if (ext.contains("scaleYEvents") && ext["scaleYEvents"].is_array() && !ext["scaleYEvents"].empty())
            line.scale_y = std::make_shared<PiecewiseEased>(
                build_rpe_eased_track(ext["scaleYEvents"], bpm_map, bpmfactor, rpe_easing_shift, 1.0));
        if (ext.contains("textEvents") && ext["textEvents"].is_array() && !ext["textEvents"].empty())
            line.text = std::make_shared<PiecewiseText>(
                build_rpe_text_track(ext["textEvents"], bpm_map, bpmfactor, rpe_easing_shift));
        if (ext.contains("gifEvents") && ext["gifEvents"].is_array() && !ext["gifEvents"].empty())
            line.gif_progress = std::make_shared<PiecewiseEased>(
                build_rpe_eased_track(ext["gifEvents"], bpm_map, bpmfactor, rpe_easing_shift, 0.0));

        if (jl.contains("Texture")) {
            std::string tp = jl.value("Texture", "");
            if (!tp.empty() && tp != "line.png") line.texture_path = tp;
        }
        if (jl.contains("anchor") && jl["anchor"].is_array() && jl["anchor"].size() >= 2)
            line.anchor = {jl["anchor"][0].get<double>(), jl["anchor"][1].get<double>()};
        line.is_gif = jl.value("isGif", false);

        // zOrder: explicit draw order (higher = on top)
        line.z_order = jl.value("zOrder", 0);

        // isCover: line acts as a cover layer drawn over notes (default 1 per RPE docs)
        line.is_cover = (jl.value("isCover", 1) != 0);

        // inclineEvents: perspective tilt track
        if (ext.contains("inclineEvents") && ext["inclineEvents"].is_array() && !ext["inclineEvents"].empty())
            line.incline = std::make_shared<PiecewiseEased>(
                build_rpe_eased_track(ext["inclineEvents"], bpm_map, bpmfactor, rpe_easing_shift, 0.0));

        // Control events: per-x-position note property modifiers
        auto parse_ctrl = [](const json& arr, const std::string& value_key) -> std::vector<CtrlPoint> {
            std::vector<CtrlPoint> pts;
            if (!arr.is_array()) return pts;
            for (const auto& e : arr) {
                CtrlPoint p;
                p.x      = e.value("x", 0.0f);
                p.value  = e.value(value_key, 1.0f);
                p.easing = e.value("easing", 1);
                pts.push_back(p);
            }
            std::sort(pts.begin(), pts.end(), [](const CtrlPoint& a, const CtrlPoint& b) {
                return a.x < b.x;
            });
            return pts;
        };

        if (jl.contains("alphaControl"))
            line.alpha_ctrl = parse_ctrl(jl["alphaControl"], "alpha");
        if (jl.contains("posControl"))
            line.pos_ctrl   = parse_ctrl(jl["posControl"],   "pos");
        if (jl.contains("sizeControl"))
            line.size_ctrl  = parse_ctrl(jl["sizeControl"],  "size");
        if (jl.contains("yControl"))
            line.y_ctrl     = parse_ctrl(jl["yControl"],     "y");
        if (jl.contains("skewControl"))
            line.skew_ctrl  = parse_ctrl(jl["skewControl"],  "skew");

        int father = jl.value("father", -1);
        // rotateWithFather: absent field defaults to false for pre-v163 compatibility (RPE docs)
        bool rwf = jl.value("rotateWithFather", false);
        line.father = father;
        line.rotate_with_father = rwf;
        fathers.push_back(father);
        rot_with_fathers.push_back(rwf);

        result.lines.push_back(std::move(line));

        // Notes
        int nid_base = i * 100000;
        int nid = nid_base;
        auto notes_arr = jl.value("notes", json::array());
        for (auto& n : notes_arr) {
            // RPE type: 1=Tap, 2=Hold, 3=Flick, 4=Drag
            // Internal:  1=Tap, 2=Drag, 3=Hold, 4=Flick
            int rpe_type = n.value("type", 1);
            int kind;
            if (rpe_type == 2) kind = 3;      // RPE Hold → internal Hold
            else if (rpe_type == 3) kind = 4;  // RPE Flick → internal Flick
            else if (rpe_type == 4) kind = 2;  // RPE Drag → internal Drag
            else kind = 1;

            double b0 = beat_to_value(n.value("startTime", json::array({0, 0, 1})));
            double b1 = beat_to_value(n.value("endTime", n.value("startTime", json::array({0, 0, 1}))));
            double t_hit = bpm_map.beat_to_sec(b0, bpmfactor);
            double t_end = bpm_map.beat_to_sec(b1, bpmfactor);

            // Any note with duration → hold
            if (t_end > t_hit + 1e-9) kind = 3;

            // above: RPE above=1 means front side; we invert
            int above_raw = n.value("above", 1);
            bool above = (above_raw != 1);
            bool fake = (n.value("isFake", 0) == 1);

            double posx_units = n.value("positionX", 0.0);
            double y_offset_units = n.value("yOffset", 0.0);
            double size = n.value("size", 1.0);
            double speed_mul = n.value("speed", 1.0);

            double alpha_note = 1.0;
            if (n.contains("alpha") && !n["alpha"].is_null())
                alpha_note = clamp(n["alpha"].get<double>() / 255.0, 0.0, 1.0);

            Note note;
            note.nid = nid++;
            note.line_id = i;
            note.kind = kind;
            note.above = above;
            note.fake = fake;
            note.t_hit = t_hit;
            note.t_end = (kind == 3) ? t_end : t_hit;
            note.x_local_px = posx_units * sx;
            note.y_offset_px = y_offset_units * sy;
            note.speed_mul = speed_mul;
            note.size_px = size;
            note.alpha01 = alpha_note;

            if (n.contains("tint") && !n["tint"].is_null())
                note.tint_rgb = parse_rgb3(n["tint"]);
            else if (n.contains("color") && !n["color"].is_null())
                note.tint_rgb = parse_rgb3(n["color"]);

            if (n.contains("tintHitEffects") && !n["tintHitEffects"].is_null())
                note.tint_hitfx_rgb = parse_rgb3(n["tintHitEffects"]);

            if (n.contains("hitsound") && !n["hitsound"].is_null())
                note.hitsound_path = n.value("hitsound", "");

            // visibleTime: seconds before hit that note becomes visible (default 999999)
            note.visible_time = n.value("visibleTime", 999999.0f);

            result.notes.push_back(std::move(note));
        }
    }

    // Cache scroll samples
    std::unordered_map<int, Line*> line_map;
    for (auto& ln : result.lines) line_map[ln.lid] = &ln;
    for (auto& n : result.notes) {
        auto* ln = line_map[n.line_id];
        n.scroll_hit = ln->scroll_px.integral(n.t_hit);
        n.scroll_end = ln->scroll_px.integral(n.t_end);
    }

    // Father/child line composition
    // Save base track functions before composing
    std::vector<TrackFn> base_x(line_count), base_y(line_count), base_r(line_count);
    for (int i = 0; i < line_count; ++i) {
        base_x[i] = result.lines[i].pos_x;
        base_y[i] = result.lines[i].pos_y;
        base_r[i] = result.lines[i].rot;
    }

    // DFS composition with cycle detection
    // state: 0=unvisited, 1=visiting, 2=done
    std::vector<int> state_mark(line_count, 0);
    struct Composed { TrackFn x, y, r; };
    std::vector<Composed> cache(line_count);

    std::function<Composed(int)> build_comp = [&](int lid) -> Composed {
        if (lid < 0 || lid >= line_count) {
            auto zero = [](double) { return 0.0; };
            return {zero, zero, zero};
        }
        if (state_mark[lid] == 2) return cache[lid];
        if (state_mark[lid] == 1)
            throw std::runtime_error("RPE father cycle detected at line " + std::to_string(lid));
        state_mark[lid] = 1;

        int f = fathers[lid];
        auto bx = base_x[lid], by = base_y[lid], br = base_r[lid];

        if (f < 0 || f >= line_count) {
            cache[lid] = {bx, by, br};
        } else {
            Composed parent = build_comp(f);
            TrackFn px = parent.x, py = parent.y, pr = parent.r;
            TrackFn cx = [bx, px](double t) { return bx(t) + px(t); };
            TrackFn cy = [by, py](double t) { return by(t) + py(t); };
            TrackFn cr;
            if (rot_with_fathers[lid])
                cr = [br, pr](double t) { return br(t) + pr(t); };
            else
                cr = br;
            cache[lid] = {cx, cy, cr};
        }
        state_mark[lid] = 2;
        return cache[lid];
    };

    for (int lid = 0; lid < line_count; ++lid) {
        auto [cx, cy, cr] = build_comp(lid);
        result.lines[lid].pos_x = cx;
        result.lines[lid].pos_y = cy;
        result.lines[lid].rot = cr;
    }

    // Sort notes by t_hit
    std::sort(result.notes.begin(), result.notes.end(),
              [](const Note& a, const Note& b) { return a.t_hit < b.t_hit; });

    result.finalize();
    return result;
}

} // namespace phigros::chart
