#include "phigros/math/easing.hpp"
#include "phigros/math/tracks.hpp"
#include "phigros/math/util.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace phigros::math;

static int failures = 0;

static void check(const char* name, double got, double expected, double tol = 1e-9) {
    double diff = std::abs(got - expected);
    if (diff > tol) {
        std::printf("FAIL %s: got=%.12f expected=%.12f diff=%.2e\n", name, got, expected, diff);
        ++failures;
    }
}

int main() {
    // Boundary checks for all easings
    for (int i = 1; i <= 29; ++i) {
        auto fn = easing_from_type(i);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "ease_%02d(0)", i);
        check(buf, fn(0.0), (i == 20 || i == 21) ? 0.0 : 0.0, 0.02);
        std::snprintf(buf, sizeof(buf), "ease_%02d(1)", i);
        check(buf, fn(1.0), 1.0, 1e-9);
    }

    // Specific value checks (verified against Python)
    check("ease_01(0.5)", ease_01(0.5), 0.5);
    check("ease_02(0.5)", ease_02(0.5), std::sin(M_PI * 0.25));
    check("ease_05(0.5)", ease_05(0.5), 0.25);
    check("ease_08(0.5)", ease_08(0.5), 1.0 - 0.125);
    check("ease_09(0.5)", ease_09(0.5), 0.125);
    check("ease_16(0.5)", ease_16(0.5), 1.0 - std::pow(2.0, -5.0));
    check("ease_20(0)", ease_20(0.0), 0.0, 1e-6);
    check("ease_21(0)", ease_21(0.0), 0.0, 1e-6);
    check("ease_20(1)", ease_20(1.0), 1.0, 1e-6);
    check("ease_21(1)", ease_21(1.0), 1.0, 1e-6);

    // PiecewiseEased test
    {
        std::vector<EasedSeg> segs;
        segs.push_back({0.0, 1.0, 0.0, 100.0, 0}); // linear
        segs.push_back({1.0, 2.0, 100.0, 200.0, 5}); // quad in
        PiecewiseEased track(std::move(segs), -1.0);

        check("track(-1)", track.eval(-1.0), 0.0);      // before first → v0
        check("track(0.5)", track.eval(0.5), 50.0);      // linear midpoint
        check("track(1.0)", track.eval(1.0), 100.0);     // boundary
        check("track(1.5)", track.eval(1.5), lerp(100.0, 200.0, ease_05(0.5)));
        check("track(3.0)", track.eval(3.0), 200.0);     // after last → v1
    }

    // IntegralTrack test
    {
        std::vector<Seg1D> segs;
        segs.push_back({0.0, 1.0, 10.0, 10.0, 0.0});  // constant 10
        segs.push_back({1.0, 2.0, 20.0, 20.0, 10.0});  // constant 20
        IntegralTrack track(std::move(segs));

        check("integral(0)", track.integral(0.0), 0.0);
        check("integral(0.5)", track.integral(0.5), 5.0);
        check("integral(1.0)", track.integral(1.0), 10.0);
        check("integral(1.5)", track.integral(1.5), 10.0 + 10.0);
        check("integral(2.0)", track.integral(2.0), 10.0 + 20.0);
    }

    // SumTrack test
    {
        PiecewiseEased a({{0.0, 1.0, 10.0, 20.0, 0}}, 0.0);
        PiecewiseEased b({{0.0, 1.0, 5.0, 15.0, 0}}, 0.0);
        SumTrack sum({std::move(a), std::move(b)}, 0.0);
        check("sum(0.5)", sum.eval(0.5), 15.0 + 10.0); // (15 + 10)
    }

    // Bezier easing test
    check("bezier_linear", cubic_bezier_y_for_x(0.0, 0.0, 1.0, 1.0, 0.5), 0.5, 0.01);

    // clamp / lerp / hsv
    check("clamp(-1,0,1)", clamp(-1.0, 0.0, 1.0), 0.0);
    check("clamp(2,0,1)", clamp(2.0, 0.0, 1.0), 1.0);
    check("lerp(0,10,0.3)", lerp(0.0, 10.0, 0.3), 3.0);

    if (failures == 0)
        std::printf("All tests passed.\n");
    else
        std::printf("%d test(s) FAILED.\n", failures);

    return failures > 0 ? 1 : 0;
}
