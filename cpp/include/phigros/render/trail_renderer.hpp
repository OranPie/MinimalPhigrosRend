#pragma once
#include "phigros/render/render_target.hpp"
#include "phigros/config/render_config.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace phigros::render {

// Renders the scene into a circular ring-buffer of RenderTargets and
// composites them back-to-front with per-age alpha decay.
// Only the foreground (notes/lines) is trailed; background is drawn separately.
//
// Usage in main loop:
//   bg_renderer.draw(...);                    // background once
//   if (trail.enabled()) {
//       trail.begin_frame(ren);               // redirect to ring slot
//       render_scene_no_bg(t);
//       RenderTarget::unbind(ren);
//       trail.composite(ren);                 // echo frames onto backbuffer
//   } else {
//       render_scene_no_bg(t);
//   }
struct TrailRenderer {
    static constexpr int MAX_SLOTS = 16;
    RenderTarget slots[MAX_SLOTS];

    int n_slots = 6;
    int head = 0;          // ring-buffer write pointer (current frame)
    int frame_count = 0;   // total frames rendered (for available-slot count)

    bool _enabled = false;
    double alpha = 0.6;    // opacity of the current frame (age=0)
    double decay = 0.85;   // alpha multiplier per older frame
    int dim = 0;           // colour-mod darkening per older frame (0=none, 255=black)
    SDL_BlendMode blend_mode = SDL_BLENDMODE_BLEND;

    bool enabled() const { return _enabled; }

    void init(SDL_Renderer* ren, int w, int h, const config::RenderConfig& cfg) {
        if (!cfg.trail_alpha.has_value()) return;
#ifdef __EMSCRIPTEN__
        return;  // SDL_TEXTUREACCESS_TARGET not reliably available on WebGL
#endif
        _enabled = true;
        alpha   = cfg.trail_alpha.value_or(0.6);
        decay   = cfg.trail_decay.value_or(0.85);
        n_slots = std::clamp(cfg.trail_frames.value_or(6), 2, MAX_SLOTS);
        dim     = cfg.trail_dim.value_or(0);

        const std::string& b = cfg.trail_blend.value_or(std::string("normal"));
        blend_mode = (b == "add") ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND;

        for (int i = 0; i < n_slots; ++i)
            slots[i] = RenderTarget::create(ren, w, h);
    }

    // Call before scene rendering — redirects draw calls into the current ring slot.
    void begin_frame(SDL_Renderer* ren) {
        head = frame_count % n_slots;
        slots[head].clear(ren, 0, 0, 0, 0);  // transparent background
        slots[head].bind(ren);
    }

    // Call after scene rendering (render target must be set back to NULL before calling).
    // Composites all available ring slots from oldest to newest onto the backbuffer.
    void composite(SDL_Renderer* ren) {
        int available = std::min(frame_count + 1, n_slots);
        for (int age = available - 1; age >= 0; --age) {
            int slot_idx = ((head - age) % n_slots + n_slots) % n_slots;
            double slot_alpha = alpha * std::pow(decay, static_cast<double>(age));
            uint8_t a = static_cast<uint8_t>(std::clamp(slot_alpha * 255.0, 0.0, 255.0));
            if (a == 0) continue;

            // dim: darken older frames via colour modulation
            if (dim > 0 && age > 0) {
                double dim_factor = std::pow(1.0 - dim / 255.0, static_cast<double>(age));
                uint8_t cm = static_cast<uint8_t>(std::clamp(dim_factor * 255.0, 0.0, 255.0));
                SDL_SetTextureColorMod(slots[slot_idx].tex, cm, cm, cm);
            } else {
                SDL_SetTextureColorMod(slots[slot_idx].tex, 255, 255, 255);
            }
            slots[slot_idx].blit_to(ren, a, blend_mode);
        }
        ++frame_count;
    }

    void destroy() {
        for (int i = 0; i < MAX_SLOTS; ++i) slots[i].destroy();
        _enabled = false;
    }
};

} // namespace phigros::render
