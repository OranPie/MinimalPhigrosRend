#pragma once
#include <vector>
#include <string>
#include <cstddef>
#include <algorithm>
#include "phigros/math/easing.hpp"
#include "phigros/math/util.hpp"

namespace phigros::math {

// --- EasedSeg: one segment of a piecewise-eased track ---
struct EasedSeg {
    double t0, t1;
    double v0, v1;
    int easing_type = 0;       // 0..29 for standard, -1 for bezier
    double L = 0.0, R = 1.0;   // easing clip window
    // Bezier control points (used only when easing_type == -1)
    double bez_x1 = 0, bez_y1 = 0, bez_x2 = 0, bez_y2 = 0;

    double apply_easing(double p) const {
        if (easing_type == -1)
            return cubic_bezier_y_for_x(bez_x1, bez_y1, bez_x2, bez_y2, p);
        return easing_from_type(easing_type)(p);
    }
};

// --- PiecewiseEased: evaluate a float track with easing ---
class PiecewiseEased {
public:
    std::vector<EasedSeg> segs;
    double default_val;

    explicit PiecewiseEased(double def = 0.0) : default_val(def) {}
    PiecewiseEased(std::vector<EasedSeg> s, double def = 0.0)
        : segs(std::move(s)), default_val(def) {}

    double eval(double t) const {
        if (segs.empty()) return default_val;
        seek(t);
        const auto& s = segs[cursor_];
        if (t <= s.t0) return s.v0;
        if (t >= s.t1) return s.v1;
        double p_raw = (t - s.t0) / (s.t1 - s.t0);
        double p;
        if (p_raw <= s.L) p = 0.0;
        else if (p_raw >= s.R) p = 1.0;
        else p = (p_raw - s.L) / std::max(1e-9, s.R - s.L);
        p = clamp(p, 0.0, 1.0);
        double e = s.apply_easing(p);
        return lerp(s.v0, s.v1, e);
    }

private:
    mutable size_t cursor_ = 0;
    void seek(double t) const {
        size_t i = cursor_;
        while (i + 1 < segs.size() && t >= segs[i].t1) ++i;
        while (i > 0 && t < segs[i].t0) --i;
        cursor_ = i;
    }
};

// --- SumTrack: sum of multiple PiecewiseEased tracks ---
class SumTrack {
public:
    std::vector<PiecewiseEased> tracks;
    double default_val;

    explicit SumTrack(double def = 0.0) : default_val(def) {}
    SumTrack(std::vector<PiecewiseEased> tr, double def = 0.0)
        : tracks(std::move(tr)), default_val(def) {}

    double eval(double t) const {
        if (tracks.empty()) return default_val;
        double sum = 0.0;
        for (const auto& tr : tracks) sum += tr.eval(t);
        return sum;
    }
};

// --- Seg1D: segment for IntegralTrack (linear ramp) ---
struct Seg1D {
    double t0, t1;
    double v0, v1;
    double prefix; // integral from 0 to t0
};

// --- IntegralTrack: computes cumulative integral of piecewise-linear speed ---
class IntegralTrack {
public:
    std::vector<Seg1D> segs;

    IntegralTrack() = default;
    explicit IntegralTrack(std::vector<Seg1D> s) : segs(std::move(s)) {}

    double integral(double t) const {
        if (segs.empty()) return 0.0;
        seek(t);
        const auto& s = segs[cursor_];
        if (t <= s.t0) return s.prefix;
        if (t >= s.t1) {
            double dt = s.t1 - s.t0;
            return s.prefix + 0.5 * (s.v0 + s.v1) * dt;
        }
        double dt = t - s.t0;
        double full = s.t1 - s.t0;
        double u = dt / std::max(1e-9, full);
        double vt = lerp(s.v0, s.v1, u);
        return s.prefix + 0.5 * (s.v0 + vt) * dt;
    }

private:
    mutable size_t cursor_ = 0;
    void seek(double t) const {
        size_t i = cursor_;
        while (i + 1 < segs.size() && t >= segs[i].t1) ++i;
        while (i > 0 && t < segs[i].t0) --i;
        cursor_ = i;
    }
};

// --- ColorSeg: segment for color interpolation ---
struct ColorSeg {
    double t0, t1;
    RGB c0, c1;
    int easing_type = 0;
    double L = 0.0, R = 1.0;
    double bez_x1 = 0, bez_y1 = 0, bez_x2 = 0, bez_y2 = 0;

    double apply_easing(double p) const {
        if (easing_type == -1)
            return cubic_bezier_y_for_x(bez_x1, bez_y1, bez_x2, bez_y2, p);
        return easing_from_type(easing_type)(p);
    }
};

// --- PiecewiseColor: evaluate a color track ---
class PiecewiseColor {
public:
    std::vector<ColorSeg> segs;
    RGB default_val{255, 255, 255};

    PiecewiseColor() = default;
    PiecewiseColor(std::vector<ColorSeg> s, RGB def = {255, 255, 255})
        : segs(std::move(s)), default_val(def) {}

    RGB eval(double t) const {
        if (segs.empty()) return default_val;
        seek(t);
        const auto& s = segs[cursor_];
        if (t <= s.t0) return s.c0;
        if (t >= s.t1) return s.c1;
        double p_raw = (t - s.t0) / (s.t1 - s.t0);
        double p;
        if (p_raw <= s.L) p = 0.0;
        else if (p_raw >= s.R) p = 1.0;
        else p = (p_raw - s.L) / std::max(1e-9, s.R - s.L);
        p = clamp(p, 0.0, 1.0);
        double e = s.apply_easing(p);
        int r = static_cast<int>(lerp(static_cast<double>(s.c0.r), static_cast<double>(s.c1.r), e));
        int g = static_cast<int>(lerp(static_cast<double>(s.c0.g), static_cast<double>(s.c1.g), e));
        int b = static_cast<int>(lerp(static_cast<double>(s.c0.b), static_cast<double>(s.c1.b), e));
        return {static_cast<int>(clamp(r, 0, 255)),
                static_cast<int>(clamp(g, 0, 255)),
                static_cast<int>(clamp(b, 0, 255))};
    }

private:
    mutable size_t cursor_ = 0;
    void seek(double t) const {
        size_t i = cursor_;
        while (i + 1 < segs.size() && t >= segs[i].t1) ++i;
        while (i > 0 && t < segs[i].t0) --i;
        cursor_ = i;
    }
};

// --- TextSeg / PiecewiseText ---
struct TextSeg {
    double t0, t1;
    std::string s0, s1;
};

class PiecewiseText {
public:
    std::vector<TextSeg> segs;
    std::string default_val;

    PiecewiseText() = default;
    PiecewiseText(std::vector<TextSeg> s, std::string def = "")
        : segs(std::move(s)), default_val(std::move(def)) {}

    const std::string& eval(double t) const {
        if (segs.empty()) return default_val;
        seek(t);
        const auto& s = segs[cursor_];
        if (t <= s.t0) return s.s0;
        if (t >= s.t1) return s.s1;
        if (s.s0 == s.s1) return s.s0;
        double mid = (s.t0 + s.t1) * 0.5;
        return t < mid ? s.s0 : s.s1;
    }

private:
    mutable size_t cursor_ = 0;
    void seek(double t) const {
        size_t i = cursor_;
        while (i + 1 < segs.size() && t >= segs[i].t1) ++i;
        while (i > 0 && t < segs[i].t0) --i;
        cursor_ = i;
    }
};

} // namespace phigros::math
