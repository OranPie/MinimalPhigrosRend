#pragma once
#include <vector>
#include <string>
#include <cstddef>
#include <cstdio>
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
        seek_with(cursor_, t);
        return eval_at(cursor_, t);
    }

    // Stateless variant: caller owns the cursor. Enables thread-safe evaluation
    // of a shared PiecewiseEased across parallel workers.
    // Pass a size_t initialized to 0 on first call; cache it across frames for
    // amortized O(1) sequential-time evaluation.
    double eval(double t, size_t& cursor) const {
        if (segs.empty()) return default_val;
        seek_with(cursor, t);
        return eval_at(cursor, t);
    }

private:
    mutable size_t cursor_ = 0;

    double eval_at(size_t i, double t) const {
        const auto& s = segs[i];
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

    void seek_with(size_t& cursor, double t) const {
        size_t i = cursor;
        while (i + 1 < segs.size() && t >= segs[i].t1) ++i;
        if (i > 0 && t < segs[i].t0) {
            size_t lo = 0, hi = i - 1;
            while (lo < hi) { size_t mid = lo + (hi - lo + 1) / 2; if (segs[mid].t0 <= t) lo = mid; else hi = mid - 1; }
            i = lo;
        }
        cursor = i;
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
        if (i > 0 && t < segs[i].t0) {
            size_t lo = 0, hi = i - 1;
            while (lo < hi) { size_t mid = lo + (hi - lo + 1) / 2; if (segs[mid].t0 <= t) lo = mid; else hi = mid - 1; }
            i = lo;
        }
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
        if (i > 0 && t < segs[i].t0) {
            size_t lo = 0, hi = i - 1;
            while (lo < hi) { size_t mid = lo + (hi - lo + 1) / 2; if (segs[mid].t0 <= t) lo = mid; else hi = mid - 1; }
            i = lo;
        }
        cursor_ = i;
    }
};

// --- TextSeg / PiecewiseText ---
struct TextSeg {
    double t0, t1;
    std::string s0, s1;
    // RPE font field: empty = default "cmdysj". Stored for completeness; renderer uses loaded font.
    std::string font;
    // RPE easing for %P% interpolation
    int easing_type = 1;
    double easing_L = 0.0, easing_R = 1.0;
};

class PiecewiseText {
public:
    std::vector<TextSeg> segs;
    std::string default_val;

    PiecewiseText() = default;
    PiecewiseText(std::vector<TextSeg> s, std::string def = "")
        : segs(std::move(s)), default_val(std::move(def)) {}

    // Evaluate text at time t. Returns computed string (not reference) to support %P% substitution.
    std::string eval(double t) const {
        if (segs.empty()) return default_val;
        seek(t);
        const auto& s = segs[cursor_];
        if (t <= s.t0) return process_text(s.s0, s, 0.0);
        if (t >= s.t1) return process_text(s.s1, s, 1.0);
        if (s.s0 == s.s1) return process_text(s.s0, s, 0.5);
        double mid = (s.t0 + s.t1) * 0.5;
        double raw_p = (t - s.t0) / (s.t1 - s.t0);
        return process_text(t < mid ? s.s0 : s.s1, s, raw_p);
    }

private:
    mutable size_t cursor_ = 0;
    void seek(double t) const {
        size_t i = cursor_;
        while (i + 1 < segs.size() && t >= segs[i].t1) ++i;
        if (i > 0 && t < segs[i].t0) {
            size_t lo = 0, hi = i - 1;
            while (lo < hi) { size_t mid = lo + (hi - lo + 1) / 2; if (segs[mid].t0 <= t) lo = mid; else hi = mid - 1; }
            i = lo;
        }
        cursor_ = i;
    }

    // Process %P% substitution: replace %P% with easing-interpolated value between s0 and s1 numeric values
    std::string process_text(const std::string& text, const TextSeg& seg, double raw_progress) const {
        if (text.find("%P%") == std::string::npos) return text;

        // Extract numeric values from s0 and s1
        double v0 = parse_number(seg.s0);
        double v1 = parse_number(seg.s1);

        // Apply easing bounds
        double p = raw_progress;
        if (p <= seg.easing_L) p = 0.0;
        else if (p >= seg.easing_R) p = 1.0;
        else p = (p - seg.easing_L) / std::max(1e-9, seg.easing_R - seg.easing_L);
        p = clamp(p, 0.0, 1.0);

        // Apply easing function
        double eased = apply_easing(p, seg.easing_type);
        double value = lerp(v0, v1, eased);

        // Format and substitute
        std::string result = text;
        size_t pos = result.find("%P%");
        if (pos != std::string::npos) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0f", value);
            result.replace(pos, 3, buf);
        }
        return result;
    }

    static double parse_number(const std::string& s) {
        try {
            size_t idx = 0;
            double val = std::stod(s, &idx);
            return val;
        } catch (...) {
            return 0.0;
        }
    }

    static double apply_easing(double t, int type) {
        // Use easing functions from easing.hpp
        if (type < 1 || type > 29) return t;
        return easing_from_type(type)(t);
    }
};

} // namespace phigros::math
