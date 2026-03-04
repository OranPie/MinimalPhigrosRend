#pragma once
#include "phigros/core/types.hpp"
#include "phigros/chart/sampled_track.hpp"
#include <memory>
#include <cstdint>
#include <algorithm>

namespace phigros::chart {

// A chart with all TrackFn evaluations pre-sampled into flat float arrays.
// Created by compile_chart() and round-tripped through write_phbc/read_phbc.
// Call to_chart_data() to get a ChartData fully compatible with the rest of
// the render/engine pipeline (no changes to kinematics or build_frame needed).
struct CompiledChartData {
    double  offset         = 0.0;
    double  chart_end_t    = 0.0;
    int     playable_count = 0;
    float   sample_rate    = 240.0f; // Hz
    double  t_start        = 0.0;   // time of samples[0]
    int     sample_count   = 0;     // length of every track array

    struct CompiledLine {
        int         lid = 0;
        math::RGB   color_rgb;       // static fallback color

        // Sampled track arrays — each has length == sample_count
        std::vector<float> pos_x, pos_y, rot, alpha, scroll;

        // Optional time-varying color (present when source Line::color != nullptr)
        std::vector<float> color_r, color_g, color_b;
    };

    std::vector<CompiledLine> lines;
    std::vector<Note>         notes; // plain data; t_enter already baked

    // Convert to a ChartData whose TrackFns delegate to SampledTrack lambdas.
    // The resulting ChartData::is_compiled == true, so callers skip
    // precompute_t_enter() (notes[*].t_enter is already valid).
    ChartData to_chart_data() const {
        ChartData out;
        out.offset         = offset;
        out.chart_end_t    = chart_end_t;
        out.playable_count = playable_count;
        out.is_compiled    = true;
        out.notes          = notes;

        out.lines.reserve(lines.size());
        for (const auto& cl : lines) {
            // Wrap each track array in a shared SampledTrack; capture by shared_ptr
            // so the lambda survives arbitrary copies of ChartData.
            auto mk = [&](const std::vector<float>& arr) {
                auto st = std::make_shared<SampledTrack>(SampledTrack{t_start, sample_rate, arr});
                return [st](double t) { return static_cast<double>(st->eval(t)); };
            };

            Line ln;
            ln.lid       = cl.lid;
            ln.color_rgb = cl.color_rgb;
            ln.pos_x     = mk(cl.pos_x);
            ln.pos_y     = mk(cl.pos_y);
            ln.rot       = mk(cl.rot);
            ln.alpha     = mk(cl.alpha);
            ln.scroll_fn = mk(cl.scroll);

            // Wire compiled_color if the source line had a dynamic color track
            if (!cl.color_r.empty()) {
                struct ColorSampler {
                    SampledTrack r, g, b;
                    math::RGB eval(double t) const {
                        auto clamp_byte = [](float v) {
                            return static_cast<uint8_t>(std::clamp(static_cast<int>(v), 0, 255));
                        };
                        return {clamp_byte(r.eval(t)), clamp_byte(g.eval(t)), clamp_byte(b.eval(t))};
                    }
                };
                auto cs = std::make_shared<ColorSampler>(ColorSampler{
                    {t_start, sample_rate, cl.color_r},
                    {t_start, sample_rate, cl.color_g},
                    {t_start, sample_rate, cl.color_b}
                });
                ln.compiled_color = [cs](double t) -> math::RGB { return cs->eval(t); };
            }

            out.lines.push_back(std::move(ln));
        }
        return out;
    }
};

} // namespace phigros::chart
