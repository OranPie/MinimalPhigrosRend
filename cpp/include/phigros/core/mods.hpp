#pragma once
#include "phigros/core/types.hpp"
#include "phigros/math/util.hpp"
#include <unordered_map>

namespace phigros::mods {

// Mirror: flip note x positions around center
inline void apply_mirror(ChartData& chart, double center = 0.0,
                         bool flip_side = false) {
    for (auto& n : chart.notes) {
        if (n.fake) continue;
        n.x_local_px = 2.0 * center - n.x_local_px;
        if (flip_side) n.above = !n.above;
    }
}

enum class ColorMode { Constant, Gradient, ByKind, ByLine };

// Colorize: tint notes by various strategies
inline void apply_colorize(
    ChartData& chart, ColorMode mode,
    math::RGB constant_color = {255, 255, 255},
    math::RGB gradient_start = {255, 255, 255},
    math::RGB gradient_end   = {255, 255, 255},
    const std::unordered_map<int, math::RGB>* by_kind = nullptr,
    const std::unordered_map<int, math::RGB>* by_line = nullptr)
{
    if (chart.notes.empty()) return;

    double min_t = chart.notes.front().t_hit;
    double max_t = chart.notes.back().t_hit;
    double range = max_t - min_t;

    for (auto& n : chart.notes) {
        if (n.fake) continue;

        switch (mode) {
        case ColorMode::Constant:
            n.tint_rgb = constant_color;
            break;
        case ColorMode::Gradient: {
            double p = range > 1e-9 ? (n.t_hit - min_t) / range : 0.0;
            n.tint_rgb = {
                static_cast<int>(math::lerp(gradient_start.r, gradient_end.r, p)),
                static_cast<int>(math::lerp(gradient_start.g, gradient_end.g, p)),
                static_cast<int>(math::lerp(gradient_start.b, gradient_end.b, p))
            };
            break;
        }
        case ColorMode::ByKind:
            if (by_kind) {
                auto it = by_kind->find(n.kind);
                if (it != by_kind->end()) n.tint_rgb = it->second;
            }
            break;
        case ColorMode::ByLine:
            if (by_line) {
                auto it = by_line->find(n.line_id);
                if (it != by_line->end()) n.tint_rgb = it->second;
            }
            break;
        }
    }
}

} // namespace phigros::mods
