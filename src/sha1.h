#ifndef KTERM_SHA1_H
#define KTERM_SHA1_H

#include <stddef.h>
#include <stdint.h>

/*
 * Public-domain SHA1 (classic Steve Reid implementation), vendored just to
 * compute the WebSocket handshake's Sec-WebSocket-Accept header (RFC 6455):
 * base64(SHA1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")).
 */

typedef struct {
  uint32_t state[5];
  uint32_t count[2];
  unsigned char buffer[64];
} SHA1_CTX;

void SHA1Init(SHA1_CTX *ctx);
void SHA1Update(SHA1_CTX *ctx, const unsigned char *data, size_t len);
void SHA1Final(unsigned char digest[20], SHA1_CTX *ctx);

#endif /* KTERM_SHA1_H */
