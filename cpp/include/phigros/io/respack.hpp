#pragma once
#include "phigros/render/texture.hpp"
#include "phigros/math/util.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstdio>

// miniz header
#include "miniz.h"

namespace phigros::io {

struct RespackConfig {
    std::string name;
    int hitfx_cols = 5, hitfx_rows = 6;
    int hold_head_h = 20, hold_tail_h = 20;
    int hold_head_h_mh = 20, hold_tail_h_mh = 20;
    double hitfx_duration = 0.5;
    double hitfx_scale = 1.0;
    bool hitfx_rotate = false;
    bool hitfx_tinted = true;
    bool hide_particles = false;
    bool hold_keep_head = false;
    bool hold_repeat = false;
    bool hold_compact = false;
    math::RGB color_perfect{235, 255, 236};
    math::RGB color_good{235, 180, 225};
    uint8_t alpha_perfect = 160;
    uint8_t alpha_good = 255;
};

struct Respack {
    RespackConfig cfg;

    // Note textures
    render::Texture click, drag, flick, hold;
    render::Texture click_mh, drag_mh, flick_mh, hold_mh;

    // Hit effect spritesheet
    render::Texture hitfx_sheet;

    // Utility: white 4×4 texture for drawing lines/rectangles
    render::Texture white_tex;

    bool loaded = false;

    const render::Texture& note_texture(int kind, bool mh_flag) const {
        if (mh_flag) {
            switch (kind) {
                case 1: return click_mh.valid() ? click_mh : click;
                case 2: return drag_mh.valid() ? drag_mh : drag;
                case 3: return hold_mh.valid() ? hold_mh : hold;
                case 4: return flick_mh.valid() ? flick_mh : flick;
            }
        }
        switch (kind) {
            case 1: return click;
            case 2: return drag;
            case 3: return hold;
            case 4: return flick;
            default: return click;
        }
    }

    void destroy() {
        click.destroy(); drag.destroy(); flick.destroy(); hold.destroy();
        click_mh.destroy(); drag_mh.destroy(); flick_mh.destroy(); hold_mh.destroy();
        hitfx_sheet.destroy(); white_tex.destroy();
        loaded = false;
    }
};

// Parse hex color 0xAARRGGBB or 0xRRGGBB
inline void parse_hex_color(const std::string& s, math::RGB& rgb, uint8_t& alpha) {
    unsigned long long val = 0;
    try {
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            val = std::stoull(s.substr(2), nullptr, 16);
        else
            val = std::stoull(s, nullptr, 16);
    } catch (...) { return; }

    if (s.size() > 8) { // 0xAARRGGBB
        alpha = static_cast<uint8_t>((val >> 24) & 0xFF);
        rgb.r = static_cast<uint8_t>((val >> 16) & 0xFF);
        rgb.g = static_cast<uint8_t>((val >> 8) & 0xFF);
        rgb.b = static_cast<uint8_t>(val & 0xFF);
    } else { // 0xRRGGBB
        rgb.r = static_cast<uint8_t>((val >> 16) & 0xFF);
        rgb.g = static_cast<uint8_t>((val >> 8) & 0xFF);
        rgb.b = static_cast<uint8_t>(val & 0xFF);
        alpha = 255;
    }
}

// Parse info.yml (minimal YAML parser matching Python's _parse_info_yml_minimal)
inline RespackConfig parse_info_yml(const std::string& text) {
    RespackConfig cfg;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        // Strip comment
        auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos) line = line.substr(0, comment_pos);

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);

        // Trim
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
        };
        trim(key); trim(val);
        // Remove quotes
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);

        if (key == "name") cfg.name = val;
        else if (key == "hitFx" || key == "hitfx") {
            // Parse [cols, rows]
            auto bracket = val.find('[');
            if (bracket != std::string::npos) {
                int a = 0, b = 0;
                if (std::sscanf(val.c_str() + bracket, "[%d, %d]", &a, &b) == 2 ||
                    std::sscanf(val.c_str() + bracket, "[%d,%d]", &a, &b) == 2) {
                    cfg.hitfx_cols = a; cfg.hitfx_rows = b;
                }
            }
        }
        else if (key == "holdAtlas") {
            int a = 0, b = 0;
            auto bracket = val.find('[');
            if (bracket != std::string::npos &&
                (std::sscanf(val.c_str() + bracket, "[%d, %d]", &a, &b) == 2 ||
                 std::sscanf(val.c_str() + bracket, "[%d,%d]", &a, &b) == 2)) {
                cfg.hold_head_h = a; cfg.hold_tail_h = b;
            }
        }
        else if (key == "holdAtlasMH") {
            int a = 0, b = 0;
            auto bracket = val.find('[');
            if (bracket != std::string::npos &&
                (std::sscanf(val.c_str() + bracket, "[%d, %d]", &a, &b) == 2 ||
                 std::sscanf(val.c_str() + bracket, "[%d,%d]", &a, &b) == 2)) {
                cfg.hold_head_h_mh = a; cfg.hold_tail_h_mh = b;
            }
        }
        else if (key == "hitFxDuration") cfg.hitfx_duration = std::atof(val.c_str());
        else if (key == "hitFxScale") cfg.hitfx_scale = std::atof(val.c_str());
        else if (key == "hitFxRotate") cfg.hitfx_rotate = (val == "true");
        else if (key == "hitFxTinted") cfg.hitfx_tinted = (val == "true");
        else if (key == "hideParticles") cfg.hide_particles = (val == "true");
        else if (key == "holdKeepHead") cfg.hold_keep_head = (val == "true");
        else if (key == "holdRepeat") cfg.hold_repeat = (val == "true");
        else if (key == "holdCompact") cfg.hold_compact = (val == "true");
        else if (key == "colorPerfect") parse_hex_color(val, cfg.color_perfect, cfg.alpha_perfect);
        else if (key == "colorGood") parse_hex_color(val, cfg.color_good, cfg.alpha_good);
    }
    return cfg;
}

