#pragma once
#include "phigros/core/types.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/hud/hud.hpp"
#include <vector>
#include <string>

namespace phigros::render {

// Note type colors (matching Python's NOTE_TYPE_COLORS)
inline math::RGB note_type_color(int kind) {
    switch (kind) {
        case 1: return {255, 220, 120}; // Tap
        case 2: return {140, 240, 255}; // Drag
        case 3: return {120, 200, 255}; // Hold
        case 4: return {255, 140, 220}; // Flick
        default: return {255, 255, 255};
    }
}

// Frame snapshot: computed per-frame state for all lines and visible notes
struct LineSnapshot {
    int lid;
    double x, y, rot, alpha01, scroll;
    math::RGB color;
};

struct NoteSnapshot {
    int nid;
    int kind;
    double wx, wy;        // world position (head)
    double wx_tail, wy_tail; // world position (tail, for holds)
    double alpha;
    double line_rot;      // line rotation for note alignment
    math::RGB color;
    bool is_hold;
    bool judged;
    bool miss;
    bool mh;              // multi-hit flag
};

struct FrameSnapshot {
    double t;
    std::vector<LineSnapshot> lines;
    std::vector<NoteSnapshot> notes;
    hud::HudState hud;
};

// Build a full frame snapshot for time t
inline FrameSnapshot build_frame(
    double t,
    const ChartData& chart,
    const std::vector<NoteState>& states,
    const engine::Judge& judge,
    const config::RenderConfig& cfg)
{
    FrameSnapshot frame;
    frame.t = t;

    // Evaluate all line states
    std::vector<engine::LineState> ls_cache(chart.lines.size());
    frame.lines.reserve(chart.lines.size());
    for (size_t i = 0; i < chart.lines.size(); ++i) {
        auto& ln = chart.lines[i];
        auto ls = engine::eval_line_state(
            ln, t, cfg.force_line_alpha01,
            cfg.force_line_alpha01_by_lid
                ? &(*cfg.force_line_alpha01_by_lid) : nullptr);
        ls_cache[i] = ls;
        frame.lines.push_back({
            ln.lid, ls.x, ls.y, ls.rot, ls.alpha01, ls.scroll,
            ln.color ? ln.color->eval(t) : ln.color_rgb
        });
    }

    // Evaluate visible notes
    double flow_mul = cfg.note_flow_speed_multiplier;
    frame.notes.reserve(chart.notes.size() / 4);
    for (size_t i = 0; i < chart.notes.size(); ++i) {
        auto& note = chart.notes[i];
        auto& ns = states[i];

        // Skip judged non-holds and missed notes past their time
        if (ns.judged && note.kind != 3) continue;
        if (ns.miss) continue;

        // Simple culling
        if (note.t_hit > t + cfg.approach * 2.0) continue;
        if (note.t_end < t - 0.5) continue;

        if (note.line_id < 0 || note.line_id >= static_cast<int>(chart.lines.size()))
            continue;
        auto& ls = ls_cache[note.line_id];

        // Head position
        auto head = engine::note_world_pos(
            ls.x, ls.y, ls.rot, ls.scroll, note,
            note.scroll_hit, false, flow_mul,
            cfg.note_speed_mul_affects_travel, false);

        // Tail position (holds)
        double wx_tail = head.x, wy_tail = head.y;
        if (note.kind == 3) {
            auto tail = engine::note_world_pos(
                ls.x, ls.y, ls.rot, ls.scroll, note,
                note.scroll_end, true, flow_mul,
                cfg.note_speed_mul_affects_travel, false);
            wx_tail = tail.x;
            wy_tail = tail.y;
        }

        auto color = note_type_color(note.kind);
        frame.notes.push_back({
            note.nid, note.kind, head.x, head.y,
            wx_tail, wy_tail, note.alpha01, ls.rot, color,
            note.kind == 3, ns.judged, ns.miss, note.mh
        });
    }

    // Update HUD
    auto sr = engine::compute_score(judge.acc_sum, judge.max_combo,
                                     static_cast<int>(chart.notes.size()));
    double chart_end = 0.0;
    for (auto& n : chart.notes)
        chart_end = std::max(chart_end, n.t_end);

    int playable = 0;
    for (auto& n : chart.notes) if (!n.fake) ++playable;

    hud::update_hud(frame.hud, sr.score, sr.acc_ratio,
                    judge.combo, judge.max_combo, t, chart_end, playable);

    return frame;
}

} // namespace phigros::render
