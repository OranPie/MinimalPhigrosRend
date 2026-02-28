from __future__ import annotations

import hashlib
import os
from dataclasses import dataclass
from typing import Optional, Tuple


CRYPT_NONE = 0
CRYPT_CHACHA20POLY1305_SCRYPT = 1


@dataclass
class CryptoParams:
    crypt_id: int
    salt: bytes
    n: int
    r: int
    p: int


def _require_crypto():
    try:
        from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305  # noqa: F401
    except Exception as e:
        raise RuntimeError(
            "PCC encryption requires 'cryptography' package. Install cryptography to use password encryption."
        ) from e


def derive_key(password: str, params: CryptoParams) -> bytes:
    pw = password.encode('utf-8')
    return hashlib.scrypt(pw, salt=params.salt, n=int(params.n), r=int(params.r), p=int(params.p), dklen=32)


def encrypt_aead(password: str, params: CryptoParams, plaintext: bytes, aad: bytes) -> Tuple[bytes, bytes]:
    _require_crypto()
    from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

    key = derive_key(password, params)
    nonce = os.urandom(12)
    aead = ChaCha20Poly1305(key)
    ct = aead.encrypt(nonce, plaintext, aad)
    return nonce, ct


def decrypt_aead(password: str, params: CryptoParams, nonce: bytes, ciphertext: bytes, aad: bytes) -> bytes:
    _require_crypto()
    from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

    key = derive_key(password, params)
    aead = ChaCha20Poly1305(key)
    return aead.decrypt(nonce, ciphertext, aad)


def default_params() -> CryptoParams:
    return CryptoParams(
        crypt_id=CRYPT_CHACHA20POLY1305_SCRYPT,
        salt=os.urandom(16),
        n=2**15,
        r=8,
        p=1,
    )
