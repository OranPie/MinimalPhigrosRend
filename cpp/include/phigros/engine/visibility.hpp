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

// Check if a line has any speed≈0 segment before time t
inline bool line_has_zero_speed_before(const math::IntegralTrack& track, double t) {
    for (const auto& s : track.segs) {
        if (s.t0 >= t) break;
        double seg_end = std::min(s.t1, t);
        if (seg_end > s.t0 && std::abs(s.v0) <= 1e-4)
            return true;
    }
    return false;
}

// Find earliest zero-speed segment start time on a line
inline double earliest_zero_speed_time(const math::IntegralTrack& track, double before_t) {
    for (const auto& s : track.segs) {
        if (s.t0 >= before_t) break;
        if (std::abs(s.v0) <= 1e-4) return s.t0;
    }
    return before_t;
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
//
// Handles "acting notes" / "release" pattern: notes on lines with speed=0
// segments may be visible long before their t_hit. These notes are placed early
// (speed=0 parks them at a fixed scroll offset), then the line moves to them
// at hit time. The algorithm checks visibility during zero-speed periods and
// extends lookback accordingly.
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

        // Check scroll speed at hit time
        bool has_zero_speed_history = false;
        if (n.line_id >= 0 && n.line_id < static_cast<int>(lines.size())) {
            const auto& track = lines[n.line_id].scroll_px;
            double v = scroll_speed_at(track, t_hit);

            // Speed=0 at hit time: note is a stationary "release" note — always visible
            if (v <= 1e-4) { n.t_enter = -1e9; continue; }

            // Check if line had ANY speed=0 period before t_hit.
            // If so, the note may be an "acting note" visible much earlier than
            // the normal approach window suggests.
            has_zero_speed_history = line_has_zero_speed_before(track, t_hit);
        }

        // For acting notes, extend lookback to cover the zero-speed period
        double effective_lookback = lookback_default;
        if (has_zero_speed_history) {
            double t_zero = earliest_zero_speed_time(
                lines[n.line_id].scroll_px, t_hit);
            effective_lookback = std::max(lookback_default, t_hit - t_zero + 10.0);
        }

        // Find a visible point (prefer t_hit)
        double t_vis = t_hit;
        bool vis_at_hit = visible(n, t_vis);

        if (!vis_at_hit) {
            double step = dt_init;
            bool found = false;
            for (int i = 0; i < MAX_EXPAND; ++i) {
                double t2 = t_hit - step;
                if (t2 < t_hit - effective_lookback) break;
                if (visible(n, t2)) { t_vis = t2; found = true; break; }
                step *= 2.0;
            }
            if (!found) {
                // For acting notes: also probe visibility during zero-speed periods
                if (has_zero_speed_history) {
                    const auto& track = lines[n.line_id].scroll_px;
                    for (const auto& seg : track.segs) {
                        if (seg.t0 >= t_hit) break;
                        if (std::abs(seg.v0) > 1e-4) continue;
                        // Probe middle + boundaries of zero-speed segment
                        double probes[] = {seg.t0, (seg.t0 + std::min(seg.t1, t_hit)) * 0.5,
                                           std::min(seg.t1, t_hit)};
                        for (double tp : probes) {
                            if (tp >= 0 && visible(n, tp)) {
                                t_vis = tp; found = true; break;
                            }
                        }
                        if (found) break;
                    }
                }
                if (!found) { n.t_enter = t_hit - effective_lookback; continue; }
            }
        }

        // Exponential search backward from visible point
        double hi = t_vis;
        double lo = 0.0;
        bool lo_found = false;
        double step = dt_init;
        for (int i = 0; i < MAX_EXPAND; ++i) {
            double t2 = hi - step;
            if (t2 < t_hit - effective_lookback) break;
            if (visible(n, t2)) {
                hi = t2;
                step *= 2.0;
            } else {
                lo = t2; lo_found = true; break;
            }
        }

        if (!lo_found) { n.t_enter = t_hit - effective_lookback; continue; }

        // Binary search refinement (lo=invisible, hi=visible)
        for (int i = 0; i < 20; ++i) {
            double mid = (lo + hi) * 0.5;
            if (visible(n, mid)) hi = mid; else lo = mid;
        }
        n.t_enter = hi;
    }
}

} // namespace phigros::engine
