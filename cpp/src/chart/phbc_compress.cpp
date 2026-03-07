#include "phigros/chart/phbc_compress.hpp"
#include <miniz.h>
#include <stdexcept>

#ifdef PHIGROS_HAS_LZMA
#include <lzma.h>
#endif

namespace phigros::chart {

// ── zlib (miniz) ────────────────────────────────────────────────────────────

static std::vector<uint8_t> zlib_compress(const std::vector<uint8_t>& data) {
    mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(data.size()));
    std::vector<uint8_t> out(bound);
    mz_ulong out_len = bound;
    int res = mz_compress2(out.data(), &out_len,
                           data.data(), static_cast<mz_ulong>(data.size()),
                           MZ_DEFAULT_COMPRESSION);
    if (res != MZ_OK)
        throw std::runtime_error("phbc_compress: zlib compression failed (error " +
                                 std::to_string(res) + ")");
    out.resize(out_len);
    return out;
}

static std::vector<uint8_t> zlib_decompress(const std::vector<uint8_t>& data,
                                            uint32_t uncompressed_size) {
    std::vector<uint8_t> out(uncompressed_size);
    mz_ulong dest_len = uncompressed_size;
    int res = mz_uncompress(out.data(), &dest_len,
                            data.data(), static_cast<mz_ulong>(data.size()));
    if (res != MZ_OK)
        throw std::runtime_error("phbc_decompress: zlib decompression failed (error " +
                                 std::to_string(res) + ")");
    out.resize(dest_len);
    return out;
}

// ── LZMA ────────────────────────────────────────────────────────────────────

#ifdef PHIGROS_HAS_LZMA

static std::vector<uint8_t> lzma_compress_impl(const std::vector<uint8_t>& data) {
    // Worst-case output size: input + overhead
    size_t out_cap = lzma_stream_buffer_bound(data.size());
    std::vector<uint8_t> out(out_cap);
    size_t out_pos = 0;
    lzma_ret ret = lzma_easy_buffer_encode(
        6,               // preset (0-9, 6 is default balance)
        LZMA_CHECK_CRC64,
        nullptr,         // allocator
        data.data(), data.size(),
        out.data(), &out_pos, out_cap);
    if (ret != LZMA_OK)
        throw std::runtime_error("phbc_compress: LZMA compression failed (error " +
                                 std::to_string(ret) + ")");
    out.resize(out_pos);
    return out;
}

static std::vector<uint8_t> lzma_decompress_impl(const std::vector<uint8_t>& data,
                                                  uint32_t uncompressed_size) {
    std::vector<uint8_t> out(uncompressed_size);
    size_t in_pos = 0, out_pos = 0;
    uint64_t memlimit = 128 * 1024 * 1024; // 128 MB memory limit
    lzma_ret ret = lzma_stream_buffer_decode(
        &memlimit, 0, nullptr,
        data.data(), &in_pos, data.size(),
        out.data(), &out_pos, out.size());
    if (ret != LZMA_OK)
        throw std::runtime_error("phbc_decompress: LZMA decompression failed (error " +
                                 std::to_string(ret) + ")");
    out.resize(out_pos);
    return out;
}

#endif // PHIGROS_HAS_LZMA

// ── Public API ──────────────────────────────────────────────────────────────

std::vector<uint8_t> phbc_compress(const std::vector<uint8_t>& data,
                                   CompressionAlgo algo) {
    switch (algo) {
        case CompressionAlgo::Zlib:
            return zlib_compress(data);
        case CompressionAlgo::Lzma:
#ifdef PHIGROS_HAS_LZMA
            return lzma_compress_impl(data);
#else
            throw std::runtime_error("phbc_compress: LZMA support not compiled (build with -DUSE_LZMA=ON)");
#endif
        default:
            throw std::runtime_error("phbc_compress: unknown algorithm");
    }
}

std::vector<uint8_t> phbc_decompress(const std::vector<uint8_t>& data,
                                     CompressionAlgo algo,
                                     uint32_t uncompressed_size) {
    switch (algo) {
        case CompressionAlgo::Zlib:
            return zlib_decompress(data, uncompressed_size);
        case CompressionAlgo::Lzma:
#ifdef PHIGROS_HAS_LZMA
            return lzma_decompress_impl(data, uncompressed_size);
#else
            throw std::runtime_error("phbc_decompress: LZMA support not compiled (build with -DUSE_LZMA=ON)");
#endif
        default:
            throw std::runtime_error("phbc_decompress: unknown algorithm");
    }
}

} // namespace phigros::chart
