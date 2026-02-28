#pragma once

#include "phic/core/types.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace phic {

// CPU-only software renderer that rasterises engine output to an RGB24 buffer.
class SwRenderer {
public:
    struct Config {
        int width = 1280;
        int height = 720;
        int lane_count = 8;
        int bg_dim = 120;        // 0-255
        double approach_sec = 3.0;
        double expand = 1.0;
        double overrender = 2.0;
        double note_scale_x = 1.0;
        double note_scale_y = 1.0;
        double note_flow_speed_mul = 1.0;
        bool multicolor_lines = false;
        bool show_hud = true;

        std::string bg_path;       // background image path (empty = dark fill)
        int bg_blur_factor = 10;   // downscale factor for blur
    };

    explicit SwRenderer(Config cfg);

    const std::vector<uint8_t>& render_frame(
        const std::vector<FrameCommand>& cmds,
        const std::vector<LineState>&    lines,
        const EngineStats&               stats,
        double                           time_sec,
        double                           chart_end_sec,
        const std::string&               title);

    int width()  const { return cfg_.width; }
    int height() const { return cfg_.height; }
    std::size_t buffer_bytes() const {
        return static_cast<std::size_t>(cfg_.width) * static_cast<std::size_t>(cfg_.height) * 3;
    }

    void push_judge_events(const std::vector<JudgeEvent>& evts,
                           const std::vector<FrameCommand>& cmds);

    void load_background(const std::string& path, int blur_factor, int dim);
    void load_respack(const std::string& zip_path);

    // Collect hit sound events during rendering (time_sec, kind).
    struct HitSoundEvent {
        double time_sec;
        NoteKind kind;
    };
    const std::vector<HitSoundEvent>& hit_sound_log() const { return hitsound_log_; }
    void clear_hit_sound_log() { hitsound_log_.clear(); }

    Config& config() { return cfg_; }

private:
    Config cfg_;
    std::vector<uint8_t> buf_;

    std::vector<uint8_t> bg_rgb_;
    bool bg_loaded_ = false;

    // RGBA texture
    struct Texture {
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
    };
    Texture tex_tap_, tex_drag_, tex_flick_, tex_hold_;
    bool respack_loaded_ = false;

    // Hit-FX sprite sheet
    Texture hitfx_sheet_;
    int hitfx_cols_ = 1, hitfx_rows_ = 1;
    double hitfx_duration_ = 0.5;
    double hitfx_scale_ = 1.0;
    bool hitfx_tinted_ = false;
    bool hitfx_loaded_ = false;

    // Judge FX colors from respack
    uint8_t color_perfect_r_ = 255, color_perfect_g_ = 215, color_perfect_b_ = 0, color_perfect_a_ = 235;
    uint8_t color_good_r_ = 50, color_good_g_ = 150, color_good_b_ = 255, color_good_a_ = 235;

    struct JudgeFx {
        double x, y;       // screen-normalised position
        double ttl;
        double duration;
        int kind;           // 1=perfect,2=good,3=bad,4=miss
        uint8_t r, g, b;
    };
    std::vector<JudgeFx> fx_;

    // Hit sound event log
    std::vector<HitSoundEvent> hitsound_log_;

    // Primitives
    void clear_bg();
    void draw_lane_dividers();
    void draw_notes(const std::vector<FrameCommand>& cmds);
    void draw_line_indicators(const std::vector<LineState>& lines);
    void draw_judge_fx(double dt);
    void draw_hud(const EngineStats& stats, double time_sec, double chart_end_sec,
                  const std::string& title);

    void fill_rect(int x0, int y0, int w, int h, uint8_t r, uint8_t g, uint8_t b, double alpha = 1.0);
    void fill_ellipse(double cx, double cy, double rx, double ry,
                      uint8_t r, uint8_t g, uint8_t b, double alpha = 1.0);
    void fill_circle(double cx, double cy, double radius,
                     uint8_t r, uint8_t g, uint8_t b, double alpha = 1.0);
    void draw_hline(int x0, int x1, int y, uint8_t r, uint8_t g, uint8_t b, double alpha = 1.0);
    void draw_vline(int x, int y0, int y1, uint8_t r, uint8_t g, uint8_t b, double alpha = 1.0);
    void draw_char7(int x0, int y0, char ch, int scale, uint8_t r, uint8_t g, uint8_t b, double alpha = 1.0);
    void draw_text7(int x0, int y0, const std::string& text, int scale,
                    uint8_t r, uint8_t g, uint8_t b, double alpha = 1.0);
    void blit_texture(const Texture& tex, int dst_x, int dst_y, int dst_w, int dst_h, double alpha = 1.0);
    void blit_sprite_frame(const Texture& sheet, int cols, int rows, int frame_idx,
                           int dst_x, int dst_y, int dst_w, int dst_h,
                           double alpha, uint8_t tint_r = 255, uint8_t tint_g = 255, uint8_t tint_b = 255);
};

}  // namespace phic
