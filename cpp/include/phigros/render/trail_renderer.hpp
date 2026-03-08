#pragma once
#include "phigros/render/render_target.hpp"
#include "phigros/config/render_config.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <iostream>

namespace phigros::render {

// Renders the scene into a circular ring-buffer of RenderTargets and
// composites them back-to-front with per-age alpha decay.
// Only the foreground (notes/lines) is trailed; background is drawn separately.
//
// Enhanced features:
//   - Blur ramp: older frames progressively blurred via downscale-upscale
//   - Gaussian decay: softer falloff curve option (vs exponential)
//   - Chromatic offset: sub-pixel position shift per age for ghosting
//   - Glow pass: additive overlay of trail for neon/bloom effect
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
    // Allow longer temporal trails at high internal sampling rates (e.g. 240fps).
    static constexpr int MAX_SLOTS = 64;
    static constexpr int MAX_BLUR_PASSES = 4;

    RenderTarget slots[MAX_SLOTS];
    // Intermediate targets for blur (half-size chain)
    RenderTarget blur_chain[MAX_BLUR_PASSES];
    int blur_chain_count = 0;

    int n_slots = 6;
    int head = 0;
    int frame_count = 0;

    bool _enabled = false;
    double alpha = 0.6;
    double decay = 0.85;
    int dim = 0;
    SDL_BlendMode blend_mode = SDL_BLENDMODE_BLEND;

    // Enhanced visuals
    bool   blur_ramp = false;
    int    blur_quality = 2;    // downscale passes (1-4)
    double chromatic = 0.0;     // px offset per age
    bool   use_gaussian = false;
    double glow = 0.0;          // additive glow intensity

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

        // Enhanced params
        blur_ramp    = cfg.trail_blur_ramp.value_or(false);
        blur_quality = std::clamp(cfg.trail_blur_quality.value_or(2), 1, MAX_BLUR_PASSES);
        chromatic    = cfg.trail_chromatic.value_or(0.0);
        glow         = std::clamp(cfg.trail_glow.value_or(0.0), 0.0, 1.0);

        const std::string& curve = cfg.trail_decay_curve.value_or(std::string("exponential"));
        use_gaussian = (curve == "gaussian");

        for (int i = 0; i < n_slots; ++i)
            slots[i] = RenderTarget::create(ren, w, h);

        // Check if render targets were created successfully (requires hardware-accelerated backend)
        bool all_valid = true;
        for (int i = 0; i < n_slots; ++i) {
            if (!slots[i].valid()) {
                all_valid = false;
                break;
            }
        }
        if (!all_valid) {
            std::cerr << "[Trail] Failed to create render targets — requires hardware-accelerated backend (sdl_hw).\n"
                      << "        Trail effect disabled. Use --backend sdl_hw or set \"backend\":\"sdl_hw\" in config.\n";
            _enabled = false;
            for (int i = 0; i < n_slots; ++i) slots[i].destroy();
            return;
        }

