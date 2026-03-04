#pragma once
#include "phigros/render/sprite_batch.hpp"
#include "phigros/render/hud_renderer.hpp"
#include "phigros/engine/judge.hpp"
#include <cstdio>
#include <cmath>
#include <string>
#include <algorithm>

namespace phigros::render {

// Phigros grade tiers (highest to lowest):
//   PHI  — 1,000,000 (all perfect)
//   V    — full combo, score ≥ 960,000
//   S    — full combo
//   A    — score ≥ 950,000
//   B    — score ≥ 900,000
//   C    — below 900,000
inline std::string compute_grade(int score, bool fc) {
    if (score == 1000000) return "PHI";
    if (fc && score >= 960000) return "V";
    if (fc) return "S";
    if (score >= 950000) return "A";
    if (score >= 900000) return "B";
    return "C";
}

// Draws the end-of-chart result overlay.
// `alpha` [0,1] drives the fade-in: overlay darkens from 0→1, text appears after 0.4.
struct ResultScreen {
    void draw(const SpriteBatch& batch, const HudRenderer& hud,
              const engine::Judge& judge, const engine::ScoreResult& sr,
              int playable_notes, int W, int H, double alpha) const {
        alpha = std::min(alpha, 1.0);

        // Dark overlay (fades in first)
        uint8_t bg_a = static_cast<uint8_t>(alpha * 160);
        batch.draw_rect(0, 0, W, H, 0, 0, 0, bg_a);

        if (!hud.has_font || alpha < 0.4) return;

        // Text fades in after overlay is mostly opaque
        uint8_t ta = static_cast<uint8_t>(((alpha - 0.4) / 0.6) * 230.0);
        double cx = W * 0.5, cy = H * 0.5;

        bool fc = (playable_notes > 0 && judge.max_combo >= playable_notes);
        std::string grade = compute_grade(sr.score, fc);

        // Grade label — golden for PHI, teal for V, white otherwise
        {
            uint8_t gr = 255, gg = 255, gb = 255;
            if (sr.score == 1000000) { gg = 220; gb = 80; }
            else if (fc && sr.score >= 960000) { gr = 80; gg = 220; gb = 200; }
            double tw = hud.text_width(hud.font_large, grade);
            hud.draw_text(batch, hud.font_large, grade,
                          cx - tw * 0.5, cy - 90.0, gr, gg, gb, ta);
        }

        // Score (7-digit zero-padded)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%07d", sr.score);
            double tw = hud.text_width(hud.font_large, buf);
            hud.draw_text(batch, hud.font_large, buf,
                          cx - tw * 0.5, cy - 42.0, 255, 255, 255, ta);
        }

        // Accuracy
        {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%.2f%%", sr.acc_ratio * 100.0);
            double tw = hud.text_width(hud.font_small, buf);
            hud.draw_text(batch, hud.font_small, buf,
                          cx - tw * 0.5, cy + 6.0, 200, 200, 200, ta);
        }

        // Max combo / total
        {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "MAX COMBO  %d / %d",
                          judge.max_combo, playable_notes);
            double tw = hud.text_width(hud.font_small, buf);
            hud.draw_text(batch, hud.font_small, buf,
                          cx - tw * 0.5, cy + 34.0, 180, 180, 180, ta);
        }

        // AP / FC badge
        if (sr.score == 1000000 || fc) {
            const char* badge = (sr.score == 1000000) ? "ALL PERFECT" : "FULL COMBO";
            uint8_t br = (sr.score == 1000000) ? 255 :  80;
            uint8_t bgg= (sr.score == 1000000) ? 220 : 255;
            uint8_t bb = (sr.score == 1000000) ?  80 :  80;
            double tw = hud.text_width(hud.font_small, badge);
            hud.draw_text(batch, hud.font_small, badge,
                          cx - tw * 0.5, cy + 62.0, br, bgg, bb, ta);
        }
    }
};

} // namespace phigros::render
