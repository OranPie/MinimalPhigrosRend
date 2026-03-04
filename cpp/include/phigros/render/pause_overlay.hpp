#pragma once
#include "phigros/render/sprite_batch.hpp"
#include "phigros/render/hud_renderer.hpp"

namespace phigros::render {

// Drawn over the frozen game frame when the player is paused.
struct PauseOverlay {
    void draw(const SpriteBatch& batch, const HudRenderer& hud, int W, int H) const {
        // Semi-transparent dark overlay
        batch.draw_rect(0, 0, W, H, 0, 0, 0, 200);

        if (!hud.has_font) return;

        double cx = W * 0.5, cy = H * 0.5;

        // "PAUSED" centred
        {
            const std::string label = "PAUSED";
            double tw = hud.text_width(hud.font_large, label);
            hud.draw_text(batch, hud.font_large, label,
                          cx - tw * 0.5, cy - 36.0, 255, 255, 255, 230);
        }

        // Key hint line
        {
            const std::string hint = "SPACE resume  R restart  ESC quit";
            double tw = hud.text_width(hud.font_small, hint);
            hud.draw_text(batch, hud.font_small, hint,
                          cx - tw * 0.5, cy + 12.0, 180, 180, 180, 180);
        }
    }
};

} // namespace phigros::render
