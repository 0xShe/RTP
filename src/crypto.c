#include "crypto.h"
#include <stdlib.h>
#include <string.h>

static void swap(unsigned char *a, unsigned char *b) {
    unsigned char tmp = *a;
    *a = *b;
    *b = tmp;
}

void rc4_init(rc4_state_t *state, const unsigned char *key, size_t keylen) {
    for (int i = 0; i < 256; i++) {
        state->S[i] = i;
    }
    int j = 0;
    for (int i = 0; i < 256; i++) {
        j = (j + state->S[i] + key[i % keylen]) % 256;
        swap(&state->S[i], &state->S[j]);
    }
    state->i = 0;
    state->j = 0;
}

void rc4_crypt(rc4_state_t *state, unsigned char *data, size_t len) {
    int i = state->i;
    int j = state->j;
    for (size_t k = 0; k < len; k++) {
        i = (i + 1) % 256;
        j = (j + state->S[i]) % 256;
        swap(&state->S[i], &state->S[j]);
        unsigned char K = state->S[(state->S[i] + state->S[j]) % 256];
        data[k] ^= K;
    }
    state->i = i;
    state->j = j;
}

// TLS 1.2 Application Data Record (Type 23, Version 0x0303)
static const unsigned char TLS_APP_DATA_HEADER[] = {0x17, 0x03, 0x03};

size_t obfs_tls_wrap(unsigned char *out_buf, const unsigned char *in_buf, size_t in_len) {
    // 5 bytes header: [Type(1)] [Version(2)] [Length(2)]
    // N bytes jitter padding (optional, kept simple for now)
    size_t total_len = in_len; 
    
    memcpy(out_buf, TLS_APP_DATA_HEADER, 3);
    out_buf[3] = (total_len >> 8) & 0xFF;
    out_buf[4] = total_len & 0xFF;
    
    memcpy(out_buf + 5, in_buf, in_len);
    return total_len + 5;
}

size_t obfs_tls_unwrap(unsigned char *out_buf, const unsigned char *in_buf, size_t in_len) {
    if (in_len < 5) return 0;
    // Verify it looks like our fake TLS header
    if (in_buf[0] != 0x17 || in_buf[1] != 0x03 || in_buf[2] != 0x03) {
        // Not wrapped or corrupted, fallback to raw
        memcpy(out_buf, in_buf, in_len);
        return in_len;
    }
    
    size_t payload_len = (in_buf[3] << 8) | in_buf[4];
    if (payload_len > in_len - 5) return 0; // Incomplete packet (needs buffering in real world)
    
    memcpy(out_buf, in_buf + 5, payload_len);
    return payload_len;
}
