#pragma once
#include "phigros/core/types.hpp"
#include "phigros/engine/kinematics.hpp"
#include <cmath>
#include <algorithm>
#include <vector>

namespace phigros::engine {

// Get scroll speed (px/sec) at time t from IntegralTrack segments
inline double scroll_speed_at(const math::IntegralTrack& track, double t) {
    for (const auto& s : track.segs) {
        if (t < s.t0) break;
        if (t <= s.t1) return std::abs(s.v0);
    }
    if (!track.segs.empty()) return std::abs(track.segs.back().v1);
    return 0.0;
}

// AABB visibility check with margin
inline bool note_visible_on_screen(
    const std::vector<Line>& lines,
    const Note& note, double t,
    int W, int H,
    int base_w, int base_h,
    double expand_factor = 1.0,
    double note_scale_x = 1.0,
    double note_scale_y = 1.0)
{
    if (note.line_id < 0 || note.line_id >= static_cast<int>(lines.size()))
        return false;
    auto& ln = lines[note.line_id];
    auto ls = eval_line_state(ln, t);
    auto pos = note_world_pos(ls.x, ls.y, ls.rot, ls.scroll, note, note.scroll_hit);

    double ex = std::max(1.0, expand_factor);
    double sx = note_scale_x / ex;
    double sy = note_scale_y / ex;
    double w = base_w * note.size_px * sx;
    double h = base_h * note.size_px * sy;

    double cx = W * 0.5, cy = H * 0.5;
    double half_w = W * ex * 0.5, half_h = H * ex * 0.5;
    double left = cx - half_w, right = cx + half_w;
    double top = cy - half_h, bottom = cy + half_h;

    int margin = std::max(120, static_cast<int>(0.18 * std::max(W, H) * ex));

    return (pos.x + w / 2 >= left - margin && pos.x - w / 2 <= right + margin &&
            pos.y + h / 2 >= top - margin  && pos.y - h / 2 <= bottom + margin);
}

// Precompute t_enter for all notes.
// expand_factor must match the value used during rendering so that the
// visibility check sees the same viewport as the actual draw pass.
inline void precompute_t_enter(
    std::vector<Line>& lines,
    std::vector<Note>& notes,
    int W, int H,
    double expand_factor    = 1.0,
    double lookback_default = 256.0,
    double dt_init          = 1.0 / 30.0)
{
    int base_w = static_cast<int>(0.06 * W);
    int base_h = static_cast<int>(0.018 * H);
    dt_init = std::max(1e-4, dt_init);
    constexpr int MAX_EXPAND = 32;

    auto visible = [&](const Note& n, double t) {
        return note_visible_on_screen(lines, n, t, W, H, base_w, base_h,
                                      expand_factor);
    };

    for (auto& n : notes) {
        if (n.fake) { n.t_enter = -1e9; continue; }

        double t_hit = n.t_hit;

        // Check scroll speed — if near-zero, note is always visible
        if (n.line_id >= 0 && n.line_id < static_cast<int>(lines.size())) {
            double v = scroll_speed_at(lines[n.line_id].scroll_px, t_hit);
            if (v <= 1e-4) { n.t_enter = -1e9; continue; }
        }

        // Find a visible point (prefer t_hit)
        double t_vis = t_hit;
        bool vis_at_hit = visible(n, t_vis);

        if (!vis_at_hit) {
            double step = dt_init;
            bool found = false;
            for (int i = 0; i < MAX_EXPAND; ++i) {
                double t2 = t_hit - step;
                if (t2 < t_hit - lookback_default) break;
                if (visible(n, t2)) { t_vis = t2; found = true; break; }
                step *= 2.0;
            }
            if (!found) { n.t_enter = t_hit - lookback_default; continue; }
        }

        // Exponential search backward from visible point
        double hi = t_vis;
        double lo = 0.0;
        bool lo_found = false;
        double step = dt_init;
        for (int i = 0; i < MAX_EXPAND; ++i) {
            double t2 = hi - step;
            if (t2 < t_hit - lookback_default) break;
            if (visible(n, t2)) {
                hi = t2;
                step *= 2.0;
            } else {
                lo = t2; lo_found = true; break;
            }
        }

        if (!lo_found) { n.t_enter = t_hit - lookback_default; continue; }

        // Binary search refinement (lo=invisible, hi=visible)
        for (int i = 0; i < 20; ++i) {
            double mid = (lo + hi) * 0.5;
            if (visible(n, mid)) hi = mid; else lo = mid;
        }
        n.t_enter = hi;
    }
}

} // namespace phigros::engine
