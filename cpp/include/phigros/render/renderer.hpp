#pragma once
#include "phigros/core/types.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/hud/hud.hpp"
#include <vector>
#include <array>
#include <algorithm>
#include <string>

namespace phigros::render {

// Legacy helper for optional kind-based tinting (e.g. via mods/tools).
// Default rendering uses Note::tint_rgb directly.
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
    double x, y, rot, cos_rot, sin_rot, alpha01, scroll;
    math::RGB color;
    float incline = 0.0f;   // RPE inclineEvents: perspective tilt (degrees)
    bool is_cover = false;  // RPE isCover: drawn over notes
    int z_order = 0;        // RPE zOrder
    float scale_x = 1.0f;  // RPE scaleXEvents
    float scale_y = 1.0f;  // RPE scaleYEvents
    // Pointer into stable Line::texture_path — avoids per-frame heap allocation.
    // Null or pointing to empty string means default line rendering.
    const std::string* texture_path = nullptr;
    std::string text;         // RPE textEvents current value (computed per-frame)
};

struct NoteSnapshot {
    int nid;
    int kind;
    double wx, wy;           // world position (head)
    double wx_tail, wy_tail; // world position (tail, for holds)
    double alpha;
    double line_rot;         // line rotation for note alignment
    double size_px = 1.0;    // per-note size multiplier (from Note::size_px)
    math::RGB color;
    bool is_hold;
    bool judged;
    bool miss;
    bool mh;                 // multi-hit flag
    bool holding = false;    // true while a hold note is actively held
    bool draw_hold_head = true; // false when holding and respack holdKeepHead=false
    float skew = 0.0f;       // RPE skewControl: note skew angle (degrees)
};

struct FrameSnapshot {
    double t;
    std::vector<LineSnapshot> lines;
    std::vector<NoteSnapshot> notes;
    hud::HudState hud;
};

// Build a full frame snapshot for time t
// Apply Python-style expand transform: compress world coords toward screen centre
// by 1/expand, making a larger portion of the playfield visible.
// Matches phic_renderer's apply_expand_xy(x, y, W, H, expand).
inline void apply_expand_xy(double& x, double& y, int W, int H, double expand) {
    if (expand <= 1.000001) return;
    double cx = W * 0.5, cy = H * 0.5;
    double s = 1.0 / expand;
    x = cx + (x - cx) * s;
    y = cy + (y - cy) * s;
}

