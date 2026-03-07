#pragma once
#include "phigros/chart/phbc_io.hpp"
#include <vector>
#include <cstdint>
#include <string>

namespace phigros::chart {

// Derive a 32-byte encryption key from a password and salt using PBKDF2-SHA256.
// `iterations` defaults to 100000.
// Returns 32 bytes (256 bits).
std::vector<uint8_t> phbc_derive_key(const std::string& password,
                                     const uint8_t salt[16],
                                     int iterations = 100000);

// Generate cryptographically secure random bytes.
void phbc_random_bytes(uint8_t* buf, size_t len);

// Encrypt plaintext with the given algorithm, key, and IV.
// Fills `meta` with salt, IV, and auth tag (for AEAD algorithms).
// Returns ciphertext.
std::vector<uint8_t> phbc_encrypt(const std::vector<uint8_t>& plaintext,
                                  EncryptionAlgo algo,
                                  const std::string& password,
                                  PhbcCryptoMeta& meta);

// Decrypt ciphertext with the given algorithm, key, and IV.
// Uses `meta` for salt, IV, and auth tag verification.
// Throws std::runtime_error on decryption failure (wrong password, tampered data).
std::vector<uint8_t> phbc_decrypt(const std::vector<uint8_t>& ciphertext,
                                  EncryptionAlgo algo,
                                  const std::string& password,
                                  const PhbcCryptoMeta& meta);

// Check if a given encryption algorithm is available in this build.
bool phbc_encryption_available(EncryptionAlgo algo);

} // namespace phigros::chart
