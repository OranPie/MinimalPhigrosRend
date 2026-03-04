#include "phigros/chart/phbc_io.hpp"
#include <ostream>
#include <istream>
#include <stdexcept>
#include <cstring>

namespace phigros::chart {

// ── low-level helpers ───────────────────────────────────────────────────────

static void w8 (std::ostream& o, uint8_t  v) { o.put(static_cast<char>(v)); }
static void w16(std::ostream& o, uint16_t v) { o.write(reinterpret_cast<const char*>(&v), 2); }
static void w32(std::ostream& o, int32_t  v) { o.write(reinterpret_cast<const char*>(&v), 4); }
static void wf (std::ostream& o, float    v) { o.write(reinterpret_cast<const char*>(&v), 4); }
static void wd (std::ostream& o, double   v) { o.write(reinterpret_cast<const char*>(&v), 8); }
static void wfv(std::ostream& o, const std::vector<float>& v) {
    if (!v.empty())
        o.write(reinterpret_cast<const char*>(v.data()),
                static_cast<std::streamsize>(v.size() * sizeof(float)));
}

static uint8_t  r8 (std::istream& i) { uint8_t  v; i.read(reinterpret_cast<char*>(&v), 1); return v; }
static uint16_t r16(std::istream& i) { uint16_t v; i.read(reinterpret_cast<char*>(&v), 2); return v; }
static int32_t  r32(std::istream& i) { int32_t  v; i.read(reinterpret_cast<char*>(&v), 4); return v; }
static float    rf (std::istream& i) { float    v; i.read(reinterpret_cast<char*>(&v), 4); return v; }
static double   rd (std::istream& i) { double   v; i.read(reinterpret_cast<char*>(&v), 8); return v; }
static std::vector<float> rfv(std::istream& i, int n) {
    std::vector<float> v(n);
    i.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(n * sizeof(float)));
    return v;
}

// ── write ───────────────────────────────────────────────────────────────────

void write_phbc(const CompiledChartData& c, std::ostream& os) {
    // Header
    os.write("PHBC", 4);
    w16(os, 1);  // version
    w16(os, 0);  // flags
    wd (os, c.offset);
    wd (os, c.chart_end_t);
    w32(os, static_cast<int32_t>(c.playable_count));
    w32(os, static_cast<int32_t>(c.notes.size()));
    w32(os, static_cast<int32_t>(c.lines.size()));
    wf (os, c.sample_rate);
    wd (os, c.t_start);
    w32(os, c.sample_count);

    // Lines
    for (const auto& l : c.lines) {
        w32(os, l.lid);
        w8 (os, l.color_rgb.r);
        w8 (os, l.color_rgb.g);
        w8 (os, l.color_rgb.b);
        w8 (os, 0); // pad
        int32_t dyn = l.color_r.empty() ? 0 : 1;
        w32(os, dyn);
        if (dyn) {
            wfv(os, l.color_r);
            wfv(os, l.color_g);
            wfv(os, l.color_b);
        }
        wfv(os, l.pos_x);
        wfv(os, l.pos_y);
        wfv(os, l.rot);
        wfv(os, l.alpha);
        wfv(os, l.scroll);
    }

    // Notes
    for (const auto& n : c.notes) {
        w32(os, n.nid);
        w32(os, n.line_id);
        w32(os, n.kind);
        w8 (os, n.above  ? 1 : 0);
        w8 (os, n.fake   ? 1 : 0);
        w8 (os, n.mh     ? 1 : 0);
        w8 (os, 0); // pad
        wd (os, n.t_hit);
        wd (os, n.t_end);
        wd (os, n.t_enter);
        wd (os, n.scroll_hit);
        wd (os, n.scroll_end);
        wd (os, n.x_local_px);
        wd (os, n.y_offset_px);
        wf (os, static_cast<float>(n.speed_mul));
        wf (os, static_cast<float>(n.size_px));
        wf (os, static_cast<float>(n.alpha01));
        w8 (os, n.tint_rgb.r);
        w8 (os, n.tint_rgb.g);
        w8 (os, n.tint_rgb.b);
        w8 (os, 0); // pad
    }
}

// ── read ────────────────────────────────────────────────────────────────────

CompiledChartData read_phbc(std::istream& is) {
    // Magic
    char magic[4];
    is.read(magic, 4);
    if (std::memcmp(magic, "PHBC", 4) != 0)
        throw std::runtime_error("read_phbc: bad magic (not a .phbc file)");

    uint16_t version = r16(is);
    if (version != 1)
        throw std::runtime_error("read_phbc: unsupported version " + std::to_string(version));
    r16(is); // flags (ignored)

    CompiledChartData c;
    c.offset         = rd(is);
    c.chart_end_t    = rd(is);
    c.playable_count = r32(is);
    int32_t note_count = r32(is);
    int32_t line_count = r32(is);
    c.sample_rate    = rf(is);
    c.t_start        = rd(is);
    c.sample_count   = r32(is);

    // Lines
    c.lines.resize(line_count);
    for (auto& l : c.lines) {
        l.lid          = r32(is);
        l.color_rgb.r  = r8(is);
        l.color_rgb.g  = r8(is);
        l.color_rgb.b  = r8(is);
        r8(is); // pad
        int32_t dyn    = r32(is);
        if (dyn) {
            l.color_r = rfv(is, c.sample_count);
            l.color_g = rfv(is, c.sample_count);
            l.color_b = rfv(is, c.sample_count);
        }
        l.pos_x  = rfv(is, c.sample_count);
        l.pos_y  = rfv(is, c.sample_count);
        l.rot    = rfv(is, c.sample_count);
        l.alpha  = rfv(is, c.sample_count);
        l.scroll = rfv(is, c.sample_count);
    }

    // Notes
    c.notes.resize(note_count);
    for (auto& n : c.notes) {
        n.nid          = r32(is);
        n.line_id      = r32(is);
        n.kind         = r32(is);
        n.above        = r8(is) != 0;
        n.fake         = r8(is) != 0;
        n.mh           = r8(is) != 0;
        r8(is); // pad
        n.t_hit        = rd(is);
        n.t_end        = rd(is);
        n.t_enter      = rd(is);
        n.scroll_hit   = rd(is);
        n.scroll_end   = rd(is);
        n.x_local_px   = rd(is);
        n.y_offset_px  = rd(is);
        n.speed_mul    = rf(is);
        n.size_px      = rf(is);
        n.alpha01      = rf(is);
        n.tint_rgb.r   = r8(is);
        n.tint_rgb.g   = r8(is);
        n.tint_rgb.b   = r8(is);
        r8(is); // pad
    }

    if (!is)
        throw std::runtime_error("read_phbc: stream read error");

    return c;
}

} // namespace phigros::chart
