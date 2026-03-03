#pragma once
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace phigros::math {

// 29 easing functions matching Python easing.py exactly
inline double ease_01(double t) { return t; }
inline double ease_02(double t) { return std::sin(M_PI * t / 2.0); }
inline double ease_03(double t) { return 1.0 - std::cos(M_PI * t / 2.0); }
inline double ease_04(double t) { return 1.0 - (1.0 - t) * (1.0 - t); }
inline double ease_05(double t) { return t * t; }
inline double ease_06(double t) { return -(std::cos(M_PI * t) - 1.0) / 2.0; }

inline double ease_07(double t) {
    return t < 0.5 ? 2.0 * t * t : 1.0 - (-2.0 * t + 2.0) * (-2.0 * t + 2.0) / 2.0;
}

inline double ease_08(double t) { double a = 1.0 - t; return 1.0 - a * a * a; }
inline double ease_09(double t) { return t * t * t; }
inline double ease_10(double t) { double a = 1.0 - t; return 1.0 - a * a * a * a; }
inline double ease_11(double t) { return t * t * t * t; }

inline double ease_12(double t) {
    if (t < 0.5) return 4.0 * t * t * t;
    double a = -2.0 * t + 2.0;
    return 1.0 - a * a * a / 2.0;
}

inline double ease_13(double t) {
    if (t < 0.5) return 8.0 * t * t * t * t;
    double a = -2.0 * t + 2.0;
    return 1.0 - a * a * a * a / 2.0;
}

inline double ease_14(double t) { double a = 1.0 - t; return 1.0 - a * a * a * a * a; }
inline double ease_15(double t) { return t * t * t * t * t; }

inline double ease_16(double t) {
    return t == 1.0 ? 1.0 : 1.0 - std::pow(2.0, -10.0 * t);
}

inline double ease_17(double t) {
    return t == 0.0 ? 0.0 : std::pow(2.0, 10.0 * t - 10.0);
}

inline double ease_18(double t) {
    double x = t - 1.0;
    return std::sqrt(1.0 - x * x);
}

inline double ease_19(double t) {
    return 1.0 - std::sqrt(1.0 - t * t);
}

inline double ease_20(double t) {
    double x = t - 1.0;
    return 1.0 + 2.70158 * x * x * x + 1.70158 * x * x;
}

inline double ease_21(double t) {
    return 2.70158 * t * t * t - 1.70158 * t * t;
}

inline double ease_22(double t) {
    if (t < 0.5) return (1.0 - std::sqrt(1.0 - (2.0 * t) * (2.0 * t))) / 2.0;
    double a = -2.0 * t + 2.0;
    return (std::sqrt(1.0 - a * a) + 1.0) / 2.0;
}

inline double ease_23(double t) {
    constexpr double s = 2.5949095;
    if (t < 0.5) {
        double x = 2.0 * t;
        return (x * x * ((s + 1.0) * x - s)) / 2.0;
    }
    double x = 2.0 * t - 2.0;
    return (x * x * ((s + 1.0) * x + s) + 2.0) / 2.0;
}

inline double ease_24(double t) {
    if (t == 0.0) return 0.0;
    if (t == 1.0) return 1.0;
    return std::pow(2.0, -10.0 * t) * std::sin((t * 10.0 - 0.75) * (2.0 * M_PI / 3.0)) + 1.0;
}

inline double ease_25(double t) {
    if (t == 0.0) return 0.0;
    if (t == 1.0) return 1.0;
    return -std::pow(2.0, 10.0 * t - 10.0) * std::sin((t * 10.0 - 10.75) * (2.0 * M_PI / 3.0));
}

inline double ease_26(double t) {
    if (t < 1.0 / 2.75) return 7.5625 * t * t;
    if (t < 2.0 / 2.75) { double x = t - 1.5 / 2.75; return 7.5625 * x * x + 0.75; }
    if (t < 2.5 / 2.75) { double x = t - 2.25 / 2.75; return 7.5625 * x * x + 0.9375; }
    double x = t - 2.625 / 2.75;
    return 7.5625 * x * x + 0.984375;
}

inline double ease_27(double t) { return 1.0 - ease_26(1.0 - t); }

inline double ease_28(double t) {
    return t < 0.5
        ? (1.0 - ease_26(1.0 - 2.0 * t)) / 2.0
        : (1.0 + ease_26(2.0 * t - 1.0)) / 2.0;
}

inline double ease_29(double t) {
    if (t == 0.0) return 0.0;
    if (t == 1.0) return 1.0;
    constexpr double k = (2.0 * M_PI) / 4.5;
    if (t < 0.5)
        return -(std::pow(2.0, 20.0 * t - 10.0) * std::sin((20.0 * t - 11.125) * k)) / 2.0;
    return (std::pow(2.0, -20.0 * t + 10.0) * std::sin((20.0 * t - 11.125) * k)) / 2.0 + 1.0;
}

// Function pointer type for easing
using EasingFn = double(*)(double);

// Lookup table: easing type index → function pointer
inline EasingFn easing_from_type(int tp) {
    static const EasingFn table[] = {
        ease_01, ease_01, ease_02, ease_03, ease_04, ease_05, ease_06, ease_07,
        ease_08, ease_09, ease_10, ease_11, ease_12, ease_13, ease_14, ease_15,
        ease_16, ease_17, ease_18, ease_19, ease_20, ease_21, ease_22, ease_23,
        ease_24, ease_25, ease_26, ease_27, ease_28, ease_29,
    };
    if (tp < 0 || tp >= 30) return ease_01;
    return table[tp];
}

// Cubic bezier solver: find y given x by binary search on parameter u
inline double cubic_bezier_y_for_x(double x1, double y1, double x2, double y2,
                                    double x, int iters = 18) {
    auto bx = [&](double u) {
        double a = 1.0 - u;
        return 3.0 * a * a * u * x1 + 3.0 * a * u * u * x2 + u * u * u;
    };
    auto by = [&](double u) {
        double a = 1.0 - u;
        return 3.0 * a * a * u * y1 + 3.0 * a * u * u * y2 + u * u * u;
    };
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < iters; ++i) {
        double mid = (lo + hi) * 0.5;
        if (bx(mid) < x) lo = mid; else hi = mid;
    }
    return by((lo + hi) * 0.5);
}

} // namespace phigros::math
