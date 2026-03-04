#pragma once
#include <vector>
#include <cmath>

namespace phigros::chart {

// Pre-sampled 1-D track stored as a uniform float array.
// eval(t) performs O(1) linear interpolation between adjacent samples.
// Created by compile_chart(); used to replace live easing evaluations in
// compiled ChartData (Line::scroll_fn, Line::compiled_color lambdas, etc.).
struct SampledTrack {
    double t_start      = 0.0;
    float  sample_rate  = 240.0f;
    std::vector<float> samples;  // samples[i] = value at t_start + i/sample_rate

    float eval(double t) const noexcept {
        if (samples.empty()) return 0.0f;
        double fi = (t - t_start) * static_cast<double>(sample_rate);
        int i = static_cast<int>(fi);
        if (i < 0) return samples.front();
        if (i + 1 >= static_cast<int>(samples.size())) return samples.back();
        float frac = static_cast<float>(fi - i);
        return samples[i] + frac * (samples[i + 1] - samples[i]);
    }
};

} // namespace phigros::chart