inline FrameSnapshot build_frame(
    double t,
    const ChartData& chart,
    const std::vector<NoteState>& states,
    const engine::Judge& judge,
    const config::RenderConfig& cfg)
{
    FrameSnapshot frame;
    frame.t = t;

    // Evaluate all line states.
    // Fast path: stack storage for charts <= 256 lines.
    // Fallback: heap storage for larger charts (avoids silently dropping notes).
    std::array<engine::LineState, 256> ls_arr{};
    std::vector<engine::LineState> ls_heap;
    const size_t n_lines = chart.lines.size();
    if (n_lines > ls_arr.size()) ls_heap.resize(n_lines);
    frame.lines.reserve(n_lines);
    for (size_t i = 0; i < n_lines; ++i) {
        auto& ln = chart.lines[i];
        auto ls = engine::eval_line_state(
            ln, t, cfg.force_line_alpha01,
            cfg.force_line_alpha01_by_lid
                ? &(*cfg.force_line_alpha01_by_lid) : nullptr);
        if (i < ls_arr.size()) ls_arr[i] = ls;
        else                   ls_heap[i] = ls;
        float incline_deg = ln.incline ? static_cast<float>(ln.incline->eval(t)) : 0.0f;
        float sx = ln.scale_x ? static_cast<float>(ln.scale_x->eval(t)) : 1.0f;
        float sy = ln.scale_y ? static_cast<float>(ln.scale_y->eval(t)) : 1.0f;
        std::string line_text = ln.text ? ln.text->eval(t) : std::string{};
        // attachUI lines are hidden per RPE spec (UI element takes over rendering).
        // Lines with textEvents are also always hidden — only the text is shown.
        double eff_alpha = (!ln.attach_ui.empty() || ln.text) ? 0.0 : ls.alpha01;
        // Color: when line has textEvents but no colorEvents, default is white per RPE spec.
        // colorEvents (ln.color) can override this.
        const math::RGB text_white{255, 255, 255};
        auto line_color = ln.compiled_color ? ln.compiled_color(t)
                        : (ln.color ? ln.color->eval(t)
                                    : (ln.text ? text_white : ln.color_rgb));
        frame.lines.push_back({
            ln.lid, ls.x, ls.y, ls.rot, ls.cos_rot, ls.sin_rot,
            eff_alpha, ls.scroll,
            line_color,
            incline_deg, ln.is_cover, ln.z_order,
            sx, sy, &ln.texture_path, std::move(line_text)
        });
    }

    // Sort lines by z_order for correct draw order
    std::stable_sort(frame.lines.begin(), frame.lines.end(),
        [](const LineSnapshot& a, const LineSnapshot& b) {
            return a.z_order < b.z_order;
        });

    // Evaluate notes — only screen-position culling is applied.
    double flow_mul = cfg.note_flow_speed_multiplier;
    const int W = cfg.window_w;
    const int H = cfg.window_h;
    const double ex = std::max(1.0, cfg.expand_factor);
    const double over = std::max(1.0, cfg.overrender);
    const double base_w = 0.06 * W * cfg.note_scale_x;
    const double base_h = 0.018 * H * cfg.note_scale_y;
    static thread_local size_t s_last_note_count = 32;
    frame.notes.reserve(s_last_note_count + 8);

    auto point_visible = [&](double x, double y, double half_w, double half_h) {
        return (x + half_w >= 0.0 && x - half_w <= W &&
                y + half_h >= 0.0 && y - half_h <= H);
    };

    // Helper: emit one note into the frame snapshot
    auto emit_note = [&](size_t i) {
        const auto& note = chart.notes[i];
        const auto& ns   = states[i];

        if (ns.judged && note.kind != 3) return;
        if (ns.miss) return;
        if (note.line_id < 0 || note.line_id >= static_cast<int>(n_lines)) return;

        // visible_time: note is hidden until (t_hit - visible_time) seconds before hit
        if (note.visible_time < 999998.0 && t < note.t_hit - note.visible_time) return;

        // Optional t_enter/t_end culling for dense charts.
        if (!cfg.no_cull && !cfg.no_cull_enter_time) {
            if (t < note.t_enter) return;
            if (t > note.t_end + 0.5) return;
        }

        const auto& ls = (static_cast<size_t>(note.line_id) < ls_arr.size())
            ? ls_arr[note.line_id]
            : ls_heap[note.line_id];

        // Evaluate control events for this note's scroll distance from the judge line.
        // The 'x' field in RPE control events is "note与判定线的纵向距离" —
        // the note's perpendicular distance from the line in RPE y-units (not x position).
        const auto& ln = chart.lines[note.line_id];
        float scroll_dist_rpe = static_cast<float>((note.scroll_hit - ls.scroll) / (H / 900.0));
        float ctrl_alpha = eval_ctrl(ln.alpha_ctrl, scroll_dist_rpe, 1.0f);
        float ctrl_size  = eval_ctrl(ln.size_ctrl,  scroll_dist_rpe, 1.0f);
        float ctrl_skew  = eval_ctrl(ln.skew_ctrl,  scroll_dist_rpe, 0.0f);
        // posControl.pos = multiplier for positionX (not a y-offset)
        float ctrl_pos   = eval_ctrl(ln.pos_ctrl,   scroll_dist_rpe, 1.0f);
        float ctrl_y     = eval_ctrl(ln.y_ctrl,     scroll_dist_rpe, 0.0f);

        auto head = engine::note_world_pos_cs(
            ls.x, ls.y, ls.cos_rot, ls.sin_rot, ls.scroll, note,
            note.scroll_hit, false, flow_mul,
            cfg.note_speed_mul_affects_travel,
            note.kind == 3 && ns.holding);

        // posControl: pos is a multiplier for positionX — shift head along the line tangent
        if (ctrl_pos != 1.0f) {
            double dx_extra = (ctrl_pos - 1.0) * note.x_local_px;
            head.x += ls.cos_rot * dx_extra;
            head.y += ls.sin_rot * dx_extra;
        }
        // yControl: y is an additional perpendicular offset (RPE y-units)
        if (ctrl_y != 0.0f) {
            double sgn = note.above ? -1.0 : 1.0; // screen y: above=front → negative normal
            double dy = sgn * ctrl_y * (H / 900.0);
            head.x += -ls.sin_rot * dy;
            head.y +=  ls.cos_rot * dy;
        }

        double wx_tail = head.x, wy_tail = head.y;
        if (note.kind == 3) {
            auto tail = engine::note_world_pos_cs(
                ls.x, ls.y, ls.cos_rot, ls.sin_rot, ls.scroll, note,
                note.scroll_end, true, flow_mul,
                cfg.note_speed_mul_affects_travel, false);
            wx_tail = tail.x;
            wy_tail = tail.y;
        }

        // The only culling rule: out-of-screen (after expand transform).
        if (!cfg.no_cull && !cfg.no_cull_screen) {
            double hx = head.x, hy = head.y;
            apply_expand_xy(hx, hy, W, H, ex);

            const double half_w = std::max(1.0, base_w * note.size_px * ctrl_size * 0.5 * over);
            const double half_h = std::max(1.0, base_h * note.size_px * ctrl_size * 0.5 * over);

            bool vis = point_visible(hx, hy, half_w, half_h);
            if (!vis && note.kind == 3) {
                double tx = wx_tail, ty = wy_tail;
                apply_expand_xy(tx, ty, W, H, ex);
                vis = point_visible(tx, ty, half_w, half_h) ||
                      ((std::min(hx, tx) <= W && std::max(hx, tx) >= 0.0) &&
                       (std::min(hy, ty) <= H && std::max(hy, ty) >= 0.0));
            }
            if (!vis) return;
        }

        auto color = note.tint_rgb;
        double note_alpha = note.alpha01 * ctrl_alpha * cfg.note_alpha;
        switch (cfg.line_alpha_mode) {
        case config::LineAlphaMode::Always:
            note_alpha *= ls.alpha01;
            break;
        case config::LineAlphaMode::NegativeOnly:
            if (ls.alpha_raw < 0.0)
                note_alpha *= ls.alpha01;
            break;
        default: break;
        }

        frame.notes.push_back({
            note.nid, note.kind, head.x, head.y,
            wx_tail, wy_tail, note_alpha, ls.rot,
            note.size_px * ctrl_size, color,
            note.kind == 3, ns.judged, ns.miss, note.mh,
            ns.holding,
            !(note.kind == 3 && ns.holding),
            ctrl_skew
        });
    };

    for (size_t i = 0; i < chart.notes.size(); ++i) emit_note(i);

    s_last_note_count = frame.notes.size();  // 6B5: update adaptive reserve hint

    // Sort by (non-hold first for z-order, then by kind) to batch same-texture notes
    // and reduce SDL texture state thrashing in SdlExecutor
    std::stable_sort(frame.notes.begin(), frame.notes.end(),
        [](const NoteSnapshot& a, const NoteSnapshot& b) {
            if (a.is_hold != b.is_hold) return a.is_hold < b.is_hold;
            return a.kind < b.kind;
        });

    // Update HUD — use precomputed metadata to avoid O(N) loops
    auto sr = engine::compute_score(judge.acc_sum, judge.max_combo,
                                     static_cast<int>(chart.notes.size()));
    hud::update_hud(frame.hud, sr.score, sr.acc_ratio,
                    judge.combo, judge.max_combo, t,
                    chart.chart_end_t, chart.playable_count);

    return frame;
}

} // namespace phigros::render
