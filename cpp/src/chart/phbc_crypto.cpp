#include "phigros/chart/phbc_crypto.hpp"
#include <stdexcept>
#include <cstring>
#include <random>

#ifdef PHIGROS_HAS_OPENSSL
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#endif

namespace phigros::chart {

// ── Random bytes ────────────────────────────────────────────────────────────

void phbc_random_bytes(uint8_t* buf, size_t len) {
#ifdef PHIGROS_HAS_OPENSSL
    if (RAND_bytes(buf, static_cast<int>(len)) != 1)
        throw std::runtime_error("phbc_random_bytes: RAND_bytes failed");
#else
    // Fallback: use std::random_device (sufficient for XOR obfuscation)
    std::random_device rd;
    for (size_t i = 0; i < len; i += 4) {
        uint32_t val = rd();
        size_t to_copy = std::min(len - i, size_t(4));
        std::memcpy(buf + i, &val, to_copy);
    }
#endif
}

// ── PBKDF2 key derivation ───────────────────────────────────────────────────

std::vector<uint8_t> phbc_derive_key(const std::string& password,
                                     const uint8_t salt[16],
                                     int iterations) {
#ifdef PHIGROS_HAS_OPENSSL
    std::vector<uint8_t> key(32);
    if (!PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                            salt, 16, iterations,
                            EVP_sha256(), 32, key.data()))
        throw std::runtime_error("phbc_derive_key: PBKDF2 failed");
    return key;
#else
    // Without OpenSSL, derive a simple key via repeated hashing (XOR only)
    std::vector<uint8_t> key(32, 0);
    for (size_t i = 0; i < password.size(); ++i)
        key[i % 32] ^= static_cast<uint8_t>(password[i]);
    for (size_t i = 0; i < 16; ++i)
        key[i] ^= salt[i];
    // Mix rounds
    for (int r = 0; r < 256; ++r)
        for (int i = 0; i < 32; ++i)
            key[i] = static_cast<uint8_t>(key[i] ^ key[(i + 13) % 32] ^ (r + i));
    return key;
#endif
}

// ── XOR cipher (always available) ───────────────────────────────────────────

static std::vector<uint8_t> xor_crypt(const std::vector<uint8_t>& data,
                                      const std::vector<uint8_t>& key) {
    std::vector<uint8_t> out(data.size());
    for (size_t i = 0; i < data.size(); ++i)
        out[i] = data[i] ^ key[i % key.size()];
    return out;
}

// ── OpenSSL AEAD / CBC wrappers ─────────────────────────────────────────────

#ifdef PHIGROS_HAS_OPENSSL

static std::string openssl_error() {
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    return buf;
}

// Generic AEAD encrypt (GCM or ChaCha20-Poly1305)
static std::vector<uint8_t> aead_encrypt(const EVP_CIPHER* cipher,
                                         const std::vector<uint8_t>& plaintext,
                                         const std::vector<uint8_t>& key,
                                         const uint8_t iv[], int iv_len,
                                         uint8_t tag[16]) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("aead_encrypt: CTX_new failed");

    std::vector<uint8_t> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
    int out_len = 0, final_len = 0;

    if (EVP_EncryptInit_ex(ctx, cipher, nullptr, key.data(), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aead_encrypt: EncryptInit failed: " + openssl_error());
    }
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, iv_len, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aead_encrypt: set IV len failed");
    }

    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &out_len,
                          plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aead_encrypt: EncryptUpdate failed");
    }
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + out_len, &final_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aead_encrypt: EncryptFinal failed");
    }
    ciphertext.resize(out_len + final_len);

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16, tag) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aead_encrypt: get tag failed");
    }
    EVP_CIPHER_CTX_free(ctx);
    return ciphertext;
}

// Generic AEAD decrypt
static std::vector<uint8_t> aead_decrypt(const EVP_CIPHER* cipher,
                                         const std::vector<uint8_t>& ciphertext,
                                         const std::vector<uint8_t>& key,
                                         const uint8_t iv[], int iv_len,
                                         const uint8_t tag[16]) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("aead_decrypt: CTX_new failed");

    std::vector<uint8_t> plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int out_len = 0, final_len = 0;

    if (EVP_DecryptInit_ex(ctx, cipher, nullptr, key.data(), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aead_decrypt: DecryptInit failed");
    }
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, iv_len, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aead_decrypt: set IV len failed");
    }

    if (EVP_DecryptUpdate(ctx, plaintext.data(), &out_len,
                          ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aead_decrypt: DecryptUpdate failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, 16,
                            const_cast<uint8_t*>(tag)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aead_decrypt: set tag failed");
    }

    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + out_len, &final_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aead_decrypt: authentication failed (wrong password or tampered data)");
    }
    plaintext.resize(out_len + final_len);
    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
}

