#include "phigros/chart/phbc_io.hpp"
#include "phigros/chart/phbc_compress.hpp"
#include "phigros/chart/phbc_crypto.hpp"
#include <ostream>
#include <istream>
#include <sstream>
#include <stdexcept>
#include <cstring>

namespace phigros::chart {

// ── membuf: wrap a byte array as an std::istream ────────────────────────────

struct membuf : std::streambuf {
    membuf(const uint8_t* begin, size_t size) {
        char* p = const_cast<char*>(reinterpret_cast<const char*>(begin));
        setg(p, p, p + size);
    }
};

// ── low-level helpers ───────────────────────────────────────────────────────

static void w8 (std::ostream& o, uint8_t  v) { o.put(static_cast<char>(v)); }
static void w16(std::ostream& o, uint16_t v) { o.write(reinterpret_cast<const char*>(&v), 2); }
static void w32(std::ostream& o, int32_t  v) { o.write(reinterpret_cast<const char*>(&v), 4); }
static void wu32(std::ostream& o, uint32_t v) { o.write(reinterpret_cast<const char*>(&v), 4); }
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
static uint32_t ru32(std::istream& i) { uint32_t v; i.read(reinterpret_cast<char*>(&v), 4); return v; }
static float    rf (std::istream& i) { float    v; i.read(reinterpret_cast<char*>(&v), 4); return v; }
static double   rd (std::istream& i) { double   v; i.read(reinterpret_cast<char*>(&v), 8); return v; }
static std::vector<float> rfv(std::istream& i, int n) {
    std::vector<float> v(n);
    i.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(n * sizeof(float)));
    return v;
}

// ── payload serialization (shared by v1 direct-write and v2 buffer-write) ──

static void write_payload(std::ostream& os, const CompiledChartData& c) {
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

static void read_payload(std::istream& is, CompiledChartData& c,
                         int32_t line_count, int32_t note_count) {
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
}

// ── write header ────────────────────────────────────────────────────────────

static void write_header(std::ostream& os, const CompiledChartData& c,
                         uint16_t version, uint16_t flags) {
    os.write("PHBC", 4);
    w16(os, version);
    w16(os, flags);
    wd (os, c.offset);
    wd (os, c.chart_end_t);
    w32(os, static_cast<int32_t>(c.playable_count));
    w32(os, static_cast<int32_t>(c.notes.size()));
    w32(os, static_cast<int32_t>(c.lines.size()));
    wf (os, c.sample_rate);
    wd (os, c.t_start);
    w32(os, c.sample_count);
}

// ── write v1 (backward-compatible) ──────────────────────────────────────────

void write_phbc(const CompiledChartData& c, std::ostream& os) {
    write_header(os, c, PHBC_VERSION_1, 0);
    write_payload(os, c);
}

// ── write v2 (with compression + encryption) ────────────────────────────────

void write_phbc(const CompiledChartData& c, std::ostream& os,
                const PhbcWriteOptions& opts) {
    // If nothing enabled, delegate to v1 for maximum compatibility
    if (!opts.compress && !opts.encrypt) {
        write_phbc(c, os);
        return;
    }

    // Build flags
    uint16_t flags = phbc_build_flags(opts);

    // Serialize payload to buffer
    std::ostringstream payload_ss(std::ios::binary);
    write_payload(payload_ss, c);
    std::string payload_str = payload_ss.str();
    std::vector<uint8_t> payload(payload_str.begin(), payload_str.end());
    uint32_t uncompressed_size = static_cast<uint32_t>(payload.size());

    // Compress if requested
    if (opts.compress)
        payload = phbc_compress(payload, opts.compress_algo);

    // Encrypt if requested
    PhbcCryptoMeta crypto_meta;
    if (opts.encrypt) {
        if (opts.password.empty())
            throw std::runtime_error("write_phbc: encryption requires a password");
        if (!phbc_encryption_available(opts.encrypt_algo))
            throw std::runtime_error(std::string("write_phbc: ") +
                encryption_name(opts.encrypt_algo) +
                " not available in this build");
        payload = phbc_encrypt(payload, opts.encrypt_algo, opts.password, crypto_meta);
    }

    // Write header
    write_header(os, c, PHBC_VERSION_2, flags);

    // Write metadata
    if (opts.compress)
        wu32(os, uncompressed_size);

    if (opts.encrypt)
        os.write(reinterpret_cast<const char*>(&crypto_meta), sizeof(PhbcCryptoMeta));

    // Write payload
    os.write(reinterpret_cast<const char*>(payload.data()),
             static_cast<std::streamsize>(payload.size()));
}

// ── read (v1 + v2) ──────────────────────────────────────────────────────────

CompiledChartData read_phbc(std::istream& is, const std::string& password) {
    // Magic
    char magic[4];
    is.read(magic, 4);
    if (std::memcmp(magic, "PHBC", 4) != 0)
        throw std::runtime_error("read_phbc: bad magic (not a .phbc file)");

    uint16_t version = r16(is);
    uint16_t flags   = r16(is);

    if (version != PHBC_VERSION_1 && version != PHBC_VERSION_2)
        throw std::runtime_error("read_phbc: unsupported version " + std::to_string(version));

    // Read header fields (same layout for v1 and v2)
    CompiledChartData c;
    c.offset         = rd(is);
    c.chart_end_t    = rd(is);
    c.playable_count = r32(is);
    int32_t note_count = r32(is);
    int32_t line_count = r32(is);
    c.sample_rate    = rf(is);
    c.t_start        = rd(is);
    c.sample_count   = r32(is);

    // v1: read payload directly from stream
    if (version == PHBC_VERSION_1) {
        read_payload(is, c, line_count, note_count);
        if (!is)
            throw std::runtime_error("read_phbc: stream read error");
        return c;
    }

    // v2: read metadata, then payload buffer, decrypt, decompress, parse

    auto comp_algo = phbc_compression_from_flags(flags);
    bool compressed = (flags & PHBC_FLAG_COMPRESSED) != 0;
    bool encrypted  = (flags & PHBC_FLAG_ENCRYPTED)  != 0;
    auto enc_algo  = phbc_encryption_from_flags(flags);

    uint32_t uncompressed_size = 0;
    if (compressed)
        uncompressed_size = ru32(is);

    PhbcCryptoMeta crypto_meta;
    if (encrypted)
        is.read(reinterpret_cast<char*>(&crypto_meta), sizeof(PhbcCryptoMeta));

    // Read remaining bytes as payload
    std::vector<uint8_t> payload(std::istreambuf_iterator<char>(is), {});

    if (encrypted) {
        if (password.empty())
            throw std::runtime_error("read_phbc: encrypted .phbc requires a password (use --password)");
        if (!phbc_encryption_available(enc_algo))
            throw std::runtime_error(std::string("read_phbc: ") +
                encryption_name(enc_algo) +
                " not available in this build (rebuild with -DUSE_ENCRYPTION=ON)");
        payload = phbc_decrypt(payload, enc_algo, password, crypto_meta);
    }

    if (compressed)
        payload = phbc_decompress(payload, comp_algo, uncompressed_size);

    // Parse payload from buffer
    membuf mbuf(payload.data(), payload.size());
    std::istream payload_stream(&mbuf);
    read_payload(payload_stream, c, line_count, note_count);

    if (!payload_stream)
        throw std::runtime_error("read_phbc: payload parse error (corrupted or wrong password)");

    return c;
}

} // namespace phigros::chart
