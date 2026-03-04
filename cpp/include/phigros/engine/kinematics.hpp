#pragma once
#include "phigros/core/types.hpp"
#include "phigros/math/util.hpp"
#include <cmath>
#include <optional>
#include <unordered_map>

namespace phigros::engine {

struct LineState {
    double x, y, rot, alpha01, scroll, alpha_raw;
    double cos_rot = 0.0, sin_rot = 0.0;  // precomputed from rot
};

inline LineState eval_line_state(
    const Line& line, double t,
    std::optional<double> force_alpha = std::nullopt,
    const std::unordered_map<int, double>* force_alpha_by_lid = nullptr)
{
    double x = line.pos_x(t);
    double y = line.pos_y(t);
    double rot = line.rot(t);
    double a_raw = line.alpha(t);
    double s = line.scroll_px.integral(t);
    double a01 = math::clamp(std::abs(a_raw), 0.0, 1.0);

    if (force_alpha_by_lid) {
        auto it = force_alpha_by_lid->find(line.lid);
        if (it != force_alpha_by_lid->end()) {
            double f = math::clamp(it->second, 0.0, 1.0);
            a01 = f;
            a_raw = f;
        }
    }
    if (force_alpha.has_value()) {
        double f = math::clamp(*force_alpha, 0.0, 1.0);
        a01 = f;
        a_raw = f;
    }
    return {x, y, rot, a01, s, a_raw, std::cos(rot), std::sin(rot)};
}

struct Vec2 { double x, y; };

inline Vec2 note_world_pos(
    double line_x, double line_y, double rot,
    double scroll_now, const Note& note,
    double scroll_target, bool for_tail = false,
    double flow_speed_mul = 1.0,
    bool speed_mul_affects_travel = false,
    bool hold_keep_head = false)
{
    double tx = std::cos(rot), ty = std::sin(rot);
    double nx = -std::sin(rot), ny = std::cos(rot);
    double sgn = note.above ? 1.0 : -1.0;
    double x_local = note.x_local_px;

    double dy = (scroll_target - scroll_now) * flow_speed_mul;

    // holdKeepHead: head doesn't pass through line
    if (hold_keep_head && note.kind == 3 && !for_tail) {
        if (dy < 0.0) dy = 0.0;
    }

    double mult = 1.0;
    if (for_tail && note.kind == 3)
        mult = std::max(0.0, note.speed_mul);
    else if (!for_tail && note.kind != 3 && speed_mul_affects_travel)
        mult = std::max(0.0, note.speed_mul);

    double y_local = sgn * dy * mult + note.y_offset_px;
    return {line_x + tx * x_local + nx * y_local,
            line_y + ty * x_local + ny * y_local};
}

// Fast variant: takes precomputed cos_r / sin_r (from LineState) instead of rot angle.
// Eliminates two trig calls when rendering multiple notes on the same line.
inline Vec2 note_world_pos_cs(
    double line_x, double line_y, double cos_r, double sin_r,
    double scroll_now, const Note& note,
    double scroll_target, bool for_tail = false,
    double flow_speed_mul = 1.0,
    bool speed_mul_affects_travel = false,
    bool hold_keep_head = false)
{
    double tx = cos_r, ty = sin_r;
    double nx = -sin_r, ny = cos_r;
    double sgn = note.above ? 1.0 : -1.0;
    double x_local = note.x_local_px;

    double dy = (scroll_target - scroll_now) * flow_speed_mul;

    if (hold_keep_head && note.kind == 3 && !for_tail) {
        if (dy < 0.0) dy = 0.0;
    }

    double mult = 1.0;
    if (for_tail && note.kind == 3)
        mult = std::max(0.0, note.speed_mul);
    else if (!for_tail && note.kind != 3 && speed_mul_affects_travel)
        mult = std::max(0.0, note.speed_mul);

    double y_local = sgn * dy * mult + note.y_offset_px;
    return {line_x + tx * x_local + nx * y_local,
            line_y + ty * x_local + ny * y_local};
}

} // namespace phigros::engine
