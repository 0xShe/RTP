#include "mutated.h"
#ifndef RTP_CRYPTO_H
#define RTP_CRYPTO_H

#include <stddef.h>

// RC4 State
typedef struct {
    unsigned char S[256];
    int i, j;
} rc4_state_t;

void rc4_init(rc4_state_t *state, const unsigned char *key, size_t keylen);
void rc4_crypt(rc4_state_t *state, unsigned char *data, size_t len);

// Obfuscation (Jitter/TLS Padding)
size_t obfs_tls_wrap(unsigned char *out_buf, const unsigned char *in_buf, size_t in_len);
size_t obfs_tls_unwrap(unsigned char *out_buf, const unsigned char *in_buf, size_t in_len);

#endif // RTP_CRYPTO_H
