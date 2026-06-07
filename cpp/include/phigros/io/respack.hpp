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
#include <cctype>

// miniz header
#include "miniz.h"

namespace phigros::io {

inline std::string trim_copy(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    while (!s.empty() &&
           (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    return s;
}

inline std::string unquote_copy(std::string s) {
    s = trim_copy(std::move(s));
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') ||
         (s.front() == '\'' && s.back() == '\''))) {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

inline std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

inline bool parse_bool(const std::string& s) {
    std::string v = lower_copy(unquote_copy(s));
    return v == "true" || v == "yes" || v == "1" || v == "on";
}

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
    math::RGB color_perfect{255, 236, 159};
    math::RGB color_good{180, 225, 255};
    uint8_t alpha_perfect = 0xe1;
    uint8_t alpha_good = 0xeb;
};

struct Respack {
    RespackConfig cfg;

    // Note textures
    render::Texture click, drag, flick, hold;
    render::Texture click_mh, drag_mh, flick_mh, hold_mh;

    // Hit effect spritesheet
    render::Texture hitfx_sheet;
    render::Texture hitfx_sheet_perfect;
    render::Texture hitfx_sheet_good;

    // Raw hitsound OGG bytes loaded from respack (indexed by note kind 1–4).
    // Index 0 unused; 3 (hold) is empty if the respack has no hold.ogg.
    std::vector<uint8_t> hitsound_ogg[5];

    // Utility: white 4×4 texture for drawing lines/rectangles
    render::Texture white_tex;

    bool loaded = false;

    static bool textures_match(const render::Texture& a, const render::Texture& b) {
        if (!a.valid() || !b.valid()) return false;
        if (a.w != b.w || a.h != b.h) return false;
        if (a.pixel_data && b.pixel_data) return *a.pixel_data == *b.pixel_data;
        return a.tex == b.tex;
    }

    bool has_hold_mh_variant() const {
        if (!hold_mh.valid()) return false;
        if (cfg.hold_head_h_mh != cfg.hold_head_h || cfg.hold_tail_h_mh != cfg.hold_tail_h) {
            return true;
        }
        return !textures_match(hold, hold_mh);
    }

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
        hitfx_sheet.destroy();
        hitfx_sheet_perfect.destroy();
        hitfx_sheet_good.destroy();
        white_tex.destroy();
        loaded = false;
    }
};

// Parse hex color 0xAARRGGBB or 0xRRGGBB
inline void parse_hex_color(const std::string& s, math::RGB& rgb, uint8_t& alpha) {
    unsigned long long val = 0;
    std::string raw = trim_copy(s);
    try {
        if (raw.size() > 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X'))
            val = std::stoull(raw.substr(2), nullptr, 16);
        else
            val = std::stoull(raw, nullptr, 16);
    } catch (...) { return; }

    size_t digits = raw.size();
    if (digits > 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X'))
        digits -= 2;
    if (digits == 8) { // AARRGGBB
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
    bool saw_hold_atlas_mh = false;
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

        key = trim_copy(std::move(key));
        val = unquote_copy(std::move(val));

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
                cfg.hold_tail_h = a;
                cfg.hold_head_h = b;
            }
        }
        else if (key == "holdAtlasMH") {
            int a = 0, b = 0;
            auto bracket = val.find('[');
            if (bracket != std::string::npos &&
                (std::sscanf(val.c_str() + bracket, "[%d, %d]", &a, &b) == 2 ||
                 std::sscanf(val.c_str() + bracket, "[%d,%d]", &a, &b) == 2)) {
                cfg.hold_tail_h_mh = a;
                cfg.hold_head_h_mh = b;
                saw_hold_atlas_mh = true;
            }
        }
        else if (key == "hitFxDuration") cfg.hitfx_duration = std::atof(val.c_str());
        else if (key == "hitFxScale") cfg.hitfx_scale = std::atof(val.c_str());
        else if (key == "hitFxRotate") cfg.hitfx_rotate = parse_bool(val);
        else if (key == "hitFxTinted") cfg.hitfx_tinted = parse_bool(val);
        else if (key == "hideParticles") cfg.hide_particles = parse_bool(val);
        else if (key == "holdKeepHead") cfg.hold_keep_head = parse_bool(val);
        else if (key == "holdRepeat") cfg.hold_repeat = parse_bool(val);
        else if (key == "holdCompact") cfg.hold_compact = parse_bool(val);
        else if (key == "colorPerfect") parse_hex_color(val, cfg.color_perfect, cfg.alpha_perfect);
        else if (key == "colorGood") parse_hex_color(val, cfg.color_good, cfg.alpha_good);
    }
    if (!saw_hold_atlas_mh) {
        cfg.hold_tail_h_mh = cfg.hold_tail_h;
        cfg.hold_head_h_mh = cfg.hold_head_h;
    }
    return cfg;
}

inline std::string normalize_zip_member_name(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    while (name.rfind("./", 0) == 0) name.erase(0, 2);
    while (!name.empty() && name.front() == '/') name.erase(name.begin());
    return lower_copy(name);
}

inline bool is_ignored_zip_member(const std::string& normalized_name) {
    if (normalized_name.rfind("__macosx/", 0) == 0) return true;
    auto slash = normalized_name.find_last_of('/');
    std::string base = slash == std::string::npos
        ? normalized_name
        : normalized_name.substr(slash + 1);
    return base.rfind("._", 0) == 0 || base.empty();
}

inline int zip_find_member(mz_zip_archive& zip, const char* name) {
    int idx = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
    if (idx >= 0) return idx;

    const std::string wanted = normalize_zip_member_name(name);
    const int file_count = static_cast<int>(mz_zip_reader_get_num_files(&zip));
    for (int i = 0; i < file_count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;
        if (stat.m_is_directory) continue;

        std::string candidate = normalize_zip_member_name(stat.m_filename);
        if (is_ignored_zip_member(candidate)) continue;
        if (candidate == wanted) return i;
        if (candidate.size() > wanted.size() &&
            candidate.compare(candidate.size() - wanted.size(), wanted.size(), wanted) == 0 &&
            candidate[candidate.size() - wanted.size() - 1] == '/') {
            return i;
        }
    }
    return -1;
}

// Extract a file from a miniz ZIP archive. Besides exact root entries, accept
// packs that were zipped with a single top-level folder (e.g. Skin/info.yml).
inline std::vector<uint8_t> zip_extract(mz_zip_archive& zip, const char* name) {
    int idx = zip_find_member(zip, name);
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
    return render::Texture::from_memory_cached(ren, data.data(), static_cast<int>(data.size()));
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
    if (info_data.empty())
        info_data = zip_extract(zip, "info.yaml");
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
    rp.hitfx_sheet_perfect = load_tex_from_zip(zip, ren, "hit_fx_perfect.png");
    rp.hitfx_sheet_good = load_tex_from_zip(zip, ren, "hit_fx_good.png");

    // Hitsound audio (OGG bytes stored for AudioSystem to decode into pools)
    rp.hitsound_ogg[1] = zip_extract(zip, "click.ogg");
    rp.hitsound_ogg[2] = zip_extract(zip, "drag.ogg");
    rp.hitsound_ogg[4] = zip_extract(zip, "flick.ogg");
    rp.hitsound_ogg[3] = zip_extract(zip, "hold.ogg"); // optional; falls back to tap

    mz_zip_reader_end(&zip);

    // White texture for line/rect drawing
    rp.white_tex = render::Texture::solid_rect(ren, 4, 4, 255, 255, 255);
    rp.loaded = true;
    return rp;
}

} // namespace phigros::io
