#pragma once
#include <cmath>
#include <algorithm>
#include <tuple>
#include <utility>

namespace phigros::math {

inline double clamp(double x, double a, double b) {
    return x < a ? a : (x > b ? b : x);
}

inline double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

struct RGB {
    int r = 255, g = 255, b = 255;
};

inline RGB hsv_to_rgb(double h, double s, double v) {
    int i = static_cast<int>(h * 6.0);
    double f = h * 6.0 - i;
    double p = v * (1.0 - s);
    double q = v * (1.0 - f * s);
    double t = v * (1.0 - (1.0 - f) * s);
    i = i % 6;
    double rv, gv, bv;
    switch (i) {
        case 0: rv = v; gv = t; bv = p; break;
        case 1: rv = q; gv = v; bv = p; break;
        case 2: rv = p; gv = v; bv = t; break;
        case 3: rv = p; gv = q; bv = v; break;
        case 4: rv = t; gv = p; bv = v; break;
        default: rv = v; gv = p; bv = q; break;
    }
    return {static_cast<int>(rv * 255), static_cast<int>(gv * 255), static_cast<int>(bv * 255)};
}

inline std::pair<double, double> rotate_vec(double x, double y, double ang) {
    double c = std::cos(ang), s = std::sin(ang);
    return {c * x - s * y, s * x + c * y};
}

} // namespace phigros::math
