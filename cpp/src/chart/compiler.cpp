#include "phigros/chart/compiler.hpp"
#include <cmath>
#include <algorithm>

namespace phigros::chart {

CompiledChartData compile_chart(const ChartData& src, float sample_rate) {
    CompiledChartData out;
    out.offset         = src.offset;
    out.chart_end_t    = src.chart_end_t;
    out.playable_count = src.playable_count;
    out.sample_rate    = sample_rate;
    out.notes          = src.notes; // notes are plain data; t_enter already baked

    // Extend the sample window slightly beyond the chart edges so SampledTrack::eval
    // never hits the out-of-range (clamp-to-edge) path during normal playback.
    const double pre  = 2.0;
    const double post = 2.0;
    out.t_start = src.offset - pre;
    double t_end = src.chart_end_t + post;
    out.sample_count = static_cast<int>(
        std::ceil((t_end - out.t_start) * static_cast<double>(sample_rate))) + 1;

    out.lines.reserve(src.lines.size());
    for (const auto& line : src.lines) {
        CompiledChartData::CompiledLine cl;
        cl.lid       = line.lid;
        cl.color_rgb = line.color_rgb;

        cl.pos_x  .reserve(out.sample_count);
        cl.pos_y  .reserve(out.sample_count);
        cl.rot    .reserve(out.sample_count);
        cl.alpha  .reserve(out.sample_count);
        cl.scroll .reserve(out.sample_count);

        const bool has_color = static_cast<bool>(line.color) ||
                               static_cast<bool>(line.compiled_color);
        if (has_color) {
            cl.color_r.reserve(out.sample_count);
            cl.color_g.reserve(out.sample_count);
            cl.color_b.reserve(out.sample_count);
        }

        const double inv_sr = 1.0 / static_cast<double>(sample_rate);
        for (int i = 0; i < out.sample_count; ++i) {
            double t = out.t_start + i * inv_sr;

            cl.pos_x .push_back(static_cast<float>(line.pos_x (t)));
            cl.pos_y .push_back(static_cast<float>(line.pos_y (t)));
            cl.rot   .push_back(static_cast<float>(line.rot   (t)));
            cl.alpha .push_back(static_cast<float>(line.alpha (t)));

            // Scroll: use scroll_fn override if present, else live integral
            double scroll_val = line.scroll_fn
                ? line.scroll_fn(t)
                : line.scroll_px.integral(t);
            cl.scroll.push_back(static_cast<float>(scroll_val));

            // Color: evaluate whichever source is active
            if (has_color) {
                math::RGB rgb;
                if (line.compiled_color)
                    rgb = line.compiled_color(t);
                else if (line.color)
                    rgb = line.color->eval(t);
                else
                    rgb = line.color_rgb;
                cl.color_r.push_back(static_cast<float>(rgb.r));
                cl.color_g.push_back(static_cast<float>(rgb.g));
                cl.color_b.push_back(static_cast<float>(rgb.b));
            }
        }

        out.lines.push_back(std::move(cl));
    }

    return out;
}

} // namespace phigros::chart