// Extract a file from a miniz ZIP archive
inline std::vector<uint8_t> zip_extract(mz_zip_archive& zip, const char* name) {
    int idx = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
    if (idx < 0) return {};
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&zip, idx, &stat)) return {};
    std::vector<uint8_t> buf(static_cast<size_t>(stat.m_uncomp_size));
    if (!mz_zip_reader_extract_to_mem(&zip, idx, buf.data(), buf.size(), 0)) return {};
    return buf;
}

inline render::Texture load_tex_from_zip(mz_zip_archive& zip, SDL_Renderer* ren, const char* name) {
    auto data = zip_extract(zip, name);
    if (data.empty()) return {};
    return render::Texture::from_memory(ren, data.data(), static_cast<int>(data.size()));
}

inline Respack load_respack(SDL_Renderer* ren, const std::string& zip_path) {
    Respack rp;

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zip_path.c_str(), 0)) {
        // Create fallback colored textures
        rp.click = render::Texture::solid_rect(ren, 64, 8, 255, 220, 120);
        rp.drag  = render::Texture::solid_rect(ren, 64, 5, 140, 240, 255);
        rp.flick = render::Texture::solid_rect(ren, 64, 16, 255, 140, 220);
        rp.hold  = render::Texture::solid_rect(ren, 64, 128, 120, 200, 255);
        rp.white_tex = render::Texture::solid_rect(ren, 4, 4, 255, 255, 255);
        rp.loaded = true;
        return rp;
    }

    // Parse info.yml
    auto info_data = zip_extract(zip, "info.yml");
    if (!info_data.empty())
        rp.cfg = parse_info_yml(std::string(info_data.begin(), info_data.end()));

    // Load textures
    rp.click     = load_tex_from_zip(zip, ren, "click.png");
    rp.drag      = load_tex_from_zip(zip, ren, "drag.png");
    rp.flick     = load_tex_from_zip(zip, ren, "flick.png");
    rp.hold      = load_tex_from_zip(zip, ren, "hold.png");
    rp.click_mh  = load_tex_from_zip(zip, ren, "click_mh.png");
    rp.drag_mh   = load_tex_from_zip(zip, ren, "drag_mh.png");
    rp.flick_mh  = load_tex_from_zip(zip, ren, "flick_mh.png");
    rp.hold_mh   = load_tex_from_zip(zip, ren, "hold_mh.png");
    rp.hitfx_sheet = load_tex_from_zip(zip, ren, "hit_fx.png");

    mz_zip_reader_end(&zip);

    // White texture for line/rect drawing
    rp.white_tex = render::Texture::solid_rect(ren, 4, 4, 255, 255, 255);
    rp.loaded = true;
    return rp;
}

} // namespace phigros::io
