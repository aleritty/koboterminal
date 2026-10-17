/*
 * Public-domain SHA1 (classic Steve Reid implementation).
 */

#include "sha1.h"

#include <string.h>

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

static void SHA1Transform(uint32_t state[5], const unsigned char buffer[64]) {
  uint32_t a, b, c, d, e;
  uint32_t block[80];
  int i;

  for (i = 0; i < 16; i++) {
    block[i] = ((uint32_t)buffer[i * 4] << 24) |
               ((uint32_t)buffer[i * 4 + 1] << 16) |
               ((uint32_t)buffer[i * 4 + 2] << 8) |
               ((uint32_t)buffer[i * 4 + 3]);
  }
  for (i = 16; i < 80; i++) {
    block[i] = rol(block[i - 3] ^ block[i - 8] ^ block[i - 14] ^ block[i - 16], 1);
  }

  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];
  e = state[4];

  for (i = 0; i < 80; i++) {
    uint32_t f, k, temp;
    if (i < 20) {
      f = (b & c) | ((~b) & d);
      k = 0x5A827999u;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1u;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDCu;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6u;
    }

    temp = rol(a, 5) + f + e + k + block[i];
    e = d;
    d = c;
    c = rol(b, 30);
    b = a;
    a = temp;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
}

void SHA1Init(SHA1_CTX *ctx) {
  ctx->state[0] = 0x67452301u;
  ctx->state[1] = 0xEFCDAB89u;
  ctx->state[2] = 0x98BADCFEu;
  ctx->state[3] = 0x10325476u;
  ctx->state[4] = 0xC3D2E1F0u;
  ctx->count[0] = ctx->count[1] = 0;
}

void SHA1Update(SHA1_CTX *ctx, const unsigned char *data, size_t len) {
  size_t i, j, partLen;

  j = (size_t)((ctx->count[0] >> 3) & 63);
  if ((ctx->count[0] += (uint32_t)(len << 3)) < (uint32_t)(len << 3)) {
    ctx->count[1]++;
  }
  ctx->count[1] += (uint32_t)(len >> 29);

  partLen = 64 - j;
  if (len >= partLen) {
    memcpy(&ctx->buffer[j], data, partLen);
    SHA1Transform(ctx->state, ctx->buffer);
    for (i = partLen; i + 63 < len; i += 64) {
      SHA1Transform(ctx->state, &data[i]);
    }
    j = 0;
  } else {
    i = 0;
  }
  memcpy(&ctx->buffer[j], &data[i], len - i);
}

void SHA1Final(unsigned char digest[20], SHA1_CTX *ctx) {
  unsigned char finalcount[8];
  unsigned char c;
  int i;

  for (i = 0; i < 8; i++) {
    finalcount[i] = (unsigned char)((ctx->count[(i >= 4) ? 0 : 1] >>
                                      ((3 - (i & 3)) * 8)) & 255);
  }

  c = 0x80;
  SHA1Update(ctx, &c, 1);
  while ((ctx->count[0] & 504) != 448) {
    c = 0x00;
    SHA1Update(ctx, &c, 1);
  }
  SHA1Update(ctx, finalcount, 8);

  for (i = 0; i < 20; i++) {
    digest[i] = (unsigned char)((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
  }
}
