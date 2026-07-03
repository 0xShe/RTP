#include "mutated.h"
#ifndef RTP_H
#define RTP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define RTP_VERSION "1.0.0"

/* RTP Core Configuration Context */
typedef struct {
    int mode;           // 0: fwd, 1: proxy
    char *local_addr;   // -l
    int local_is_tunnel;
    char *local_addr2;  // -l (second) for L-L
    int local2_is_tunnel;
    char *remote_addr;  // -r
    int remote_is_tunnel;
    char *remote_addr2; // -r (second) for R-R
    int remote2_is_tunnel;
    char *crypto_key;   // -k
    char *socks_user;   // --user
    char *socks_pass;   // --pass
    bool silent;        // -q
    bool use_udp;       // -u
    bool tls_obfs;      // --tls
    bool auto_reconnect;// --reconnect
    bool reverse_socks; // --reverse-socks
} rtp_config_t;

/* Global configuration instance */
extern rtp_config_t g_config;

/* Core Function Prototypes */
void rtp_init(void);
void rtp_run(void);
void rtp_cleanup(void);

#endif // RTP_H
