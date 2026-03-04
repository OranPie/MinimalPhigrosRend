#pragma once
#include "phigros/core/types.hpp"
#include "phigros/math/util.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
//  Phigros mod system
//  Each mod is a named pipeline of ops applied to a ChartData.
//  All op types are plain structs; the Mod struct holds an AnyOp vector.
//
//  Usage:
//    Mod m = load_mod("my.mod.json");   // see mod_loader.hpp
//    mods::apply(chart, m);
//
//  Available ops and their JSON keys:
//    mirror        center, flip_side
//    colorize      mode [constant|gradient|by_kind|by_line],
//                  color, from, to, by_kind{kind:color}, by_line{lid:color}
//    speed         mul
//    opacity       alpha
//    wave          amplitude, frequency, phase
//    shuffle       seed, range
//    note_filter   keep[kinds], remove[kinds]  (1=tap 2=drag 3=hold 4=flick)
//    flip_timing   (no params — reverses note hit order in time)
//    scale         x_mul, y_mul
// ─────────────────────────────────────────────────────────────────────────────

namespace phigros::mods {

// ── Op structs ────────────────────────────────────────────────────────────────

struct MirrorOp {
    double center    = 0.0;   // x-axis mirror point (px, 0 = chart center)
    bool   flip_side = false; // also flip above/below
};

enum class ColorMode { Constant, Gradient, ByKind, ByLine, Hue };

struct ColorizeOp {
    ColorMode mode = ColorMode::Constant;
    // Constant
    math::RGB color{255, 255, 255};
    // Gradient (over time)
    math::RGB from{255, 100, 100};
    math::RGB to{100, 100, 255};
    // ByKind  (note kind → color)
    std::unordered_map<int, math::RGB> by_kind;
    // ByLine  (line_id → color)
    std::unordered_map<int, math::RGB> by_line;
    // Hue: cycle full spectrum over time (saturation, value)
    double hue_s = 1.0;
    double hue_v = 1.0;
};

struct SpeedOp {
    double mul = 1.0; // multiply every note's speed_mul
};

struct OpacityOp {
    double alpha = 1.0; // set note alpha01 (0–1)
};

struct WaveOp {
    double amplitude = 100.0; // px — x offset amplitude
    double frequency = 1.0;   // Hz — oscillation frequency over t_hit
    double phase     = 0.0;   // radians — initial phase
};

struct ShuffleOp {
    uint32_t seed  = 42;    // RNG seed for reproducibility
    double   range = 200.0; // max ±x displacement (px)
};

struct NoteFilterOp {
    // If `keep` is non-empty, only those kinds survive.
    // Otherwise `remove` kinds are deleted (marked fake).
    std::vector<int> keep;   // e.g. [1,4] = taps + flicks only
    std::vector<int> remove; // e.g. [3]   = remove holds
};

struct FlipTimingOp {
    // Reverses the order of note hit times while keeping positions fixed.
    // Creates a "mirror in time" of the chart.
};

struct ScaleOp {
    double x_mul = 1.0; // multiply x_local_px
    double y_mul = 1.0; // multiply y_offset_px
};

// Union of all op types
using AnyOp = std::variant<
    MirrorOp, ColorizeOp, SpeedOp, OpacityOp,
    WaveOp, ShuffleOp, NoteFilterOp, FlipTimingOp, ScaleOp
>;

// ── Mod container ─────────────────────────────────────────────────────────────

struct Mod {
    std::string          name;
    std::string          description;
    std::vector<AnyOp>   ops;
};

// ── apply() implementations ───────────────────────────────────────────────────

inline void apply(ChartData& chart, const MirrorOp& op) {
    for (auto& n : chart.notes) {
        if (n.fake) continue;
        n.x_local_px = 2.0 * op.center - n.x_local_px;
        if (op.flip_side) n.above = !n.above;
    }
}

inline void apply(ChartData& chart, const ColorizeOp& op) {
    if (chart.notes.empty()) return;
    double min_t = chart.notes.front().t_hit;
    double max_t = chart.notes.back().t_hit;
    double range = max_t - min_t;

    for (auto& n : chart.notes) {
        if (n.fake) continue;
        switch (op.mode) {
        case ColorMode::Constant:
            n.tint_rgb = op.color;
            break;
        case ColorMode::Gradient: {
            double p = range > 1e-9 ? (n.t_hit - min_t) / range : 0.0;
            n.tint_rgb = {
                static_cast<int>(math::lerp(op.from.r, op.to.r, p)),
                static_cast<int>(math::lerp(op.from.g, op.to.g, p)),
                static_cast<int>(math::lerp(op.from.b, op.to.b, p))
            };
            break;
        }
        case ColorMode::ByKind: {
            auto it = op.by_kind.find(n.kind);
            if (it != op.by_kind.end()) n.tint_rgb = it->second;
            break;
        }
        case ColorMode::ByLine: {
            auto it = op.by_line.find(n.line_id);
            if (it != op.by_line.end()) n.tint_rgb = it->second;
            break;
        }
        case ColorMode::Hue: {
            double p = range > 1e-9 ? (n.t_hit - min_t) / range : 0.0;
            n.tint_rgb = math::hsv_to_rgb(p, op.hue_s, op.hue_v);
            break;
        }
        }
    }
}

inline void apply(ChartData& chart, const SpeedOp& op) {
    for (auto& n : chart.notes)
        n.speed_mul *= op.mul;
}

inline void apply(ChartData& chart, const OpacityOp& op) {
    for (auto& n : chart.notes)
        if (!n.fake) n.alpha01 = math::clamp(op.alpha, 0.0, 1.0);
}

inline void apply(ChartData& chart, const WaveOp& op) {
    for (auto& n : chart.notes) {
        if (n.fake) continue;
        n.x_local_px += op.amplitude *
            std::sin(2.0 * M_PI * op.frequency * n.t_hit + op.phase);
    }
}

inline void apply(ChartData& chart, const ShuffleOp& op) {
    // LCG RNG — reproducible, no std::mt19937 overhead
    uint32_t rng = op.seed;
    auto next_f = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<double>(rng) / 4294967296.0;
    };
    for (auto& n : chart.notes) {
        if (n.fake) continue;
        n.x_local_px += (next_f() * 2.0 - 1.0) * op.range;
    }
}

