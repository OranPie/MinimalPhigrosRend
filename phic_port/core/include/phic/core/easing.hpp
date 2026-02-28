#pragma once

#include <algorithm>
#include <cmath>

// RPE easing functions (types 0–29) + cubic-bezier solver.
// All match Python's phic_renderer/math/easing.py easing_from_type() exactly.

namespace phic {

// Evaluate RPE easing type `tp` at normalized time `t` ∈ [0,1].
// Types 0 and 1 are both linear. Unknown types fall back to linear.
inline double rpe_ease(int tp, double t) noexcept {
    constexpr double kPi = 3.14159265358979323846;
    switch (tp) {
        case 2:  return std::sin(kPi * t / 2.0);
        case 3:  return 1.0 - std::cos(kPi * t / 2.0);
        case 4:  return 1.0 - (1.0 - t) * (1.0 - t);
        case 5:  return t * t;
        case 6:  return -(std::cos(kPi * t) - 1.0) / 2.0;
        case 7: {
            if (t < 0.5) return 2.0 * t * t;
            const double u = -2.0 * t + 2.0;
            return 1.0 - (u * u) / 2.0;
        }
        case 8:  { const double u = 1.0 - t; return 1.0 - u * u * u; }
        case 9:  return t * t * t;
        case 10: { const double u = 1.0 - t; return 1.0 - u * u * u * u; }
        case 11: return t * t * t * t;
        case 12: {
            if (t < 0.5) return 4.0 * t * t * t;
            const double u = -2.0 * t + 2.0;
            return 1.0 - (u * u * u) / 2.0;
        }
        case 13: {
            if (t < 0.5) return 8.0 * t * t * t * t;
            const double u = -2.0 * t + 2.0;
            return 1.0 - (u * u * u * u) / 2.0;
        }
        case 14: { const double u = 1.0 - t; return 1.0 - u * u * u * u * u; }
        case 15: return t * t * t * t * t;
        case 16: return (t == 1.0) ? 1.0 : 1.0 - std::pow(2.0, -10.0 * t);
        case 17: return (t == 0.0) ? 0.0 : std::pow(2.0, 10.0 * t - 10.0);
        case 18: return std::sqrt(std::max(0.0, 1.0 - (t - 1.0) * (t - 1.0)));
        case 19: return 1.0 - std::sqrt(std::max(0.0, 1.0 - t * t));
        case 20: { const double x = t - 1.0; return 1.0 + 2.70158 * x * x * x + 1.70158 * x * x; }
        case 21: return 2.70158 * t * t * t - 1.70158 * t * t;
        case 22: {
            if (t < 0.5) return (1.0 - std::sqrt(std::max(0.0, 1.0 - 4.0 * t * t))) / 2.0;
            const double u = -2.0 * t + 2.0;
            return (std::sqrt(std::max(0.0, 1.0 - u * u)) + 1.0) / 2.0;
        }
        case 23: {
            constexpr double s = 2.5949095;
            if (t < 0.5) { const double x = 2.0 * t; return x * x * ((s + 1.0) * x - s) / 2.0; }
            const double x = 2.0 * t - 2.0;
            return (x * x * ((s + 1.0) * x + s) + 2.0) / 2.0;
        }
        case 24: {
            if (t == 0.0) return 0.0;
            if (t == 1.0) return 1.0;
            return std::pow(2.0, -10.0 * t) * std::sin((t * 10.0 - 0.75) * (2.0 * kPi / 3.0)) + 1.0;
        }
        case 25: {
            if (t == 0.0) return 0.0;
            if (t == 1.0) return 1.0;
            return -std::pow(2.0, 10.0 * t - 10.0) * std::sin((t * 10.0 - 10.75) * (2.0 * kPi / 3.0));
        }
        case 26: {
            if (t < 1.0 / 2.75)  return 7.5625 * t * t;
            if (t < 2.0 / 2.75)  { const double x = t - 1.5  / 2.75; return 7.5625 * x * x + 0.75; }
            if (t < 2.5 / 2.75)  { const double x = t - 2.25 / 2.75; return 7.5625 * x * x + 0.9375; }
            const double x = t - 2.625 / 2.75; return 7.5625 * x * x + 0.984375;
        }
        case 27: {  // bounce_in = 1 - bounce_out(1-t)
            const double s = 1.0 - t;
            if (s < 1.0 / 2.75)  return 1.0 - 7.5625 * s * s;
            if (s < 2.0 / 2.75)  { const double x = s - 1.5  / 2.75; return 1.0 - (7.5625 * x * x + 0.75); }
            if (s < 2.5 / 2.75)  { const double x = s - 2.25 / 2.75; return 1.0 - (7.5625 * x * x + 0.9375); }
            const double x = s - 2.625 / 2.75; return 1.0 - (7.5625 * x * x + 0.984375);
        }
        case 28: {  // bounce_in_out
            auto bout = [](double v) -> double {
                if (v < 1.0 / 2.75)  return 7.5625 * v * v;
                if (v < 2.0 / 2.75)  { const double x = v - 1.5  / 2.75; return 7.5625 * x * x + 0.75; }
                if (v < 2.5 / 2.75)  { const double x = v - 2.25 / 2.75; return 7.5625 * x * x + 0.9375; }
                const double x = v - 2.625 / 2.75; return 7.5625 * x * x + 0.984375;
            };
            if (t < 0.5) return (1.0 - bout(1.0 - 2.0 * t)) / 2.0;
            return (1.0 + bout(2.0 * t - 1.0)) / 2.0;
        }
        case 29: {
            if (t == 0.0) return 0.0;
            if (t == 1.0) return 1.0;
            constexpr double k = 2.0 * kPi / 4.5;
            if (t < 0.5) return -(std::pow(2.0, 20.0 * t - 10.0) * std::sin((20.0 * t - 11.125) * k)) / 2.0;
            return std::pow(2.0, -20.0 * t + 10.0) * std::sin((20.0 * t - 11.125) * k) / 2.0 + 1.0;
        }
        default: return t;  // linear (cases 0, 1, unknown)
    }
}

// Cubic-bezier solver: find u s.t. Bx(u)=x, return By(u).
// Control points: (0,0), (x1,y1), (x2,y2), (1,1). 18 iters ≈ 1e-6 accuracy.
// Matches Python's cubic_bezier_y_for_x() exactly.
inline double cubic_bezier_y_for_x(double x1, double y1, double x2, double y2, double x) noexcept {
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 18; ++i) {
        const double mid = (lo + hi) * 0.5;
        const double a   = 1.0 - mid;
        const double bx  = 3.0 * a * a * mid * x1 + 3.0 * a * mid * mid * x2 + mid * mid * mid;
        if (bx < x) lo = mid; else hi = mid;
    }
    const double u = (lo + hi) * 0.5;
    const double a = 1.0 - u;
    return 3.0 * a * a * u * y1 + 3.0 * a * u * u * y2 + u * u * u;
}

}  // namespace phic
