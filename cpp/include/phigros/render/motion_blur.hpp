#pragma once
#include "phigros/render/render_target.hpp"
#include "phigros/config/render_config.hpp"
#include <algorithm>

namespace phigros::render {

// Accumulates N sub-frame renders at different time offsets using GPU ADD-blending,
// producing a motion-blur effect without CPU pixel readback.
//
// Usage in main loop:
//   bg_renderer.draw(batch, dim);        // background once (not blurred)
//   mb.begin_accumulate(ren);
//   for (int i = 0; i < mb.samples; ++i) {
//       double t_sub = t - dt * mb.shutter * (1.0 - (double)i / mb.samples);
//       mb.begin_subframe(ren);
//       render_scene_no_bg(t_sub);
//       mb.add_subframe(ren);
//   }
//   mb.composite(ren);                   // draw averaged result onto backbuffer
struct MotionBlurRenderer {
    RenderTarget accum;    // running accumulation of all sub-frames
    RenderTarget scratch;  // single sub-frame target

    bool _enabled = false;
    int samples = 4;
    double shutter = 0.5;

    bool enabled() const { return _enabled; }

    void init(SDL_Renderer* ren, int w, int h, const config::RenderConfig& cfg) {
        if (!cfg.motion_blur_samples.has_value()) return;
#ifdef __EMSCRIPTEN__
        return;  // SDL_TEXTUREACCESS_TARGET not reliably available on WebGL
#endif
        _enabled = true;
        samples = std::max(1, cfg.motion_blur_samples.value_or(4));
        shutter = cfg.motion_blur_shutter.value_or(0.5);
        accum   = RenderTarget::create(ren, w, h);
        scratch = RenderTarget::create(ren, w, h);
    }

    // Clear accumulator — call once per rendered frame.
    void begin_accumulate(SDL_Renderer* ren) {
        accum.clear(ren, 0, 0, 0, 0);  // transparent black
    }

    // Redirect rendering into the scratch texture for one sub-frame.
    void begin_subframe(SDL_Renderer* ren) {
        scratch.clear(ren, 0, 0, 0, 0);  // transparent background
        scratch.bind(ren);
    }

    // ADD-blend scratch into accum with weight 1/samples.
    // Each channel: accum(c) += scratch(c) / samples — after N calls, result = average.
    void add_subframe(SDL_Renderer* ren) {
        accum.bind(ren);
        uint8_t a = static_cast<uint8_t>(std::clamp(255.0 / samples, 1.0, 255.0));
        scratch.blit_to(ren, a, SDL_BLENDMODE_ADD);
    }

    // Draw the final accumulator onto the backbuffer (BLEND so background shows through).
    void composite(SDL_Renderer* ren) {
        RenderTarget::unbind(ren);
        accum.blit_to(ren, 255, SDL_BLENDMODE_BLEND);
    }

    void destroy() {
        accum.destroy();
        scratch.destroy();
        _enabled = false;
    }
};

} // namespace phigros::render
