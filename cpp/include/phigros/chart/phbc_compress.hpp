#pragma once
#include "phigros/chart/phbc_io.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace phigros::chart {

// Compress raw data using the specified algorithm.
// Throws std::runtime_error if the algorithm is unavailable or compression fails.
std::vector<uint8_t> phbc_compress(const std::vector<uint8_t>& data,
                                   CompressionAlgo algo);

// Decompress data using the specified algorithm.
// `uncompressed_size` is the expected output size (stored in PHBC header).
// Throws std::runtime_error on failure.
std::vector<uint8_t> phbc_decompress(const std::vector<uint8_t>& data,
                                     CompressionAlgo algo,
                                     uint32_t uncompressed_size);

} // namespace phigros::chart