inline void apply(ChartData& chart, const NoteFilterOp& op) {
    auto keep_set  = op.keep;
    auto remov_set = op.remove;
    for (auto& n : chart.notes) {
        if (n.fake) continue;
        bool remove = false;
        if (!keep_set.empty()) {
            remove = std::find(keep_set.begin(), keep_set.end(), n.kind) == keep_set.end();
        } else if (!remov_set.empty()) {
            remove = std::find(remov_set.begin(), remov_set.end(), n.kind) != remov_set.end();
        }
        if (remove) { n.fake = true; n.alpha01 = 0.0; }
    }
    chart.finalize(); // recount playable_count
}

inline void apply(ChartData& chart, const FlipTimingOp&) {
    // Collect hit times of non-fake notes, reversed
    std::vector<double> times;
    times.reserve(chart.notes.size());
    for (auto& n : chart.notes)
        if (!n.fake) times.push_back(n.t_hit);

    std::vector<double> rev(times.rbegin(), times.rend());
    size_t idx = 0;
    for (auto& n : chart.notes)
        if (!n.fake) n.t_hit = rev[idx++];
}

inline void apply(ChartData& chart, const ScaleOp& op) {
    for (auto& n : chart.notes) {
        if (n.fake) continue;
        n.x_local_px *= op.x_mul;
        n.y_offset_px *= op.y_mul;
    }
}

// ── Pipeline dispatch ─────────────────────────────────────────────────────────

inline void apply(ChartData& chart, const AnyOp& op) {
    std::visit([&](const auto& o) { apply(chart, o); }, op);
}

inline void apply(ChartData& chart, const Mod& mod) {
    for (const auto& op : mod.ops)
        apply(chart, op);
}

} // namespace phigros::mods