// AES-256-CBC encrypt (with PKCS7 padding)
static std::vector<uint8_t> cbc_encrypt(const std::vector<uint8_t>& plaintext,
                                        const std::vector<uint8_t>& key,
                                        const uint8_t iv[16]) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("cbc_encrypt: CTX_new failed");

    std::vector<uint8_t> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
    int out_len = 0, final_len = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("cbc_encrypt: EncryptInit failed");
    }
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &out_len,
                          plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("cbc_encrypt: EncryptUpdate failed");
    }
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + out_len, &final_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("cbc_encrypt: EncryptFinal failed");
    }
    ciphertext.resize(out_len + final_len);
    EVP_CIPHER_CTX_free(ctx);
    return ciphertext;
}

// AES-256-CBC decrypt
static std::vector<uint8_t> cbc_decrypt(const std::vector<uint8_t>& ciphertext,
                                        const std::vector<uint8_t>& key,
                                        const uint8_t iv[16]) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("cbc_decrypt: CTX_new failed");

    std::vector<uint8_t> plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int out_len = 0, final_len = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("cbc_decrypt: DecryptInit failed");
    }
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &out_len,
                          ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("cbc_decrypt: DecryptUpdate failed");
    }
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + out_len, &final_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("cbc_decrypt: decryption failed (wrong password or tampered data)");
    }
    plaintext.resize(out_len + final_len);
    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
}

#endif // PHIGROS_HAS_OPENSSL

// ── Public API ──────────────────────────────────────────────────────────────

bool phbc_encryption_available(EncryptionAlgo algo) {
    if (algo == EncryptionAlgo::XOR) return true;
#ifdef PHIGROS_HAS_OPENSSL
    return true;
#else
    return false;
#endif
}

std::vector<uint8_t> phbc_encrypt(const std::vector<uint8_t>& plaintext,
                                  EncryptionAlgo algo,
                                  const std::string& password,
                                  PhbcCryptoMeta& meta) {
    // Generate random salt and IV
    phbc_random_bytes(meta.salt, 16);
    phbc_random_bytes(meta.iv, 16);
    std::memset(meta.tag, 0, 16);

    // Derive key
    auto key = phbc_derive_key(password, meta.salt);

    switch (algo) {
        case EncryptionAlgo::XOR:
            return xor_crypt(plaintext, key);

#ifdef PHIGROS_HAS_OPENSSL
        case EncryptionAlgo::AES_256_GCM:
            return aead_encrypt(EVP_aes_256_gcm(), plaintext, key,
                                meta.iv, 12, meta.tag);

        case EncryptionAlgo::AES_256_CBC:
            return cbc_encrypt(plaintext, key, meta.iv);

        case EncryptionAlgo::ChaCha20_Poly1305:
            return aead_encrypt(EVP_chacha20_poly1305(), plaintext, key,
                                meta.iv, 12, meta.tag);
#else
        case EncryptionAlgo::AES_256_GCM:
        case EncryptionAlgo::AES_256_CBC:
        case EncryptionAlgo::ChaCha20_Poly1305:
            throw std::runtime_error(
                std::string("phbc_encrypt: ") + encryption_name(algo) +
                " requires OpenSSL (build with -DUSE_ENCRYPTION=ON)");
#endif
    }
    throw std::runtime_error("phbc_encrypt: unknown algorithm");
}

std::vector<uint8_t> phbc_decrypt(const std::vector<uint8_t>& ciphertext,
                                  EncryptionAlgo algo,
                                  const std::string& password,
                                  const PhbcCryptoMeta& meta) {
    auto key = phbc_derive_key(password, meta.salt);

    switch (algo) {
        case EncryptionAlgo::XOR:
            return xor_crypt(ciphertext, key);

#ifdef PHIGROS_HAS_OPENSSL
        case EncryptionAlgo::AES_256_GCM:
            return aead_decrypt(EVP_aes_256_gcm(), ciphertext, key,
                                meta.iv, 12, meta.tag);

        case EncryptionAlgo::AES_256_CBC:
            return cbc_decrypt(ciphertext, key, meta.iv);

        case EncryptionAlgo::ChaCha20_Poly1305:
            return aead_decrypt(EVP_chacha20_poly1305(), ciphertext, key,
                                meta.iv, 12, meta.tag);
#else
        case EncryptionAlgo::AES_256_GCM:
        case EncryptionAlgo::AES_256_CBC:
        case EncryptionAlgo::ChaCha20_Poly1305:
            throw std::runtime_error(
                std::string("phbc_decrypt: ") + encryption_name(algo) +
                " requires OpenSSL (build with -DUSE_ENCRYPTION=ON)");
#endif
    }
    throw std::runtime_error("phbc_decrypt: unknown algorithm");
}

} // namespace phigros::chart
