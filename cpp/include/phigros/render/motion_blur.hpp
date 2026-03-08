#pragma once
#include "phigros/render/render_target.hpp"
#include "phigros/config/render_config.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <iostream>

namespace phigros::render {

// Accumulates N sub-frame renders at different time offsets using GPU ADD-blending,
// producing a motion-blur effect without CPU pixel readback.
//
// Enhanced features:
//   - Gaussian-weighted sampling: center samples contribute more than edges
//   - Configurable weight curve: "uniform" (flat) or "gaussian" (bell curve)
//
// Usage in main loop:
//   bg_renderer.draw(batch, dim);        // background once (not blurred)
//   mb.begin_accumulate(ren);
//   for (int i = 0; i < mb.samples; ++i) {
//       double t_sub = mb.sample_time(t, dt, i);
//       uint8_t w = mb.sample_weight(i);
//       mb.begin_subframe(ren);
//       render_scene_no_bg(t_sub);
//       mb.add_subframe(ren, w);
//   }
//   mb.composite(ren);                   // draw averaged result onto backbuffer
struct MotionBlurRenderer {
    RenderTarget accum;
    RenderTarget scratch;

    bool _enabled = false;
    int samples = 4;
    double shutter = 0.5;
    bool use_gaussian = false;

    // Precomputed per-sample weights (uint8, sum ~255)
    std::vector<uint8_t> weights;

    bool enabled() const { return _enabled; }

    void init(SDL_Renderer* ren, int w, int h, const config::RenderConfig& cfg) {
        if (!cfg.motion_blur_samples.has_value()) return;
#ifdef __EMSCRIPTEN__
        return;
#endif
        _enabled = true;
        samples = std::max(1, cfg.motion_blur_samples.value_or(4));
        shutter = cfg.motion_blur_shutter.value_or(0.5);

        const std::string& curve = cfg.motion_blur_curve.value_or(std::string("uniform"));
        use_gaussian = (curve == "gaussian");

        accum   = RenderTarget::create(ren, w, h);
        scratch = RenderTarget::create(ren, w, h);

        // Check if render targets were created successfully (requires hardware-accelerated backend)
        if (!accum.valid() || !scratch.valid()) {
            std::cerr << "[MotionBlur] Failed to create render targets — requires hardware-accelerated backend (sdl_hw).\n"
                      << "             Motion blur disabled. Use --backend sdl_hw or set \"backend\":\"sdl_hw\" in config.\n";
            _enabled = false;
            accum.destroy();
            scratch.destroy();
            return;
        }

        precompute_weights();
    }

    void precompute_weights() {
        weights.resize(samples);
        if (!use_gaussian || samples <= 1) {
            // Uniform: equal weight per sample
            uint8_t w = static_cast<uint8_t>(std::clamp(255.0 / samples, 1.0, 255.0));
            for (int i = 0; i < samples; ++i) weights[i] = w;
            return;
        }

        // Gaussian bell curve centered on the middle sample
        // sigma = samples/4 gives ~95% of weight in the window
        double sigma = samples / 4.0;
        double center = (samples - 1) / 2.0;
        std::vector<double> raw(samples);
        double sum = 0.0;
        for (int i = 0; i < samples; ++i) {
            double x = (i - center) / sigma;
            raw[i] = std::exp(-0.5 * x * x);
            sum += raw[i];
        }

        // Normalize so weights sum to ~255
        for (int i = 0; i < samples; ++i) {
            double w = (raw[i] / sum) * 255.0;
            weights[i] = static_cast<uint8_t>(std::clamp(w, 1.0, 255.0));
        }
    }

    // Compute sample time for sub-frame i.
    double sample_time(double t_base, double dt_chart, int i) const {
        double frac = (samples <= 1) ? 0.0 : (static_cast<double>(i) / (samples - 1));
        return t_base - shutter * dt_chart * (1.0 - frac);
    }

    // Get precomputed weight for sample i (replaces old 255/samples).
    uint8_t sample_weight(int i) const {
        if (i < 0 || i >= static_cast<int>(weights.size())) return 1;
        return weights[i];
    }

    void begin_accumulate(SDL_Renderer* ren) {
        accum.clear(ren, 0, 0, 0, 0);
    }

    void begin_subframe(SDL_Renderer* ren) {
        scratch.clear(ren, 0, 0, 0, 0);
        scratch.bind(ren);
    }

    // ADD-blend scratch into accum with per-sample weight.
    void add_subframe(SDL_Renderer* ren, uint8_t weight) {
        accum.bind(ren);
        scratch.blit_to(ren, weight, SDL_BLENDMODE_ADD);
    }

    // Legacy API (uniform weight)
    void add_subframe(SDL_Renderer* ren) {
        uint8_t a = static_cast<uint8_t>(std::clamp(255.0 / samples, 1.0, 255.0));
        add_subframe(ren, a);
    }

    void composite(SDL_Renderer* ren) {
        RenderTarget::unbind(ren);
        accum.blit_to(ren, 255, SDL_BLENDMODE_BLEND);
    }

    void destroy() {
        accum.destroy();
        scratch.destroy();
        weights.clear();
        _enabled = false;
    }
};

} // namespace phigros::render
