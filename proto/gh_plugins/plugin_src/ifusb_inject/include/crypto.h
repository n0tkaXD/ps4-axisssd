/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Crypto primitives. */
#pragma once

#include <stddef.h>
#include <stdint.h>

void sha256(const void *data, size_t len, uint8_t out[32]);

typedef struct {
    uint32_t state[8];
    uint64_t total;
    uint8_t buf[64];
    size_t used;
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t len);
void sha256_final(sha256_ctx *c, uint8_t out[32]);

/* AES-128-CBC decryption in place; len must be a multiple of 16. */
void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t *data, size_t len);

uint32_t crc32c(const void *data, size_t len);

/* out = in^d mod n for 2048-bit big-endian numbers. */
void rsa2048_modexp(const uint8_t in[256], const uint8_t d[256], const uint8_t n[256], uint8_t out[256]);