        // Build blur chain: progressively smaller targets for downscale blur
        if (blur_ramp) {
            int bw = w, bh = h;
            blur_chain_count = 0;
            for (int i = 0; i < blur_quality; ++i) {
                bw = std::max(1, bw / 2);
                bh = std::max(1, bh / 2);
                blur_chain[i] = RenderTarget::create(ren, bw, bh);
                ++blur_chain_count;
            }
        }
    }

    void begin_frame(SDL_Renderer* ren) {
        head = frame_count % n_slots;
        slots[head].clear(ren, 0, 0, 0, 0);
        slots[head].bind(ren);
    }

    // Commit one freshly rendered slot into the ring buffer timeline.
    void submit_frame() {
        ++frame_count;
    }

    // Compute per-frame alpha using selected decay curve.
    double compute_alpha(int age) const {
        if (use_gaussian) {
            // Gaussian: sigma proportional to slot count for smooth falloff
            double sigma = n_slots * 0.4;
            double g = std::exp(-0.5 * (age * age) / (sigma * sigma));
            return alpha * g;
        }
        return alpha * std::pow(decay, static_cast<double>(age));
    }

    // Blit a slot with optional downscale blur (blur intensity scales with age).
    void blit_blurred(SDL_Renderer* ren, int slot_idx, uint8_t a, int age) {
        if (!blur_ramp || age == 0 || blur_chain_count == 0) {
            slots[slot_idx].blit_to(ren, a, blend_mode);
            return;
        }

        // Number of downscale passes proportional to age
        int passes = std::clamp(age, 1, blur_chain_count);

        // Downscale chain: slot → blur_chain[0] → blur_chain[1] → ...
        SDL_Texture* src = slots[slot_idx].tex;
        for (int p = 0; p < passes; ++p) {
            blur_chain[p].bind(ren);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
            SDL_RenderClear(ren);
            SDL_SetTextureAlphaMod(src, 255);
            SDL_SetTextureColorMod(src, 255, 255, 255);
            SDL_SetTextureBlendMode(src, SDL_BLENDMODE_NONE);
            SDL_FRect dst{0.0f, 0.0f,
                static_cast<float>(blur_chain[p].w),
                static_cast<float>(blur_chain[p].h)};
            app::sdl::render_copy(ren, src, nullptr, &dst);
            src = blur_chain[p].tex;
        }

        // Upscale back: blit smallest blur target to current render target (backbuffer)
        RenderTarget::unbind(ren);
        SDL_SetTextureAlphaMod(src, a);
        SDL_SetTextureColorMod(src, 255, 255, 255);
        SDL_SetTextureBlendMode(src, blend_mode);
        SDL_FRect full{0.0f, 0.0f,
            static_cast<float>(slots[0].w),
            static_cast<float>(slots[0].h)};
        app::sdl::render_copy(ren, src, nullptr, &full);
    }

    void composite(SDL_Renderer* ren) {
        int available = std::min(frame_count, n_slots);
        if (available <= 0) return;
        int newest = ((frame_count - 1) % n_slots + n_slots) % n_slots;
        for (int age = available - 1; age >= 0; --age) {
            int slot_idx = ((newest - age) % n_slots + n_slots) % n_slots;
            double slot_alpha = compute_alpha(age);
            uint8_t a = static_cast<uint8_t>(std::clamp(slot_alpha * 255.0, 0.0, 255.0));
            if (a == 0) continue;

            // Dim: darken older frames via colour modulation
            if (dim > 0 && age > 0) {
                double dim_factor = std::pow(1.0 - dim / 255.0, static_cast<double>(age));
                uint8_t cm = static_cast<uint8_t>(std::clamp(dim_factor * 255.0, 0.0, 255.0));
                SDL_SetTextureColorMod(slots[slot_idx].tex, cm, cm, cm);
            } else {
                SDL_SetTextureColorMod(slots[slot_idx].tex, 255, 255, 255);
            }

            // Chromatic offset: shift older frames slightly for ghost effect
            if (chromatic > 0.01 && age > 0) {
                double offset = chromatic * age;
                // Render with slight position shifts in R/G channels
                // SDL doesn't support per-channel offset, so we shift the whole
                // blit rect for a subtle directional ghost
                SDL_FRect shifted{
                    static_cast<float>(offset * 0.5),
                    static_cast<float>(-offset * 0.3),
                    static_cast<float>(slots[slot_idx].w),
                    static_cast<float>(slots[slot_idx].h)};
                SDL_SetTextureAlphaMod(slots[slot_idx].tex, a);
                SDL_SetTextureBlendMode(slots[slot_idx].tex, blend_mode);
                app::sdl::render_copy(ren, slots[slot_idx].tex, nullptr, &shifted);
            } else {
                blit_blurred(ren, slot_idx, a, age);
            }
        }

        // Glow pass: re-composite newest frames with additive blend for bloom
        if (glow > 0.01) {
            int glow_frames = std::min(3, available);
            for (int age = glow_frames - 1; age >= 0; --age) {
                int slot_idx = ((newest - age) % n_slots + n_slots) % n_slots;
                double ga = glow * compute_alpha(age) * 0.5;
                uint8_t ga8 = static_cast<uint8_t>(std::clamp(ga * 255.0, 0.0, 255.0));
                if (ga8 == 0) continue;
                SDL_SetTextureColorMod(slots[slot_idx].tex, 255, 255, 255);
                slots[slot_idx].blit_to(ren, ga8, SDL_BLENDMODE_ADD);
            }
        }
    }

    void destroy() {
        for (int i = 0; i < MAX_SLOTS; ++i) slots[i].destroy();
        for (int i = 0; i < MAX_BLUR_PASSES; ++i) blur_chain[i].destroy();
        blur_chain_count = 0;
        _enabled = false;
    }
};

} // namespace phigros::render
