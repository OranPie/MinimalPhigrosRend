#pragma once
#include "phigros/chart/compiled_chart.hpp"
#include <iosfwd>
#include <string>
#include <cstdint>
#include <cstring>

namespace phigros::chart {

// ── PHBC binary chart format (.phbc) — little-endian ────────────────────────
//
// Version 1: Uncompressed, unencrypted payload.
// Version 2: Optional compression and/or encryption of the payload.
//
// ── Header (52 bytes, identical layout for v1 and v2) ──
//     [0-3]   char magic[4] = "PHBC"
//     [4-5]   uint16_t version (1 or 2)
//     [6-7]   uint16_t flags (0 for v1; bitfield for v2, see below)
//     [8-15]  double offset
//     [16-23] double chart_end_t
//     [24-27] int32_t playable_count
//     [28-31] int32_t note_count
//     [32-35] int32_t line_count
//     [36-39] float sample_rate
//     [40-47] double t_start
//     [48-51] int32_t sample_count
//
// ── v2 flags bitfield ──
//     Bit 0:   FLAG_COMPRESSED     payload is compressed
//     Bit 1:   FLAG_LZMA           1=LZMA, 0=zlib  (only when bit 0 set)
//     Bit 2:   FLAG_ENCRYPTED      payload is encrypted
//     Bits 3-4: Encryption algo    00=AES-256-GCM  01=AES-256-CBC
//                                  10=ChaCha20-Poly1305  11=XOR
//     Bits 5-15: Reserved (0)
//
// ── v2 metadata blocks (after header, before payload) ──
//   If FLAG_COMPRESSED:
//     uint32_t uncompressed_size
//   If FLAG_ENCRYPTED:
//     uint8_t  salt[16]          PBKDF2 salt
//     uint8_t  iv[16]            IV / nonce (GCM/ChaCha use first 12, CBC uses 16)
//     uint8_t  tag[16]           Auth tag (AEAD only; zeroed for CBC/XOR)
//
// ── Payload (per-line, per-note — same binary layout as v1) ──
//   Per-line (line_count):
//     int32_t lid
//     uint8_t color_r, color_g, color_b, _pad
//     int32_t dyn_color   (1 if dynamic color arrays follow, else 0)
//     if dyn_color: sample_count × float color_r, color_g, color_b
//     sample_count × float pos_x, pos_y, rot, alpha, scroll
//
//   Per-note (note_count):
//     int32_t nid, line_id, kind
//     uint8_t above, fake, mh, _pad
//     double  t_hit, t_end, t_enter, scroll_hit, scroll_end
//     double  x_local_px, y_offset_px
//     float   speed_mul, size_px, alpha01
//     uint8_t tint_r, tint_g, tint_b, _pad

// ── Constants ───────────────────────────────────────────────────────────────

static constexpr uint16_t PHBC_VERSION_1 = 1;
static constexpr uint16_t PHBC_VERSION_2 = 2;
static constexpr uint16_t PHBC_VERSION_CURRENT = PHBC_VERSION_2;

// Flags bitfield
static constexpr uint16_t PHBC_FLAG_COMPRESSED = 0x0001; // bit 0
static constexpr uint16_t PHBC_FLAG_LZMA       = 0x0002; // bit 1 (compression algo)
static constexpr uint16_t PHBC_FLAG_ENCRYPTED  = 0x0004; // bit 2
static constexpr uint16_t PHBC_FLAG_ENC_MASK   = 0x0018; // bits 3-4 (encryption algo)
static constexpr int      PHBC_FLAG_ENC_SHIFT  = 3;

// ── Enums ───────────────────────────────────────────────────────────────────

enum class CompressionAlgo : uint8_t {
    None = 0,
    Zlib = 1,
    Lzma = 2,
};

enum class EncryptionAlgo : uint8_t {
    AES_256_GCM       = 0, // bits 3-4 = 00
    AES_256_CBC       = 1, // bits 3-4 = 01
    ChaCha20_Poly1305 = 2, // bits 3-4 = 10
    XOR               = 3, // bits 3-4 = 11
};

// ── Crypto metadata (fixed 48 bytes when encrypted) ─────────────────────────

struct PhbcCryptoMeta {
    uint8_t salt[16] = {};
    uint8_t iv[16]   = {};
    uint8_t tag[16]  = {};
};

// ── Write/read options ──────────────────────────────────────────────────────

struct PhbcWriteOptions {
    bool            compress     = false;
    CompressionAlgo compress_algo = CompressionAlgo::Zlib;
    bool            encrypt      = false;
    EncryptionAlgo  encrypt_algo = EncryptionAlgo::AES_256_GCM;
    std::string     password;    // required when encrypt == true
};

// ── Helpers ─────────────────────────────────────────────────────────────────

// Build the flags uint16_t from write options.
inline uint16_t phbc_build_flags(const PhbcWriteOptions& opt) {
    uint16_t f = 0;
    if (opt.compress) {
        f |= PHBC_FLAG_COMPRESSED;
        if (opt.compress_algo == CompressionAlgo::Lzma)
            f |= PHBC_FLAG_LZMA;
    }
    if (opt.encrypt) {
        f |= PHBC_FLAG_ENCRYPTED;
        f |= (static_cast<uint16_t>(opt.encrypt_algo) << PHBC_FLAG_ENC_SHIFT) & PHBC_FLAG_ENC_MASK;
    }
    return f;
}

// Decode compression algo from flags.
inline CompressionAlgo phbc_compression_from_flags(uint16_t flags) {
    if (!(flags & PHBC_FLAG_COMPRESSED)) return CompressionAlgo::None;
    return (flags & PHBC_FLAG_LZMA) ? CompressionAlgo::Lzma : CompressionAlgo::Zlib;
}

// Decode encryption algo from flags.
inline EncryptionAlgo phbc_encryption_from_flags(uint16_t flags) {
    return static_cast<EncryptionAlgo>((flags & PHBC_FLAG_ENC_MASK) >> PHBC_FLAG_ENC_SHIFT);
}

// Human-readable names.
inline const char* compression_name(CompressionAlgo a) {
    switch (a) {
        case CompressionAlgo::Zlib: return "zlib";
        case CompressionAlgo::Lzma: return "lzma";
        default: return "none";
    }
}
inline const char* encryption_name(EncryptionAlgo a) {
    switch (a) {
        case EncryptionAlgo::AES_256_GCM:       return "aes-256-gcm";
        case EncryptionAlgo::AES_256_CBC:        return "aes-256-cbc";
        case EncryptionAlgo::ChaCha20_Poly1305:  return "chacha20-poly1305";
        case EncryptionAlgo::XOR:                return "xor";
    }
    return "unknown";
}

// ── Core I/O ────────────────────────────────────────────────────────────────

// Write a CompiledChartData to os in PHBC v1 format (uncompressed, unencrypted).
void write_phbc(const CompiledChartData& c, std::ostream& os);

// Write a CompiledChartData to os in PHBC v2 format with optional compression/encryption.
void write_phbc(const CompiledChartData& c, std::ostream& os, const PhbcWriteOptions& opts);

// Read a CompiledChartData from is. Supports v1 and v2.
// Pass password for encrypted v2 files; ignored for v1 / unencrypted v2.
CompiledChartData read_phbc(std::istream& is, const std::string& password = "");

} // namespace phigros::chart
