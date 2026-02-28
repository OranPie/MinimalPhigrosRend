#include "phic/core/sw_renderer.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace phic {

// ============================================================
// 7-segment bitmap font data
// ============================================================

namespace {

namespace fs = std::filesystem;

// clang-format off
struct Glyph { uint8_t rows[7]; };

constexpr Glyph kFont7[] = {
    {{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, // 0
    {{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}}, // 1
    {{0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}}, // 2
    {{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}}, // 3
    {{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, // 4
    {{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}}, // 5
    {{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}}, // 6
    {{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}}, // 7
    {{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, // 8
    {{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}}, // 9
    {{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, // A
    {{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}}, // B
    {{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, // C
    {{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}}, // D
    {{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, // E
    {{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}}, // F
    {{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}}, // G
    {{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}}, // H
    {{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, // I
    {{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}}, // J
    {{0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, // K
    {{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}}, // L
    {{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, // M
    {{0x11,0x19,0x15,0x13,0x11,0x11,0x11}}, // N
    {{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, // O
    {{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}}, // P
    {{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, // Q
    {{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}}, // R
    {{0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}}, // S
    {{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}}, // T
    {{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, // U
    {{0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}}, // V
    {{0x11,0x11,0x11,0x15,0x15,0x15,0x0A}}, // W
    {{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}}, // X
    {{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}}, // Y
    {{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}}, // Z
};
// clang-format on

constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;

constexpr int kPunctSpace  = -1;
constexpr int kPunctDot    = -2;
constexpr int kPunctColon  = -3;
constexpr int kPunctSlash  = -4;
constexpr int kPunctPct    = -5;
constexpr int kPunctMinus  = -6;
constexpr int kPunctUndsc  = -7;
constexpr int kPunctLParen = -8;
constexpr int kPunctRParen = -9;

int classify_char(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'Z') return 10 + (ch - 'A');
    if (ch >= 'a' && ch <= 'z') return 10 + (ch - 'a');
    switch (ch) {
        case ' ':  return kPunctSpace;
        case '.':  return kPunctDot;
        case ':':  return kPunctColon;
        case '/':  return kPunctSlash;
        case '%':  return kPunctPct;
        case '-':  return kPunctMinus;
        case '_':  return kPunctUndsc;
        case '(':  return kPunctLParen;
        case ')':  return kPunctRParen;
        default:   return kPunctSpace;
    }
}

void bilinear_sample_rgb(const uint8_t* src, int sw, int sh,
                         double sx, double sy, uint8_t out[3]) {
    const int x0 = std::clamp(static_cast<int>(sx), 0, sw - 1);
    const int y0 = std::clamp(static_cast<int>(sy), 0, sh - 1);
    const int x1 = std::min(x0 + 1, sw - 1);
    const int y1 = std::min(y0 + 1, sh - 1);
    const double fx = sx - x0;
    const double fy = sy - y0;
    const uint8_t* p00 = src + (y0 * sw + x0) * 3;
    const uint8_t* p10 = src + (y0 * sw + x1) * 3;
    const uint8_t* p01 = src + (y1 * sw + x0) * 3;
    const uint8_t* p11 = src + (y1 * sw + x1) * 3;
    for (int c = 0; c < 3; ++c) {
        double v = p00[c] * (1 - fx) * (1 - fy)
                 + p10[c] * fx * (1 - fy)
                 + p01[c] * (1 - fx) * fy
                 + p11[c] * fx * fy;
        out[c] = static_cast<uint8_t>(std::clamp(v, 0.0, 255.0));
    }
}

// Minimal info.yml key-value parser
std::string yml_get(const std::string& text, const std::string& key) {
    auto pos = text.find(key + ":");
    if (pos == std::string::npos) return {};
    pos += key.size() + 1;
    while (pos < text.size() && text[pos] == ' ') ++pos;
    auto end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    std::string val = text.substr(pos, end - pos);
    // Trim quotes
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
        val = val.substr(1, val.size() - 2);
    return val;
}

// Parse "[a, b]" into two ints
bool yml_get_int2(const std::string& text, const std::string& key, int& a, int& b) {
    std::string val = yml_get(text, key);
    if (val.empty()) return false;
    // Strip brackets
    for (char& c : val) { if (c == '[' || c == ']') c = ' '; }
    std::istringstream iss(val);
    char comma;
    return bool(iss >> a >> comma >> b);
}

// Parse hex color "0xAARRGGBB"
bool yml_parse_color(const std::string& text, const std::string& key,
                     uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    std::string val = yml_get(text, key);
    if (val.empty()) return false;
    unsigned long hex = 0;
    try { hex = std::stoul(val, nullptr, 16); } catch (...) { return false; }
    a = static_cast<uint8_t>((hex >> 24) & 0xFF);
    r = static_cast<uint8_t>((hex >> 16) & 0xFF);
    g = static_cast<uint8_t>((hex >> 8) & 0xFF);
    b = static_cast<uint8_t>(hex & 0xFF);
    return true;
}

}  // namespace

// ============================================================
// Constructor
// ============================================================

SwRenderer::SwRenderer(Config cfg) : cfg_(std::move(cfg)) {
    buf_.resize(static_cast<std::size_t>(cfg_.width) * static_cast<std::size_t>(cfg_.height) * 3, 0);
}

// ============================================================
// Low-level drawing primitives
// ============================================================

void SwRenderer::fill_rect(int x0, int y0, int w, int h,
                            uint8_t r, uint8_t g, uint8_t b, double alpha) {
    const int W = cfg_.width;
    const int H = cfg_.height;
    int x1 = x0 + w;
    int y1 = y0 + h;
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(W, x1);
    y1 = std::min(H, y1);
    if (x0 >= x1 || y0 >= y1) return;

    if (alpha >= 0.999) {
        for (int y = y0; y < y1; ++y) {
            uint8_t* row = buf_.data() + static_cast<std::size_t>(y) * W * 3 + static_cast<std::size_t>(x0) * 3;
            for (int x = x0; x < x1; ++x) {
                row[0] = r; row[1] = g; row[2] = b;
                row += 3;
            }
        }
    } else if (alpha > 0.001) {
        const int a256 = static_cast<int>(alpha * 256.0);
        const int inv = 256 - a256;
        for (int y = y0; y < y1; ++y) {
            uint8_t* row = buf_.data() + static_cast<std::size_t>(y) * W * 3 + static_cast<std::size_t>(x0) * 3;
            for (int x = x0; x < x1; ++x) {
                row[0] = static_cast<uint8_t>((row[0] * inv + r * a256) >> 8);
                row[1] = static_cast<uint8_t>((row[1] * inv + g * a256) >> 8);
                row[2] = static_cast<uint8_t>((row[2] * inv + b * a256) >> 8);
                row += 3;
            }
        }
    }
}

void SwRenderer::draw_hline(int x0, int x1, int y,
                             uint8_t r, uint8_t g, uint8_t b, double alpha) {
    if (y < 0 || y >= cfg_.height) return;
    if (x0 > x1) std::swap(x0, x1);
    x0 = std::max(0, x0);
    x1 = std::min(cfg_.width - 1, x1);
    if (x0 > x1) return;
    fill_rect(x0, y, x1 - x0 + 1, 1, r, g, b, alpha);
}

void SwRenderer::draw_vline(int x, int y0, int y1,
                             uint8_t r, uint8_t g, uint8_t b, double alpha) {
    if (x < 0 || x >= cfg_.width) return;
    if (y0 > y1) std::swap(y0, y1);
    y0 = std::max(0, y0);
    y1 = std::min(cfg_.height - 1, y1);
    if (y0 > y1) return;
    fill_rect(x, y0, 1, y1 - y0 + 1, r, g, b, alpha);
}

void SwRenderer::fill_circle(double cx, double cy, double radius,
                              uint8_t r, uint8_t g, uint8_t b, double alpha) {
    fill_ellipse(cx, cy, radius, radius, r, g, b, alpha);
}

void SwRenderer::fill_ellipse(double cx, double cy, double rx, double ry,
                               uint8_t r, uint8_t g, uint8_t b, double alpha) {
    if (alpha <= 0.001 || rx < 0.5 || ry < 0.5) return;
    const int W = cfg_.width;
    const int H = cfg_.height;
    const int y0 = std::max(0, static_cast<int>(cy - ry));
    const int y1 = std::min(H - 1, static_cast<int>(cy + ry));
    const double rx2 = rx * rx;
    const double ry2 = ry * ry;
    const int a256 = static_cast<int>(alpha * 256.0);
    const int inv = 256 - a256;

    for (int y = y0; y <= y1; ++y) {
        const double dy = y - cy;
        const double dy2_norm = (dy * dy) / ry2;
        if (dy2_norm > 1.0) continue;
        const double half_w = rx * std::sqrt(1.0 - dy2_norm);
        const int lx = std::max(0, static_cast<int>(std::ceil(cx - half_w)));
        const int hx = std::min(W - 1, static_cast<int>(std::floor(cx + half_w)));
        uint8_t* row = buf_.data() + static_cast<std::size_t>(y) * W * 3;
        for (int x = lx; x <= hx; ++x) {
            uint8_t* p = row + x * 3;
            if (a256 >= 255) {
                p[0] = r; p[1] = g; p[2] = b;
            } else {
                p[0] = static_cast<uint8_t>((p[0] * inv + r * a256) >> 8);
                p[1] = static_cast<uint8_t>((p[1] * inv + g * a256) >> 8);
                p[2] = static_cast<uint8_t>((p[2] * inv + b * a256) >> 8);
            }
        }
    }
}

// ============================================================
// Texture blitting
// ============================================================

void SwRenderer::blit_texture(const Texture& tex, int dst_x, int dst_y,
                               int dst_w, int dst_h, double alpha) {
    if (tex.rgba.empty() || tex.w <= 0 || tex.h <= 0) return;
    if (dst_w <= 0 || dst_h <= 0 || alpha <= 0.001) return;
    const int W = cfg_.width;
    const int H = cfg_.height;
    const int dx0 = std::max(0, dst_x);
    const int dy0 = std::max(0, dst_y);
    const int dx1 = std::min(W, dst_x + dst_w);
    const int dy1 = std::min(H, dst_y + dst_h);
    if (dx0 >= dx1 || dy0 >= dy1) return;

    for (int dy = dy0; dy < dy1; ++dy) {
        const double sy = static_cast<double>(dy - dst_y) / dst_h * tex.h;
        const int sy0 = std::clamp(static_cast<int>(sy), 0, tex.h - 1);
        const int sy1 = std::min(sy0 + 1, tex.h - 1);
        const double fy = sy - sy0;
        uint8_t* dst_row = buf_.data() + static_cast<std::size_t>(dy) * W * 3;

        for (int dx = dx0; dx < dx1; ++dx) {
            const double sx = static_cast<double>(dx - dst_x) / dst_w * tex.w;
            const int sx0 = std::clamp(static_cast<int>(sx), 0, tex.w - 1);
            const int sx1 = std::min(sx0 + 1, tex.w - 1);
            const double fx = sx - sx0;

            const uint8_t* p00 = tex.rgba.data() + (sy0 * tex.w + sx0) * 4;
            const uint8_t* p10 = tex.rgba.data() + (sy0 * tex.w + sx1) * 4;
            const uint8_t* p01 = tex.rgba.data() + (sy1 * tex.w + sx0) * 4;
            const uint8_t* p11 = tex.rgba.data() + (sy1 * tex.w + sx1) * 4;

            double sr = p00[0]*(1-fx)*(1-fy) + p10[0]*fx*(1-fy) + p01[0]*(1-fx)*fy + p11[0]*fx*fy;
            double sg = p00[1]*(1-fx)*(1-fy) + p10[1]*fx*(1-fy) + p01[1]*(1-fx)*fy + p11[1]*fx*fy;
            double sb = p00[2]*(1-fx)*(1-fy) + p10[2]*fx*(1-fy) + p01[2]*(1-fx)*fy + p11[2]*fx*fy;
            double sa = p00[3]*(1-fx)*(1-fy) + p10[3]*fx*(1-fy) + p01[3]*(1-fx)*fy + p11[3]*fx*fy;

            const double a = (sa / 255.0) * alpha;
            if (a < 0.004) continue;
            uint8_t* dp = dst_row + dx * 3;
            const int a256 = static_cast<int>(a * 256.0);
            const int inv = 256 - a256;
            dp[0] = static_cast<uint8_t>((dp[0] * inv + static_cast<int>(sr) * a256) >> 8);
            dp[1] = static_cast<uint8_t>((dp[1] * inv + static_cast<int>(sg) * a256) >> 8);
            dp[2] = static_cast<uint8_t>((dp[2] * inv + static_cast<int>(sb) * a256) >> 8);
        }
    }
}

void SwRenderer::blit_sprite_frame(const Texture& sheet, int cols, int rows, int frame_idx,
                                    int dst_x, int dst_y, int dst_w, int dst_h,
                                    double alpha, uint8_t tint_r, uint8_t tint_g, uint8_t tint_b) {
    if (sheet.rgba.empty() || cols <= 0 || rows <= 0) return;
    const int total = cols * rows;
    frame_idx = std::clamp(frame_idx, 0, total - 1);
    const int frame_w = sheet.w / cols;
    const int frame_h = sheet.h / rows;
    if (frame_w <= 0 || frame_h <= 0) return;

    const int col = frame_idx % cols;
    const int row = frame_idx / cols;
    const int src_x0 = col * frame_w;
    const int src_y0 = row * frame_h;

    if (dst_w <= 0 || dst_h <= 0 || alpha <= 0.001) return;
    const int W = cfg_.width;
    const int H = cfg_.height;
    const int dx0 = std::max(0, dst_x);
    const int dy0 = std::max(0, dst_y);
    const int dx1 = std::min(W, dst_x + dst_w);
    const int dy1 = std::min(H, dst_y + dst_h);
    if (dx0 >= dx1 || dy0 >= dy1) return;

    const bool do_tint = (tint_r != 255 || tint_g != 255 || tint_b != 255);

    for (int dy = dy0; dy < dy1; ++dy) {
        const double fy_raw = static_cast<double>(dy - dst_y) / dst_h * frame_h;
        const int sy = std::clamp(static_cast<int>(fy_raw), 0, frame_h - 1);
        uint8_t* dst_row = buf_.data() + static_cast<std::size_t>(dy) * W * 3;

        for (int dx = dx0; dx < dx1; ++dx) {
            const double fx_raw = static_cast<double>(dx - dst_x) / dst_w * frame_w;
            const int sx = std::clamp(static_cast<int>(fx_raw), 0, frame_w - 1);

            const uint8_t* sp = sheet.rgba.data() +
                (static_cast<std::size_t>(src_y0 + sy) * sheet.w + (src_x0 + sx)) * 4;

            double sa = (sp[3] / 255.0) * alpha;
            if (sa < 0.004) continue;

            double sr = sp[0], sg = sp[1], sb = sp[2];
            if (do_tint) {
                sr = sr * tint_r / 255.0;
                sg = sg * tint_g / 255.0;
                sb = sb * tint_b / 255.0;
            }

            uint8_t* dp = dst_row + dx * 3;
            const int a256 = static_cast<int>(sa * 256.0);
            const int inv = 256 - a256;
            dp[0] = static_cast<uint8_t>((dp[0] * inv + static_cast<int>(sr) * a256) >> 8);
            dp[1] = static_cast<uint8_t>((dp[1] * inv + static_cast<int>(sg) * a256) >> 8);
            dp[2] = static_cast<uint8_t>((dp[2] * inv + static_cast<int>(sb) * a256) >> 8);
        }
    }
}

// ============================================================
// 7-segment text rendering
// ============================================================

void SwRenderer::draw_char7(int x0, int y0, char ch, int scale,
                             uint8_t r, uint8_t g, uint8_t b, double alpha) {
    const int idx = classify_char(ch);
    if (idx < 0) {
        switch (idx) {
            case kPunctDot:
                fill_rect(x0 + 1 * scale, y0 + 6 * scale, scale, scale, r, g, b, alpha);
                break;
            case kPunctColon:
                fill_rect(x0 + 2 * scale, y0 + 2 * scale, scale, scale, r, g, b, alpha);
                fill_rect(x0 + 2 * scale, y0 + 4 * scale, scale, scale, r, g, b, alpha);
                break;
            case kPunctSlash:
                for (int i = 0; i < kGlyphH; ++i) {
                    int xx = x0 + (kGlyphW - 1 - i * (kGlyphW - 1) / (kGlyphH - 1)) * scale;
                    fill_rect(xx, y0 + i * scale, scale, scale, r, g, b, alpha);
                }
                break;
            case kPunctPct:
                fill_rect(x0, y0, scale, scale, r, g, b, alpha);
                fill_rect(x0 + 4 * scale, y0 + 6 * scale, scale, scale, r, g, b, alpha);
                for (int i = 0; i < kGlyphH; ++i) {
                    int xx = x0 + (kGlyphW - 1 - i * (kGlyphW - 1) / (kGlyphH - 1)) * scale;
                    fill_rect(xx, y0 + i * scale, scale, scale, r, g, b, alpha);
                }
                break;
            case kPunctMinus:
                fill_rect(x0 + scale, y0 + 3 * scale, 3 * scale, scale, r, g, b, alpha);
                break;
            case kPunctUndsc:
                fill_rect(x0, y0 + 6 * scale, kGlyphW * scale, scale, r, g, b, alpha);
                break;
            case kPunctLParen:
                for (int i = 0; i < kGlyphH; ++i) {
                    int xx = x0 + ((i == 0 || i == 6) ? 2 : (i == 1 || i == 5) ? 1 : 0) * scale;
                    fill_rect(xx, y0 + i * scale, scale, scale, r, g, b, alpha);
                }
                break;
            case kPunctRParen:
                for (int i = 0; i < kGlyphH; ++i) {
                    int xx = x0 + ((i == 0 || i == 6) ? 2 : (i == 1 || i == 5) ? 3 : 4) * scale;
                    fill_rect(xx, y0 + i * scale, scale, scale, r, g, b, alpha);
                }
                break;
            default: break;
        }
        return;
    }
    if (idx >= static_cast<int>(sizeof(kFont7) / sizeof(kFont7[0]))) return;
    const Glyph& gl = kFont7[idx];
    for (int row = 0; row < kGlyphH; ++row) {
        uint8_t bits = gl.rows[row];
        for (int col = 0; col < kGlyphW; ++col) {
            if (bits & (1 << (kGlyphW - 1 - col))) {
                fill_rect(x0 + col * scale, y0 + row * scale, scale, scale, r, g, b, alpha);
            }
        }
    }
}

void SwRenderer::draw_text7(int x0, int y0, const std::string& text, int scale,
                             uint8_t r, uint8_t g, uint8_t b, double alpha) {
    const int char_w = (kGlyphW + 1) * scale;
    int x = x0;
    for (char ch : text) {
        draw_char7(x, y0, ch, scale, r, g, b, alpha);
        x += char_w;
    }
}

// ============================================================
// Background loading
// ============================================================

void SwRenderer::load_background(const std::string& path, int blur_factor, int dim) {
    int img_w = 0, img_h = 0, img_ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &img_w, &img_h, &img_ch, 3);
    if (!data) {
        std::fprintf(stderr, "warning: failed to load background: %s\n", path.c_str());
        return;
    }
    const int W = cfg_.width;
    const int H = cfg_.height;
    bg_rgb_.resize(static_cast<std::size_t>(W) * H * 3);

    // Bilinear resize to (W, H)
    for (int y = 0; y < H; ++y) {
        const double sy = static_cast<double>(y) * (img_h - 1) / std::max(1, H - 1);
        for (int x = 0; x < W; ++x) {
            const double sx = static_cast<double>(x) * (img_w - 1) / std::max(1, W - 1);
            uint8_t rgb[3];
            bilinear_sample_rgb(data, img_w, img_h, sx, sy, rgb);
            const std::size_t bi = static_cast<std::size_t>(y * W + x) * 3;
            bg_rgb_[bi] = rgb[0]; bg_rgb_[bi+1] = rgb[1]; bg_rgb_[bi+2] = rgb[2];
        }
    }
    stbi_image_free(data);

    // Box blur via downscale then bilinear upscale
    if (blur_factor > 1) {
        const int sW = std::max(1, W / blur_factor);
        const int sH = std::max(1, H / blur_factor);
        std::vector<uint8_t> small(static_cast<std::size_t>(sW) * sH * 3, 0);
        std::vector<uint32_t> accum(static_cast<std::size_t>(sW) * sH * 3, 0);
        std::vector<uint16_t> count(static_cast<std::size_t>(sW) * sH, 0);

        for (int y = 0; y < H; ++y) {
            const int sy = std::min(y * sH / H, sH - 1);
            for (int x = 0; x < W; ++x) {
                const int sx = std::min(x * sW / W, sW - 1);
                const std::size_t si = static_cast<std::size_t>(sy * sW + sx);
                const std::size_t bi = static_cast<std::size_t>(y * W + x) * 3;
                accum[si * 3 + 0] += bg_rgb_[bi]; accum[si * 3 + 1] += bg_rgb_[bi+1]; accum[si * 3 + 2] += bg_rgb_[bi+2];
                count[si]++;
            }
        }
        for (std::size_t i = 0; i < static_cast<std::size_t>(sW) * sH; ++i) {
            const uint16_t c = std::max(static_cast<uint16_t>(1), count[i]);
            small[i*3] = static_cast<uint8_t>(accum[i*3]/c);
            small[i*3+1] = static_cast<uint8_t>(accum[i*3+1]/c);
            small[i*3+2] = static_cast<uint8_t>(accum[i*3+2]/c);
        }
        // Bilinear upscale
        for (int y = 0; y < H; ++y) {
            const double sy = static_cast<double>(y) * (sH - 1) / std::max(1, H - 1);
            for (int x = 0; x < W; ++x) {
                const double sx = static_cast<double>(x) * (sW - 1) / std::max(1, W - 1);
                uint8_t rgb[3];
                bilinear_sample_rgb(small.data(), sW, sH, sx, sy, rgb);
                const std::size_t bi = static_cast<std::size_t>(y * W + x) * 3;
                bg_rgb_[bi] = rgb[0]; bg_rgb_[bi+1] = rgb[1]; bg_rgb_[bi+2] = rgb[2];
            }
        }
    }

    // Apply dim
    if (dim > 0) {
        const int d256 = std::min(256, dim);
        const int inv = 256 - d256;
        for (std::size_t i = 0; i < bg_rgb_.size(); ++i)
            bg_rgb_[i] = static_cast<uint8_t>((bg_rgb_[i] * inv) >> 8);
    }
    bg_loaded_ = true;
}

// ============================================================
// Respack loading
// ============================================================

void SwRenderer::load_respack(const std::string& zip_path) {
    std::string tmpdir = "/tmp/phic_respack_" + std::to_string(std::hash<std::string>{}(zip_path));
    {
        std::string cmd = "rm -rf \"" + tmpdir + "\" && mkdir -p \"" + tmpdir +
                          "\" && unzip -oq \"" + zip_path + "\" -d \"" + tmpdir + "\" 2>/dev/null";
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::fprintf(stderr, "warning: failed to extract respack: %s\n", zip_path.c_str());
            return;
        }
    }

    auto load_tex = [&](Texture& tex, const std::string& name) {
        std::string path = tmpdir + "/" + name;
        int w = 0, h = 0, ch = 0;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (data) {
            tex.w = w; tex.h = h;
            tex.rgba.assign(data, data + static_cast<std::size_t>(w) * h * 4);
            stbi_image_free(data);
        }
    };

    load_tex(tex_tap_,   "click.png");
    load_tex(tex_drag_,  "drag.png");
    load_tex(tex_flick_, "flick.png");
    load_tex(tex_hold_,  "hold.png");

    // Load hit-FX sprite sheet
    load_tex(hitfx_sheet_, "hit_fx.png");

    // Parse info.yml
    {
        std::string info_path = tmpdir + "/info.yml";
        std::ifstream ifs(info_path);
        if (ifs) {
            std::ostringstream ss;
            ss << ifs.rdbuf();
            std::string info = ss.str();

            yml_get_int2(info, "hitFx", hitfx_cols_, hitfx_rows_);
            std::string dur_s = yml_get(info, "hitFxDuration");
            if (!dur_s.empty()) { try { hitfx_duration_ = std::stod(dur_s); } catch (...) {} }
            std::string scale_s = yml_get(info, "hitFxScale");
            if (!scale_s.empty()) { try { hitfx_scale_ = std::stod(scale_s); } catch (...) {} }
            hitfx_tinted_ = (yml_get(info, "hitFxTinted") == "true");

            yml_parse_color(info, "colorPerfect", color_perfect_r_, color_perfect_g_, color_perfect_b_, color_perfect_a_);
            yml_parse_color(info, "colorGood", color_good_r_, color_good_g_, color_good_b_, color_good_a_);

            std::fprintf(stderr, "[respack] hitFx: %dx%d, duration=%.2f, scale=%.1f, tinted=%s\n",
                         hitfx_cols_, hitfx_rows_, hitfx_duration_, hitfx_scale_,
                         hitfx_tinted_ ? "true" : "false");
        }
    }

    respack_loaded_ = (tex_tap_.w > 0 || tex_drag_.w > 0 || tex_flick_.w > 0 || tex_hold_.w > 0);
    hitfx_loaded_ = (hitfx_sheet_.w > 0 && hitfx_cols_ > 0 && hitfx_rows_ > 0);

    if (respack_loaded_) std::fprintf(stderr, "[respack] loaded note textures from %s\n", zip_path.c_str());
    if (hitfx_loaded_)   std::fprintf(stderr, "[respack] loaded hit_fx sheet (%dx%d)\n", hitfx_sheet_.w, hitfx_sheet_.h);

    // Cleanup
    { std::string cmd = "rm -rf \"" + tmpdir + "\""; std::system(cmd.c_str()); }
}

// ============================================================
// Push judge events for hit-FX + hit sound log
// ============================================================

void SwRenderer::push_judge_events(const std::vector<JudgeEvent>& evts,
                                    const std::vector<FrameCommand>& cmds) {
    for (const auto& ev : evts) {
        if (ev.kind == JudgeKind::None || ev.kind == JudgeKind::Miss) continue;

        // Find the note position from FrameCommands
        double fx_x = 0.5, fx_y = 0.5;
        NoteKind note_kind = NoteKind::Tap;
        for (const auto& cmd : cmds) {
            if (cmd.note_id == ev.note_id) {
                fx_x = cmd.x;
                fx_y = cmd.y;
                note_kind = cmd.kind;
                break;
            }
        }

        // If not found in current frame, estimate from lane
        if (fx_x == 0.5 && fx_y == 0.5) {
            const int lane_count = std::max(1, cfg_.lane_count);
            fx_x = (static_cast<double>(ev.lane) + 0.5) / lane_count;
            fx_y = 0.5;
        }

        JudgeFx fx;
        fx.x = fx_x;
        fx.y = fx_y;
        fx.duration = hitfx_loaded_ ? hitfx_duration_ : 0.3;
        fx.ttl = fx.duration;
        fx.kind = static_cast<int>(ev.kind);

        if (ev.kind == JudgeKind::Perfect) {
            fx.r = color_perfect_r_; fx.g = color_perfect_g_; fx.b = color_perfect_b_;
        } else if (ev.kind == JudgeKind::Good) {
            fx.r = color_good_r_; fx.g = color_good_g_; fx.b = color_good_b_;
        } else {
            fx.r = 255; fx.g = 50; fx.b = 50;
        }
        fx_.push_back(fx);

        // Log hit sound event
        HitSoundEvent hs;
        hs.time_sec = ev.event_time;
        hs.kind = note_kind;
        hitsound_log_.push_back(hs);
    }
}

// ============================================================
// Rendering sub-passes
// ============================================================

void SwRenderer::clear_bg() {
    const int W = cfg_.width;
    const int H = cfg_.height;
    if (bg_loaded_ && bg_rgb_.size() == static_cast<std::size_t>(W) * H * 3) {
        std::memcpy(buf_.data(), bg_rgb_.data(), bg_rgb_.size());
    } else {
        const std::size_t total = static_cast<std::size_t>(W) * H * 3;
        uint8_t* p = buf_.data();
        for (std::size_t i = 0; i < total; i += 3) {
            p[i] = 10; p[i+1] = 10; p[i+2] = 14;
        }
        if (cfg_.bg_dim > 0)
            fill_rect(0, 0, W, H, 0, 0, 0, cfg_.bg_dim / 255.0);
    }
}

void SwRenderer::draw_lane_dividers() {}

void SwRenderer::draw_line_indicators(const std::vector<LineState>& lines) {
    const int W = cfg_.width;
    const int H = cfg_.height;

    // Align with Python: line_len = 6.75 * W, half_thick from max(1, 4/expand)
    const int half_len = static_cast<int>(3.375 * W);
    const int half_thick = std::max(1, static_cast<int>(2.0 / cfg_.expand));

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& ls = lines[i];
        if (ls.alpha < 0.01f) continue;

        const double cx = ls.x * W;
        const double cy = ls.y * H;
        const double a = static_cast<double>(ls.alpha);
        const double rot = static_cast<double>(ls.rot);

        uint8_t lr = 255, lg = 255, lb = 255;
        if (cfg_.multicolor_lines && lines.size() > 1) {
            double hue = std::fmod(static_cast<double>(i) / lines.size() * 360.0, 360.0);
            int hi = static_cast<int>(hue / 60.0) % 6;
            double f = hue / 60.0 - hi;
            uint8_t q = static_cast<uint8_t>(255 * (1.0 - f));
            uint8_t t_val = static_cast<uint8_t>(255 * f);
            switch (hi) {
                case 0: lr = 255; lg = t_val; lb = 0;   break;
                case 1: lr = q;   lg = 255;   lb = 0;   break;
                case 2: lr = 0;   lg = 255;   lb = t_val; break;
                case 3: lr = 0;   lg = q;     lb = 255; break;
                case 4: lr = t_val; lg = 0;   lb = 255; break;
                case 5: lr = 255; lg = 0;     lb = q;   break;
            }
        }

        const double cos_r = std::cos(rot);
        const double sin_r = std::sin(rot);

        for (int t = -half_len; t <= half_len; ++t) {
            const double px = cx + t * cos_r;
            const double py = cy + t * sin_r;
            for (int th = -half_thick; th <= half_thick; ++th) {
                const int ix = static_cast<int>(px - th * sin_r);
                const int iy = static_cast<int>(py + th * cos_r);
                if (ix >= 0 && ix < W && iy >= 0 && iy < H) {
                    const std::size_t off = (static_cast<std::size_t>(iy) * W + ix) * 3;
                    if (a >= 0.999) {
                        buf_[off] = lr; buf_[off+1] = lg; buf_[off+2] = lb;
                    } else {
                        const int a256 = static_cast<int>(a * 256.0);
                        const int inv = 256 - a256;
                        buf_[off]   = static_cast<uint8_t>((buf_[off]*inv + lr*a256) >> 8);
                        buf_[off+1] = static_cast<uint8_t>((buf_[off+1]*inv + lg*a256) >> 8);
                        buf_[off+2] = static_cast<uint8_t>((buf_[off+2]*inv + lb*a256) >> 8);
                    }
                }
            }
        }
    }
}

void SwRenderer::draw_notes(const std::vector<FrameCommand>& cmds) {
    const int W = cfg_.width;
    const int H = cfg_.height;
    const int base_w = static_cast<int>(W * 0.08 * cfg_.note_scale_x);
    const int base_h = static_cast<int>(H * 0.015 * cfg_.note_scale_y);

    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& cmd : cmds) {
            const bool is_hold = (cmd.kind == NoteKind::Hold);
            if ((pass == 0) != is_hold) continue;

            const int nx = static_cast<int>(cmd.x * W) - base_w / 2;
            const int ny = static_cast<int>(cmd.y * H) - base_h / 2;
            const double a = std::clamp(static_cast<double>(cmd.alpha), 0.0, 1.0);
            if (a < 0.01) continue;

            if (respack_loaded_) {
                const Texture* tex = nullptr;
                switch (cmd.kind) {
                    case NoteKind::Tap:   tex = &tex_tap_;   break;
                    case NoteKind::Drag:  tex = &tex_drag_;  break;
                    case NoteKind::Hold:  tex = &tex_hold_;  break;
                    case NoteKind::Flick: tex = &tex_flick_; break;
                }
                if (tex && tex->w > 0) {
                    if (is_hold) {
                        const int hold_end_y = static_cast<int>(cmd.hold_end_sec * H);
                        const int hold_h = std::max(base_h, std::abs(ny - hold_end_y) + base_h);
                        const int hold_y = std::min(ny, hold_end_y);
                        blit_texture(*tex, nx, hold_y, base_w, hold_h, a * 0.7);
                        blit_texture(tex_tap_.w > 0 ? tex_tap_ : *tex, nx, ny, base_w, base_h, a);
                    } else {
                        blit_texture(*tex, nx, ny, base_w, base_h, a);
                    }
                    continue;
                }
            }

            // Fallback: colored rectangles
            uint8_t nr = 0, ng = 196, nb = 255;
            switch (cmd.kind) {
                case NoteKind::Tap:   nr = 0;   ng = 196; nb = 255; break;
                case NoteKind::Drag:  nr = 255; ng = 200; nb = 0;   break;
                case NoteKind::Hold:  nr = 50;  ng = 205; nb = 50;  break;
                case NoteKind::Flick: nr = 255; ng = 50;  nb = 50;  break;
            }
            if (is_hold) {
                const int hold_end_y = static_cast<int>(cmd.hold_end_sec * H);
                const int hold_h = std::max(base_h, std::abs(ny - hold_end_y) + base_h);
                const int hold_y = std::min(ny, hold_end_y);
                fill_rect(nx, hold_y, base_w, hold_h, nr, ng, nb, a * 0.6);
                fill_rect(nx, ny, base_w, base_h, nr, ng, nb, a);
            } else {
                fill_rect(nx, ny, base_w, base_h, nr, ng, nb, a);
                if (cmd.kind == NoteKind::Flick) {
                    const int arrow_h = base_h;
                    const int arrow_w = base_w / 3;
                    const int acx = nx + base_w / 2;
                    const int acy = ny - arrow_h;
                    for (int row = 0; row < arrow_h; ++row) {
                        int half = arrow_w * (arrow_h - row) / (arrow_h + 1);
                        draw_hline(acx - half, acx + half, acy + row, nr, ng, nb, a);
                    }
                }
            }
        }
    }
}

void SwRenderer::draw_judge_fx(double dt) {
    const int W = cfg_.width;
    const int H = cfg_.height;

    auto it = fx_.begin();
    while (it != fx_.end()) {
        it->ttl -= dt;
        if (it->ttl <= 0.0) {
            it = fx_.erase(it);
            continue;
        }

        const double progress = 1.0 - (it->ttl / it->duration);
        const double lx = it->x * W;
        const double ly = it->y * H;

        if (hitfx_loaded_) {
            // Render sprite sheet frame
            const int total_frames = hitfx_cols_ * hitfx_rows_;
            const int frame_idx = std::clamp(static_cast<int>(progress * total_frames), 0, total_frames - 1);
            const int frame_w = hitfx_sheet_.w / hitfx_cols_;
            const int fx_size = static_cast<int>(frame_w * hitfx_scale_ * 0.5);
            const int dx = static_cast<int>(lx) - fx_size / 2;
            const int dy = static_cast<int>(ly) - fx_size / 2;

            if (hitfx_tinted_) {
                blit_sprite_frame(hitfx_sheet_, hitfx_cols_, hitfx_rows_, frame_idx,
                                  dx, dy, fx_size, fx_size, 1.0, it->r, it->g, it->b);
            } else {
                blit_sprite_frame(hitfx_sheet_, hitfx_cols_, hitfx_rows_, frame_idx,
                                  dx, dy, fx_size, fx_size, 1.0);
            }
        } else {
            // Fallback: expanding circle
            const double radius = 15.0 + progress * 40.0;
            const double alpha = (1.0 - progress) * 0.7;
            fill_circle(lx, ly, radius, it->r, it->g, it->b, alpha);
        }
        ++it;
    }
}

void SwRenderer::draw_hud(const EngineStats& stats, double time_sec, double chart_end_sec,
                           const std::string& title) {
    if (!cfg_.show_hud) return;
    const int W = cfg_.width;

    // Progress bar
    fill_rect(0, 0, W, 6, 40, 40, 40, 0.8);
    if (chart_end_sec > 0.0) {
        const double ratio = std::clamp(time_sec / chart_end_sec, 0.0, 1.0);
        fill_rect(0, 0, static_cast<int>(ratio * W), 6, 230, 230, 230, 0.9);
    }

    // Combo
    { char buf[64]; std::snprintf(buf, sizeof(buf), "COMBO %d", stats.combo);
      draw_text7(16, 14, buf, 3, 255, 255, 255, 0.9); }

    // Score
    { const int total = std::max(1, stats.judged_cnt);
      const double acc_ratio = stats.accuracy();
      const double combo_ratio = static_cast<double>(stats.max_combo) / total;
      const int score = static_cast<int>(acc_ratio * 900000.0 + combo_ratio * 100000.0);
      const int hit_pct = static_cast<int>(acc_ratio * 100.0);
      char buf[128];
      std::snprintf(buf, sizeof(buf), "SCORE %07d  HIT %d%%  MAX %d/%d",
                    score, hit_pct, stats.max_combo, stats.hit_total);
      draw_text7(16, 14 + 7*3 + 4, buf, 2, 200, 200, 200, 0.8); }

    // Timer
    { char buf[64]; std::snprintf(buf, sizeof(buf), "%.1fs / %.1fs", time_sec, chart_end_sec);
      draw_text7(16, 14 + 7*3 + 4 + 7*2 + 4, buf, 2, 180, 180, 180, 0.7); }

    // Title
    if (!title.empty()) {
        const int scale = 2;
        const int char_w = (kGlyphW + 1) * scale;
        const int text_w = static_cast<int>(title.size()) * char_w;
        draw_text7(W - text_w - 16, 14, title, scale, 200, 200, 200, 0.7);
    }
}

// ============================================================
// Main render_frame
// ============================================================

const std::vector<uint8_t>& SwRenderer::render_frame(
    const std::vector<FrameCommand>& cmds,
    const std::vector<LineState>&    lines,
    const EngineStats&               stats,
    double                           time_sec,
    double                           chart_end_sec,
    const std::string&               title) {

    constexpr double kDt = 1.0 / 60.0;
    clear_bg();
    draw_line_indicators(lines);
    draw_notes(cmds);
    draw_judge_fx(kDt);
    draw_hud(stats, time_sec, chart_end_sec, title);
    return buf_;
}

}  // namespace phic
