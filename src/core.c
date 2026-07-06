#include "core.h"
#include "rtp.h"
#include "log.h"
#include "net.h"
#include "crypto.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define MAX_FDS 1024
#define MAX_UDP_SESSIONS 256
#define BUFFER_SIZE 8192
#define MAX_PENDING_BUFFER (BUFFER_SIZE * 8)
#define MAX_TUNNEL_BUFFER (BUFFER_SIZE * 8)
#define UDP_SESSION_TIMEOUT 60
#define REVERSE_SOCKS_HELLO_LEN 6
#define REVERSE_SOCKS_POOL_SIZE 8

enum {
    SESSION_STATE_FWD = 0,
    SESSION_STATE_SOCKS5_INIT = 1,
    SESSION_STATE_SOCKS5_AUTH = 2,
    SESSION_STATE_SOCKS5_CMD = 3,
    SESSION_STATE_SOCKS5_FWD = 4,
    SESSION_STATE_SOCKS5_CONNECTING = 5
};

enum {
    REVERSE_SOCKS_ROLE_NONE = 0,
    REVERSE_SOCKS_ROLE_SERVER_PRIMARY = 1,
    REVERSE_SOCKS_ROLE_ACTIVE_TARGET = 2
};

static const unsigned char k_reverse_socks_hello[REVERSE_SOCKS_HELLO_LEN] = {'R', 'S', 'O', 'C', 'K', '1'};

typedef struct {
    SOCKET_TYPE client_fd;
    int client_is_tunnel;
    int client_connecting;
    SOCKET_TYPE remote_fd;
    int remote_is_tunnel;
    int remote_connecting;
    int active;
    int state;
    int close_after_client_flush;
    int reverse_socks_role;
    int reverse_socks_hello_sent;
    int reverse_socks_hello_verified;
    int reverse_socks_paired;
    int connect_announced;
    size_t reverse_socks_hello_len;
    unsigned char reverse_socks_hello_buf[REVERSE_SOCKS_HELLO_LEN];
    rc4_state_t *client_rc4_in;
    rc4_state_t *client_rc4_out;
    rc4_state_t *remote_rc4_in;
    rc4_state_t *remote_rc4_out;
    unsigned char client_tunnel_buf[MAX_TUNNEL_BUFFER];
    size_t client_tunnel_len;
    unsigned char remote_tunnel_buf[MAX_TUNNEL_BUFFER];
    size_t remote_tunnel_len;
    unsigned char to_client_buf[MAX_PENDING_BUFFER];
    size_t to_client_len;
    size_t to_client_off;
    unsigned char to_remote_buf[MAX_PENDING_BUFFER];
    size_t to_remote_len;
    size_t to_remote_off;
} session_t;

typedef struct {
    SOCKET_TYPE remote_fd;
    struct sockaddr_in peer_addr;
    socklen_t peer_len;
    int active;
    int client_is_tunnel;
    int remote_is_tunnel;
    time_t last_activity;
    rc4_state_t *client_rc4_in;
    rc4_state_t *client_rc4_out;
    rc4_state_t *remote_rc4_in;
    rc4_state_t *remote_rc4_out;
    unsigned char to_remote_buf[MAX_PENDING_BUFFER];
    size_t to_remote_len;
    size_t to_remote_off;
} udp_session_t;

static session_t g_sessions[MAX_FDS];
static udp_session_t g_udp_sessions[MAX_UDP_SESSIONS];
static time_t g_last_reconnect_time = 0;
static time_t g_last_reverse_socks_pool_attempt = 0;
static size_t pending_size(size_t len, size_t off);
static void close_session(int i);
static void describe_socket_endpoints(SOCKET_TYPE fd,
                                      char *local_buf, size_t local_buf_len,
                                      char *peer_buf, size_t peer_buf_len);

/* #region debug-point session-close-reason */
static const char *session_state_name(int state) {
    switch (state) {
        case SESSION_STATE_FWD:
            return "FWD";
        case SESSION_STATE_SOCKS5_INIT:
            return "SOCKS5_INIT";
        case SESSION_STATE_SOCKS5_AUTH:
            return "SOCKS5_AUTH";
        case SESSION_STATE_SOCKS5_CMD:
            return "SOCKS5_CMD";
        case SESSION_STATE_SOCKS5_FWD:
            return "SOCKS5_FWD";
        case SESSION_STATE_SOCKS5_CONNECTING:
            return "SOCKS5_CONNECTING";
        default:
            return "UNKNOWN";
    }
}

static void debug_log_session_snapshot(int i, const char *reason) {
    (void)i;
    (void)reason;
}

static void debug_close_session(int i, const char *reason) {
    debug_log_session_snapshot(i, reason);
    close_session(i);
}
/* #endregion debug-point session-close-reason */

static void reset_session_slot(int i) {
    memset(&g_sessions[i], 0, sizeof(g_sessions[i]));
    g_sessions[i].client_fd = INVALID_SOCKET;
    g_sessions[i].remote_fd = INVALID_SOCKET;
}

static void reset_udp_session_slot(int i) {
    memset(&g_udp_sessions[i], 0, sizeof(g_udp_sessions[i]));
    g_udp_sessions[i].remote_fd = INVALID_SOCKET;
    g_udp_sessions[i].peer_len = sizeof(g_udp_sessions[i].peer_addr);
}

static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

static int socket_error_is_wouldblock(void) {
#ifdef _WIN32
    int err = WSAGetLastError();
    return err == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static int socket_error_is_interrupted(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

static int socket_last_error_code(void) {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static int get_socket_so_error(SOCKET_TYPE fd, int *out_err) {
#ifdef _WIN32
    int err = 0;
    int len = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&err, &len) != 0) {
        return -1;
    }
#else
    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
        return -1;
    }
#endif
    *out_err = err;
    return 0;
}

/* #region debug-point socket-endpoints */
static void format_sockaddr_ipv4(const struct sockaddr_in *addr, char *buf, size_t buf_len) {
    char ip[INET_ADDRSTRLEN];

    if (!addr || !buf || buf_len == 0) {
        return;
    }
    if (rtp_inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip)) == NULL) {
        snprintf(buf, buf_len, "unknown");
        return;
    }
    snprintf(buf, buf_len, "%s:%u", ip, (unsigned int)ntohs(addr->sin_port));
}

static void describe_socket_endpoints(SOCKET_TYPE fd,
                                      char *local_buf, size_t local_buf_len,
                                      char *peer_buf, size_t peer_buf_len) {
    struct sockaddr_in local_addr;
    struct sockaddr_in peer_addr;
#ifdef _WIN32
    int local_len = sizeof(local_addr);
    int peer_len = sizeof(peer_addr);
#else
    socklen_t local_len = sizeof(local_addr);
    socklen_t peer_len = sizeof(peer_addr);
#endif

    snprintf(local_buf, local_buf_len, "n/a");
    snprintf(peer_buf, peer_buf_len, "n/a");

    if (fd == INVALID_SOCKET) {
        return;
    }
    if (getsockname(fd, (struct sockaddr*)&local_addr, &local_len) == 0) {
        format_sockaddr_ipv4(&local_addr, local_buf, local_buf_len);
    }
    if (getpeername(fd, (struct sockaddr*)&peer_addr, &peer_len) == 0) {
        format_sockaddr_ipv4(&peer_addr, peer_buf, peer_buf_len);
    }
}

static void log_session_endpoints(int i, const char *tag) {
    (void)i;
    (void)tag;
}

static void announce_session_connected(int i) {
    struct sockaddr_in peer_addr;
    socklen_t peer_len = sizeof(peer_addr);
    const char *fmt = NULL;

    if (g_sessions[i].connect_announced) {
        return;
    }

    if (g_config.local_addr &&
        g_sessions[i].client_fd != INVALID_SOCKET &&
        getpeername(g_sessions[i].client_fd, (struct sockaddr*)&peer_addr, &peer_len) == 0) {
        fmt = "Connection accepted from %s:%d";
    } else if (g_sessions[i].remote_fd != INVALID_SOCKET &&
               getpeername(g_sessions[i].remote_fd, (struct sockaddr*)&peer_addr, &peer_len) == 0) {
        fmt = "Connection established to %s:%d";
    } else if (!g_config.local_addr &&
               g_sessions[i].client_fd != INVALID_SOCKET &&
               getpeername(g_sessions[i].client_fd, (struct sockaddr*)&peer_addr, &peer_len) == 0) {
        fmt = "Connection established to %s:%d";
    }

    if (fmt == NULL) {
        return;
    }

    g_sessions[i].connect_announced = 1;
    LOG_SUCCESS(fmt, inet_ntoa(peer_addr.sin_addr), ntohs(peer_addr.sin_port));
}

static int reverse_socks_pool_connect_announced(void) {
    int j;

    for (j = 0; j < MAX_FDS; j++) {
        if (g_sessions[j].active &&
            g_sessions[j].reverse_socks_role == REVERSE_SOCKS_ROLE_ACTIVE_TARGET &&
            g_sessions[j].connect_announced) {
            return 1;
        }
    }
    return 0;
}

static void announce_reverse_socks_pool_connected(int i) {
    if (reverse_socks_pool_connect_announced()) {
        return;
    }
    g_sessions[i].connect_announced = 1;
    LOG_SUCCESS("Connection established to %s", g_config.remote_addr);
}

static int reverse_socks_server_enabled(void) {
    return g_config.reverse_socks &&
           !g_config.use_udp &&
           g_config.local_addr != NULL &&
           g_config.local_addr2 != NULL &&
           g_config.remote_addr == NULL;
}

static int reverse_socks_active_enabled(void) {
    return g_config.reverse_socks &&
           !g_config.use_udp &&
           g_config.remote_addr != NULL &&
           g_config.local_addr == NULL &&
           g_config.remote_addr2 == NULL;
}

static int session_waits_for_reverse_socks_hello(int i) {
    return g_sessions[i].reverse_socks_role == REVERSE_SOCKS_ROLE_SERVER_PRIMARY &&
           !g_sessions[i].reverse_socks_hello_verified;
}

static int reverse_socks_waiting_pool_count(void) {
    int i;
    int count = 0;

    for (i = 0; i < MAX_FDS; i++) {
        if (!g_sessions[i].active) {
            continue;
        }
        if (g_sessions[i].reverse_socks_role != REVERSE_SOCKS_ROLE_ACTIVE_TARGET) {
            continue;
        }
        if (g_sessions[i].reverse_socks_paired) {
            continue;
        }
        count++;
    }
    return count;
}

static void log_unpaired_primary_payload_hint(int i, const unsigned char *buffer, size_t len) {
    (void)i;
    (void)buffer;
    (void)len;
}
/* #endregion debug-point socket-endpoints */

static size_t pending_size(size_t len, size_t off) {
    return len > off ? len - off : 0;
}

static int has_pending_to_client(int i) {
    return pending_size(g_sessions[i].to_client_len, g_sessions[i].to_client_off) > 0;
}

static int has_pending_to_remote(int i) {
    return pending_size(g_sessions[i].to_remote_len, g_sessions[i].to_remote_off) > 0;
}

static void free_rc4_state(rc4_state_t **state) {
    if (*state) {
        free(*state);
        *state = NULL;
    }
}

static void free_session_crypto(int i) {
    free_rc4_state(&g_sessions[i].client_rc4_in);
    free_rc4_state(&g_sessions[i].client_rc4_out);
    free_rc4_state(&g_sessions[i].remote_rc4_in);
    free_rc4_state(&g_sessions[i].remote_rc4_out);
}

static void free_udp_session_crypto(int i) {
    free_rc4_state(&g_udp_sessions[i].client_rc4_in);
    free_rc4_state(&g_udp_sessions[i].client_rc4_out);
    free_rc4_state(&g_udp_sessions[i].remote_rc4_in);
    free_rc4_state(&g_udp_sessions[i].remote_rc4_out);
}

static int init_endpoint_crypto(rc4_state_t **rx_state, rc4_state_t **tx_state) {
    *rx_state = malloc(sizeof(rc4_state_t));
    *tx_state = malloc(sizeof(rc4_state_t));
    if (!*rx_state || !*tx_state) {
        free_rc4_state(rx_state);
        free_rc4_state(tx_state);
        return -1;
    }

    rc4_init(*rx_state, (unsigned char*)g_config.crypto_key, strlen(g_config.crypto_key));
    rc4_init(*tx_state, (unsigned char*)g_config.crypto_key, strlen(g_config.crypto_key));
    return 0;
}

static int init_session_crypto(int i) {
    if (!g_config.crypto_key) {
        free_session_crypto(i);
        return 0;
    }

    free_session_crypto(i);
    if (g_sessions[i].client_is_tunnel &&
        init_endpoint_crypto(&g_sessions[i].client_rc4_in, &g_sessions[i].client_rc4_out) < 0) {
        LOG_ERR("Failed to allocate client RC4 state for session [%d]", i);
        free_session_crypto(i);
        return -1;
    }

    if (g_sessions[i].remote_is_tunnel &&
        init_endpoint_crypto(&g_sessions[i].remote_rc4_in, &g_sessions[i].remote_rc4_out) < 0) {
        LOG_ERR("Failed to allocate remote RC4 state for session [%d]", i);
        free_session_crypto(i);
        return -1;
    }
    return 0;
}

static int init_udp_session_crypto(int i) {
    if (!g_config.crypto_key) {
        free_udp_session_crypto(i);
        return 0;
    }

    free_udp_session_crypto(i);
    if (g_udp_sessions[i].client_is_tunnel &&
        init_endpoint_crypto(&g_udp_sessions[i].client_rc4_in, &g_udp_sessions[i].client_rc4_out) < 0) {
        LOG_ERR("Failed to allocate UDP client RC4 state for session [%d]", i);
        free_udp_session_crypto(i);
        return -1;
    }

    if (g_udp_sessions[i].remote_is_tunnel &&
        init_endpoint_crypto(&g_udp_sessions[i].remote_rc4_in, &g_udp_sessions[i].remote_rc4_out) < 0) {
        LOG_ERR("Failed to allocate UDP remote RC4 state for session [%d]", i);
        free_udp_session_crypto(i);
        return -1;
    }
    return 0;
}

static int queue_pending(unsigned char *buf, size_t *len, size_t *off, const unsigned char *data, size_t data_len) {
    size_t live = pending_size(*len, *off);

    if (*off > 0 && live > 0) {
        memmove(buf, buf + *off, live);
        *len = live;
        *off = 0;
    } else if (live == 0) {
        *len = 0;
        *off = 0;
    }

    if (MAX_PENDING_BUFFER - *len < data_len) {
        return -1;
    }

    memcpy(buf + *len, data, data_len);
    *len += data_len;
    return 0;
}

static int flush_pending_buffer(SOCKET_TYPE fd, unsigned char *buf, size_t *len, size_t *off) {
    while (*off < *len) {
        int sent = send(fd, (const char*)buf + *off, (int)(*len - *off), 0);
        if (sent > 0) {
            *off += (size_t)sent;
            continue;
        }
        if (sent == 0) {
            return -1;
        }
        if (socket_error_is_interrupted()) {
            continue;
        }
        if (socket_error_is_wouldblock()) {
            return 0;
        }
        return -1;
    }

    *len = 0;
    *off = 0;
    return 1;
}

static int append_tunnel_data(unsigned char *stage, size_t *stage_len, const unsigned char *data, size_t data_len) {
    if (*stage_len + data_len > MAX_TUNNEL_BUFFER) {
        return -1;
    }
    memcpy(stage + *stage_len, data, data_len);
    *stage_len += data_len;
    return 0;
}

static int pop_tunnel_frame(unsigned char *stage, size_t *stage_len, unsigned char *out_buf, size_t *out_len) {
    size_t frame_len;
    size_t payload_len;

    if (*stage_len < 5) {
        return 0;
    }
    if (stage[0] != 0x17 || stage[1] != 0x03 || stage[2] != 0x03) {
        return -1;
    }

    payload_len = ((size_t)stage[3] << 8) | stage[4];
    frame_len = payload_len + 5;
    if (payload_len > BUFFER_SIZE || frame_len > MAX_TUNNEL_BUFFER) {
        return -1;
    }
    if (*stage_len < frame_len) {
        return 0;
    }

    memcpy(out_buf, stage + 5, payload_len);
    memmove(stage, stage + frame_len, *stage_len - frame_len);
    *stage_len -= frame_len;
    *out_len = payload_len;
    return 1;
}

static int can_read_from_client(int i) {
    if (g_sessions[i].client_fd == INVALID_SOCKET || g_sessions[i].client_connecting || g_sessions[i].close_after_client_flush) {
        return 0;
    }
    if (g_sessions[i].state == SESSION_STATE_SOCKS5_INIT ||
        g_sessions[i].state == SESSION_STATE_SOCKS5_AUTH ||
        g_sessions[i].state == SESSION_STATE_SOCKS5_CMD) {
        return 1;
    }
    return MAX_PENDING_BUFFER - pending_size(g_sessions[i].to_remote_len, g_sessions[i].to_remote_off) >= BUFFER_SIZE;
}

static int can_read_from_remote(int i) {
    if (g_sessions[i].remote_fd == INVALID_SOCKET || g_sessions[i].remote_connecting || g_sessions[i].close_after_client_flush) {
        return 0;
    }
    return MAX_PENDING_BUFFER - pending_size(g_sessions[i].to_client_len, g_sessions[i].to_client_off) >= BUFFER_SIZE;
}

static int send_session_payload(int i, int to_client, unsigned char *buffer, size_t len, unsigned char *scratch) {
    int is_tunnel = to_client ? g_sessions[i].client_is_tunnel : g_sessions[i].remote_is_tunnel;
    int connecting = to_client ? g_sessions[i].client_connecting : g_sessions[i].remote_connecting;
    SOCKET_TYPE fd = to_client ? g_sessions[i].client_fd : g_sessions[i].remote_fd;
    rc4_state_t *cipher = to_client ? g_sessions[i].client_rc4_out : g_sessions[i].remote_rc4_out;
    unsigned char *pending_buf = to_client ? g_sessions[i].to_client_buf : g_sessions[i].to_remote_buf;
    size_t *pending_len = to_client ? &g_sessions[i].to_client_len : &g_sessions[i].to_remote_len;
    size_t *pending_off = to_client ? &g_sessions[i].to_client_off : &g_sessions[i].to_remote_off;
    const unsigned char *send_buf = buffer;
    size_t send_len = len;

    if (is_tunnel) {
        if (cipher) {
            rc4_crypt(cipher, buffer, len);
        }
        if (g_config.tls_obfs) {
            send_len = obfs_tls_wrap(scratch, buffer, len);
            send_buf = scratch;
        }
    }

    if (fd == INVALID_SOCKET || connecting || pending_size(*pending_len, *pending_off) > 0) {
        return queue_pending(pending_buf, pending_len, pending_off, send_buf, send_len);
    }

    while (send_len > 0) {
        int sent = send(fd, (const char*)send_buf, (int)send_len, 0);
        if (sent > 0) {
            send_buf += sent;
            send_len -= (size_t)sent;
            continue;
        }
        if (sent == 0) {
            return -1;
        }
        if (socket_error_is_interrupted()) {
            continue;
        }
        if (socket_error_is_wouldblock()) {
            break;
        }
        return -1;
    }

    if (send_len == 0) {
        return 0;
    }
    return queue_pending(pending_buf, pending_len, pending_off, send_buf, send_len);
}

static int send_reverse_socks_hello(int i) {
    unsigned char hello[REVERSE_SOCKS_HELLO_LEN];
    unsigned char scratch[BUFFER_SIZE + 256];

    if (g_sessions[i].reverse_socks_role != REVERSE_SOCKS_ROLE_ACTIVE_TARGET ||
        g_sessions[i].reverse_socks_hello_sent) {
        return 0;
    }

    memcpy(hello, k_reverse_socks_hello, sizeof(hello));
    if (send_session_payload(i, 1, hello, sizeof(hello), scratch) < 0) {
        debug_close_session(i, "reverse-socks-hello-send-failed");
        return -1;
    }

    g_sessions[i].reverse_socks_hello_sent = 1;
    return 0;
}

static int handle_reverse_socks_hello_payload(int i, const unsigned char *buffer, size_t len) {
    size_t remaining;

    if (!session_waits_for_reverse_socks_hello(i) || g_sessions[i].remote_fd != INVALID_SOCKET) {
        return 0;
    }

    remaining = REVERSE_SOCKS_HELLO_LEN - g_sessions[i].reverse_socks_hello_len;
    if (len > remaining) {
        debug_close_session(i, "reverse-socks-hello-invalid-length");
        return -1;
    }

    memcpy(g_sessions[i].reverse_socks_hello_buf + g_sessions[i].reverse_socks_hello_len, buffer, len);
    g_sessions[i].reverse_socks_hello_len += len;

    if (memcmp(g_sessions[i].reverse_socks_hello_buf,
               k_reverse_socks_hello,
               g_sessions[i].reverse_socks_hello_len) != 0) {
        debug_close_session(i, "reverse-socks-hello-mismatch");
        return -1;
    }

    if (g_sessions[i].reverse_socks_hello_len < REVERSE_SOCKS_HELLO_LEN) {
        return 1;
    }

    g_sessions[i].reverse_socks_hello_verified = 1;
    return 1;
}

static void close_session(int i) {
    if (g_sessions[i].client_fd != INVALID_SOCKET) {
        CLOSE_SOCKET(g_sessions[i].client_fd);
    }
    if (g_sessions[i].remote_fd != INVALID_SOCKET) {
        CLOSE_SOCKET(g_sessions[i].remote_fd);
    }
    free_session_crypto(i);
    reset_session_slot(i);
}

static void close_udp_session(int i) {
    if (g_udp_sessions[i].remote_fd != INVALID_SOCKET) {
        CLOSE_SOCKET(g_udp_sessions[i].remote_fd);
    }
    free_udp_session_crypto(i);
    reset_udp_session_slot(i);
}

static int create_active_session_at(int slot,
                                    SOCKET_TYPE fd1, int fd1_is_tunnel, int fd1_connecting,
                                    SOCKET_TYPE fd2, int fd2_is_tunnel, int fd2_connecting, int state) {
    reset_session_slot(slot);
    g_sessions[slot].client_fd = fd1;
    g_sessions[slot].client_is_tunnel = fd1_is_tunnel;
    g_sessions[slot].client_connecting = fd1_connecting;
    g_sessions[slot].remote_fd = fd2;
    g_sessions[slot].remote_is_tunnel = fd2_is_tunnel;
    g_sessions[slot].remote_connecting = fd2_connecting;
    g_sessions[slot].active = 1;
    g_sessions[slot].state = state;
    if (reverse_socks_active_enabled()) {
        g_sessions[slot].reverse_socks_role = REVERSE_SOCKS_ROLE_ACTIVE_TARGET;
    }
    if (init_session_crypto(slot) < 0) {
        close_session(slot);
        return -1;
    }
    log_session_endpoints(slot, "active-session-created");
    if (send_reverse_socks_hello(slot) < 0) {
        return -1;
    }
    return 0;
}

static int create_active_session(SOCKET_TYPE fd1, int fd1_is_tunnel, int fd1_connecting,
                                 SOCKET_TYPE fd2, int fd2_is_tunnel, int fd2_connecting, int state) {
    return create_active_session_at(0, fd1, fd1_is_tunnel, fd1_connecting,
                                    fd2, fd2_is_tunnel, fd2_connecting, state);
}

static int alloc_session_slot(void) {
    int i;

    for (i = 0; i < MAX_FDS; i++) {
        if (!g_sessions[i].active) {
            return i;
        }
    }
    return -1;
}

static int create_reverse_socks_active_session(void) {
    SOCKET_TYPE outbound_fd;
    int slot;

    slot = alloc_session_slot();
    if (slot < 0) {
        LOG_WARN("Reverse SOCKS pool exhausted: no free session slot");
        return -1;
    }

    outbound_fd = net_connect(g_config.remote_addr, 1, 0);
    if (outbound_fd == INVALID_SOCKET) {
        LOG_WARN("Reverse SOCKS pool dial failed to %s", g_config.remote_addr);
        return -1;
    }

    if (create_active_session_at(slot,
                                 outbound_fd, g_config.remote_is_tunnel, 1,
                                 INVALID_SOCKET, 0, 0,
                                 SESSION_STATE_SOCKS5_INIT) < 0) {
        return -1;
    }

    return slot;
}

static int maintain_reverse_socks_primary_pool(void) {
    int waiting;
    int created = 0;
    time_t now;

    if (!reverse_socks_active_enabled()) {
        return 0;
    }

    waiting = reverse_socks_waiting_pool_count();
    if (waiting >= REVERSE_SOCKS_POOL_SIZE) {
        return 0;
    }

    now = time(NULL);
    if (now == g_last_reverse_socks_pool_attempt) {
        return 0;
    }
    g_last_reverse_socks_pool_attempt = now;

    while (waiting + created < REVERSE_SOCKS_POOL_SIZE) {
        if (create_reverse_socks_active_session() < 0) {
            break;
        }
        created++;
    }

    if (created > 0) {
        return created;
    }
    return waiting > 0 ? 0 : -1;
}

static int udp_has_pending_to_remote(int i) {
    return pending_size(g_udp_sessions[i].to_remote_len, g_udp_sessions[i].to_remote_off) > 0;
}

static int sockaddr_equal(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_family == b->sin_family &&
           a->sin_port == b->sin_port &&
           a->sin_addr.s_addr == b->sin_addr.s_addr;
}

static int alloc_udp_session_slot(void) {
    int i;

    for (i = 0; i < MAX_UDP_SESSIONS; i++) {
        if (!g_udp_sessions[i].active) {
            return i;
        }
    }
    return -1;
}

static int find_udp_session_by_peer(const struct sockaddr_in *peer_addr) {
    int i;

    for (i = 0; i < MAX_UDP_SESSIONS; i++) {
        if (g_udp_sessions[i].active && sockaddr_equal(&g_udp_sessions[i].peer_addr, peer_addr)) {
            return i;
        }
    }
    return -1;
}

static int process_udp_tunnel_in(int is_tunnel, rc4_state_t *cipher,
                                 unsigned char *buffer, size_t *len, unsigned char *scratch) {
    if (!is_tunnel) {
        return 0;
    }
    if (g_config.tls_obfs) {
        size_t out_len = obfs_tls_unwrap(scratch, buffer, *len);
        if (out_len == 0) {
            return -1;
        }
        memcpy(buffer, scratch, out_len);
        *len = out_len;
    }
    if (cipher) {
        rc4_crypt(cipher, buffer, *len);
    }
    return 0;
}

static int udp_queue_to_remote(int i, unsigned char *buffer, size_t len, unsigned char *scratch) {
    const unsigned char *send_buf = buffer;
    size_t send_len = len;

    if (g_udp_sessions[i].remote_is_tunnel) {
        if (g_udp_sessions[i].remote_rc4_out) {
            rc4_crypt(g_udp_sessions[i].remote_rc4_out, buffer, len);
        }
        if (g_config.tls_obfs) {
            send_len = obfs_tls_wrap(scratch, buffer, len);
            send_buf = scratch;
        }
    }

    if (g_udp_sessions[i].remote_fd == INVALID_SOCKET || udp_has_pending_to_remote(i)) {
        return queue_pending(g_udp_sessions[i].to_remote_buf,
                             &g_udp_sessions[i].to_remote_len,
                             &g_udp_sessions[i].to_remote_off,
                             send_buf,
                             send_len);
    }

    while (send_len > 0) {
        int sent = send(g_udp_sessions[i].remote_fd, (const char*)send_buf, (int)send_len, 0);
        if (sent > 0) {
            send_buf += sent;
            send_len -= (size_t)sent;
            continue;
        }
        if (sent == 0) {
            return -1;
        }
        if (socket_error_is_interrupted()) {
            continue;
        }
        if (socket_error_is_wouldblock()) {
            break;
        }
        return -1;
    }

    if (send_len == 0) {
        return 0;
    }
    return queue_pending(g_udp_sessions[i].to_remote_buf,
                         &g_udp_sessions[i].to_remote_len,
                         &g_udp_sessions[i].to_remote_off,
                         send_buf,
                         send_len);
}

static int udp_send_to_client(SOCKET_TYPE server_fd, int i, unsigned char *buffer, size_t len, unsigned char *scratch) {
    const unsigned char *send_buf = buffer;
    size_t send_len = len;

    if (g_udp_sessions[i].client_is_tunnel) {
        if (g_udp_sessions[i].client_rc4_out) {
            rc4_crypt(g_udp_sessions[i].client_rc4_out, buffer, len);
        }
        if (g_config.tls_obfs) {
            send_len = obfs_tls_wrap(scratch, buffer, len);
            send_buf = scratch;
        }
    }

    if (sendto(server_fd,
               (const char*)send_buf,
               (int)send_len,
               0,
               (struct sockaddr*)&g_udp_sessions[i].peer_addr,
               g_udp_sessions[i].peer_len) < 0) {
        return -1;
    }

    return 0;
}

static int flush_udp_remote_pending(int i) {
    if (g_udp_sessions[i].remote_fd == INVALID_SOCKET) {
        return -1;
    }
    return flush_pending_buffer(g_udp_sessions[i].remote_fd,
                                g_udp_sessions[i].to_remote_buf,
                                &g_udp_sessions[i].to_remote_len,
                                &g_udp_sessions[i].to_remote_off);
}

static int create_udp_session(const struct sockaddr_in *peer_addr, socklen_t peer_len) {
    int slot = alloc_udp_session_slot();
    SOCKET_TYPE remote_socket;

    if (slot < 0) {
        return -1;
    }

    remote_socket = net_connect(g_config.remote_addr, 1, 1);
    if (remote_socket == INVALID_SOCKET) {
        return -1;
    }

    reset_udp_session_slot(slot);
    g_udp_sessions[slot].remote_fd = remote_socket;
    g_udp_sessions[slot].peer_addr = *peer_addr;
    g_udp_sessions[slot].peer_len = peer_len;
    g_udp_sessions[slot].active = 1;
    g_udp_sessions[slot].client_is_tunnel = g_config.local_is_tunnel;
    g_udp_sessions[slot].remote_is_tunnel = g_config.remote_is_tunnel;
    g_udp_sessions[slot].last_activity = time(NULL);

    if (init_udp_session_crypto(slot) < 0) {
        close_udp_session(slot);
        return -1;
    }

    return slot;
}

static void cleanup_udp_sessions(void) {
    time_t now = time(NULL);
    int i;

    for (i = 0; i < MAX_UDP_SESSIONS; i++) {
        if (!g_udp_sessions[i].active) {
            continue;
        }
        if (now - g_udp_sessions[i].last_activity > UDP_SESSION_TIMEOUT) {
            close_udp_session(i);
        }
    }
}

static void handle_udp_listener(SOCKET_TYPE server_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    unsigned char buffer[BUFFER_SIZE];
    unsigned char scratch[BUFFER_SIZE + 256];
    size_t actual_len;
    int slot;
    int valread = recvfrom(server_fd, (char*)buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &client_len);

    if (valread <= 0 || !g_config.remote_addr) {
        return;
    }

    slot = find_udp_session_by_peer(&client_addr);
    if (slot < 0) {
        slot = create_udp_session(&client_addr, client_len);
        if (slot < 0) {
            LOG_WARN("Unable to create UDP session for new peer");
            return;
        }
    }

    g_udp_sessions[slot].last_activity = time(NULL);
    actual_len = (size_t)valread;
    if (process_udp_tunnel_in(g_udp_sessions[slot].client_is_tunnel,
                              g_udp_sessions[slot].client_rc4_in,
                              buffer,
                              &actual_len,
                              scratch) < 0) {
        close_udp_session(slot);
        return;
    }

    if (udp_queue_to_remote(slot, buffer, actual_len, scratch) < 0) {
        close_udp_session(slot);
    }
}

static void handle_udp_remote_read(SOCKET_TYPE server_fd, int i) {
    unsigned char buffer[BUFFER_SIZE];
    unsigned char scratch[BUFFER_SIZE + 256];
    size_t actual_len;
    int valread;

    if (!g_udp_sessions[i].active || g_udp_sessions[i].remote_fd == INVALID_SOCKET) {
        return;
    }

    valread = recv(g_udp_sessions[i].remote_fd, (char*)buffer, sizeof(buffer), 0);
    if (valread <= 0) {
        if (valread < 0 && socket_error_is_wouldblock()) {
            return;
        }
        close_udp_session(i);
        return;
    }

    g_udp_sessions[i].last_activity = time(NULL);
    actual_len = (size_t)valread;
    if (process_udp_tunnel_in(g_udp_sessions[i].remote_is_tunnel,
                              g_udp_sessions[i].remote_rc4_in,
                              buffer,
                              &actual_len,
                              scratch) < 0) {
        close_udp_session(i);
        return;
    }

    if (udp_send_to_client(server_fd, i, buffer, actual_len, scratch) < 0) {
        close_udp_session(i);
    }
}

static int handle_active_reconnect(void) {
    SOCKET_TYPE r1;
    SOCKET_TYPE r2 = INVALID_SOCKET;
    int state;

    if (!g_config.auto_reconnect || !g_config.remote_addr || g_config.local_addr || g_sessions[0].active) {
        return 0;
    }

    if (time(NULL) - g_last_reconnect_time < 5) {
        return 0;
    }
    g_last_reconnect_time = time(NULL);

    r1 = net_connect(g_config.remote_addr, !g_config.use_udp, g_config.use_udp);
    if (g_config.remote_addr2) {
        r2 = net_connect(g_config.remote_addr2, !g_config.use_udp, g_config.use_udp);
    }

    if (r1 == INVALID_SOCKET || (g_config.remote_addr2 && r2 == INVALID_SOCKET)) {
        if (r1 != INVALID_SOCKET) {
            CLOSE_SOCKET(r1);
        }
        if (r2 != INVALID_SOCKET) {
            CLOSE_SOCKET(r2);
        }
        return -1;
    }

    state = g_config.remote_addr2 ? SESSION_STATE_FWD : SESSION_STATE_SOCKS5_INIT;
    if (create_active_session(r1, g_config.remote_is_tunnel, !g_config.use_udp,
                              r2, g_config.remote2_is_tunnel, (!g_config.use_udp && r2 != INVALID_SOCKET),
                              state) < 0) {
        return -1;
    }
    return 0;
}

static int finish_socks5_connect(int i, int success) {
    unsigned char reply[10];
    unsigned char scratch[BUFFER_SIZE + 256];

    memset(reply, 0, sizeof(reply));
    reply[0] = 0x05;
    reply[2] = 0x00;
    reply[3] = 0x01;
    reply[1] = success ? 0x00 : 0x05;

    if (success) {
        g_sessions[i].state = SESSION_STATE_SOCKS5_FWD;
    }

    if (send_session_payload(i, 1, reply, sizeof(reply), scratch) < 0) {
        return -1;
    }
    return 0;
}

static int fail_socks5_connect(int i) {
    if (g_sessions[i].remote_fd != INVALID_SOCKET) {
        CLOSE_SOCKET(g_sessions[i].remote_fd);
        g_sessions[i].remote_fd = INVALID_SOCKET;
    }
    g_sessions[i].remote_connecting = 0;

    if (finish_socks5_connect(i, 0) < 0) {
        close_session(i);
        return -1;
    }

    if (has_pending_to_client(i)) {
        g_sessions[i].close_after_client_flush = 1;
        return 0;
    }

    debug_close_session(i, "socks5-connect-failed-after-reply");
    return -1;
}

static int complete_endpoint_connect(int i, int to_client) {
    SOCKET_TYPE fd = to_client ? g_sessions[i].client_fd : g_sessions[i].remote_fd;
    int *connecting = to_client ? &g_sessions[i].client_connecting : &g_sessions[i].remote_connecting;
    int err = 0;
    char local_ep[64];
    char peer_ep[64];

    if (fd == INVALID_SOCKET || !*connecting) {
        return 0;
    }
    if (get_socket_so_error(fd, &err) < 0 || err != 0) {
        if (!to_client && g_sessions[i].state == SESSION_STATE_SOCKS5_CONNECTING) {
            fail_socks5_connect(i);
            return -1;
        }
        debug_close_session(i, to_client ? "client-async-connect-failed" : "remote-async-connect-failed");
        return -1;
    }

    *connecting = 0;

    if (g_sessions[i].state != SESSION_STATE_SOCKS5_CONNECTING) {
        if (g_sessions[i].reverse_socks_role == REVERSE_SOCKS_ROLE_ACTIVE_TARGET &&
            !g_sessions[i].reverse_socks_paired) {
            announce_reverse_socks_pool_connected(i);
        } else {
            announce_session_connected(i);
        }
    }

    if (to_client && send_reverse_socks_hello(i) < 0) {
        return -1;
    }
    if (!to_client && g_sessions[i].state == SESSION_STATE_SOCKS5_CONNECTING) {
        if (finish_socks5_connect(i, 1) < 0) {
            debug_close_session(i, "socks5-finish-connect-reply-failed");
            return -1;
        }
    }
    return 0;
}

static int flush_pending_endpoint(int i, int to_client) {
    SOCKET_TYPE fd = to_client ? g_sessions[i].client_fd : g_sessions[i].remote_fd;
    unsigned char *buf = to_client ? g_sessions[i].to_client_buf : g_sessions[i].to_remote_buf;
    size_t *len = to_client ? &g_sessions[i].to_client_len : &g_sessions[i].to_remote_len;
    size_t *off = to_client ? &g_sessions[i].to_client_off : &g_sessions[i].to_remote_off;

    if (fd == INVALID_SOCKET) {
        return 0;
    }
    return flush_pending_buffer(fd, buf, len, off);
}

static int socks5_auth_enabled(void) {
    return g_config.socks_user != NULL && g_config.socks_pass != NULL;
}

static int schedule_close_after_client_flush(int i) {
    if (has_pending_to_client(i)) {
        g_sessions[i].close_after_client_flush = 1;
        return 0;
    }

    debug_close_session(i, "close-after-client-flush-no-pending");
    return -1;
}

static int handle_forward_payload(int i, int to_client, unsigned char *buffer, size_t len, unsigned char *scratch) {
    if (send_session_payload(i, to_client, buffer, len, scratch) < 0) {
        debug_close_session(i, to_client ? "forward-send-to-client-failed" : "forward-send-to-remote-failed");
        return -1;
    }
    return 0;
}

static int handle_socks5_client_payload(int i, unsigned char *buffer, size_t len, unsigned char *scratch) {
    SOCKET_TYPE remote_socket;
    char dst_ip[256];
    char connect_addr[300];
    int dst_port = 0;
    int method = 0xFF;

    if (g_sessions[i].state == SESSION_STATE_SOCKS5_INIT) {
        unsigned char reply[2] = {0x05, 0xFF};
        size_t nmethods;

        if (len < 2 || buffer[0] != 0x05) {
            debug_close_session(i, "socks5-init-invalid-header");
            return -1;
        }
        nmethods = (size_t)buffer[1];
        if (2 + nmethods > len) {
            debug_close_session(i, "socks5-init-truncated-methods");
            return -1;
        }

        for (size_t idx = 0; idx < nmethods; idx++) {
            unsigned char offered = buffer[2 + idx];

            if (socks5_auth_enabled()) {
                if (offered == 0x02) {
                    method = 0x02;
                    break;
                }
            } else if (offered == 0x00) {
                method = 0x00;
                break;
            }
        }

        reply[1] = (unsigned char)method;
        if (send_session_payload(i, 1, reply, sizeof(reply), scratch) < 0) {
            debug_close_session(i, "socks5-init-reply-failed");
            return -1;
        }

        if (method == 0xFF) {
            return schedule_close_after_client_flush(i);
        }

        g_sessions[i].state = (method == 0x02) ? SESSION_STATE_SOCKS5_AUTH : SESSION_STATE_SOCKS5_CMD;
        return 0;
    }

    if (g_sessions[i].state == SESSION_STATE_SOCKS5_AUTH) {
        unsigned char reply[2] = {0x01, 0x01};
        size_t user_len;
        size_t pass_len;
        const unsigned char *user_ptr;
        const unsigned char *pass_ptr;

        if (len < 5 || buffer[0] != 0x01) {
            debug_close_session(i, "socks5-auth-invalid-header");
            return -1;
        }

        user_len = (size_t)buffer[1];
        if (user_len == 0 || 2 + user_len + 1 > len) {
            debug_close_session(i, "socks5-auth-invalid-username-length");
            return -1;
        }
        user_ptr = &buffer[2];
        pass_len = (size_t)buffer[2 + user_len];
        if (pass_len == 0 || 3 + user_len + pass_len > len) {
            debug_close_session(i, "socks5-auth-invalid-password-length");
            return -1;
        }
        pass_ptr = &buffer[3 + user_len];

        if (strlen(g_config.socks_user) == user_len &&
            strlen(g_config.socks_pass) == pass_len &&
            memcmp(user_ptr, g_config.socks_user, user_len) == 0 &&
            memcmp(pass_ptr, g_config.socks_pass, pass_len) == 0) {
            reply[1] = 0x00;
            if (send_session_payload(i, 1, reply, sizeof(reply), scratch) < 0) {
                debug_close_session(i, "socks5-auth-success-reply-failed");
                return -1;
            }
            g_sessions[i].state = SESSION_STATE_SOCKS5_CMD;
            return 0;
        }

        LOG_WARN("SOCKS5 auth rejected [%d] username_len=%zu password_len=%zu", i, user_len, pass_len);
        if (send_session_payload(i, 1, reply, sizeof(reply), scratch) < 0) {
            debug_close_session(i, "socks5-auth-failure-reply-failed");
            return -1;
        }
        return schedule_close_after_client_flush(i);
    }

    if (g_sessions[i].state == SESSION_STATE_SOCKS5_CMD) {
        memset(dst_ip, 0, sizeof(dst_ip));
        if (len < 4 || buffer[0] != 0x05 || buffer[1] != 0x01) {
            debug_close_session(i, "socks5-cmd-invalid-header-or-command");
            return -1;
        }

        if (buffer[3] == 0x01 && len >= 10) {
            snprintf(dst_ip, sizeof(dst_ip), "%u.%u.%u.%u", buffer[4], buffer[5], buffer[6], buffer[7]);
            dst_port = (buffer[8] << 8) | buffer[9];
        } else if (buffer[3] == 0x03 && len >= 7) {
            int host_len = buffer[4];
            if (host_len <= 0 || host_len >= (int)sizeof(dst_ip) || 5 + host_len + 2 > (int)len) {
                debug_close_session(i, "socks5-cmd-invalid-domain-request");
                return -1;
            }
            memcpy(dst_ip, &buffer[5], (size_t)host_len);
            dst_ip[host_len] = '\0';
            dst_port = (buffer[5 + host_len] << 8) | buffer[5 + host_len + 1];
        } else {
            debug_close_session(i, "socks5-cmd-unsupported-address-type");
            return -1;
        }

        snprintf(connect_addr, sizeof(connect_addr), "%s:%d", dst_ip, dst_port);

        remote_socket = net_connect(connect_addr, !g_config.use_udp, g_config.use_udp);
        if (remote_socket == INVALID_SOCKET) {
            fail_socks5_connect(i);
            return -1;
        }

        g_sessions[i].remote_fd = remote_socket;
        g_sessions[i].remote_is_tunnel = 0;
        g_sessions[i].remote_connecting = !g_config.use_udp;
        g_sessions[i].state = g_config.use_udp ? SESSION_STATE_SOCKS5_FWD : SESSION_STATE_SOCKS5_CONNECTING;
        log_session_endpoints(i, "socks5-remote-connect-issued");

        if (g_config.use_udp) {
            if (finish_socks5_connect(i, 1) < 0) {
                debug_close_session(i, "socks5-udp-finish-connect-reply-failed");
                return -1;
            }
        }
        return 0;
    }

    return handle_forward_payload(i, 0, buffer, len, scratch);
}

static int handle_endpoint_read(int i, int from_client) {
    SOCKET_TYPE fd = from_client ? g_sessions[i].client_fd : g_sessions[i].remote_fd;
    int is_tunnel = from_client ? g_sessions[i].client_is_tunnel : g_sessions[i].remote_is_tunnel;
    rc4_state_t *cipher = from_client ? g_sessions[i].client_rc4_in : g_sessions[i].remote_rc4_in;
    unsigned char *stage = from_client ? g_sessions[i].client_tunnel_buf : g_sessions[i].remote_tunnel_buf;
    size_t *stage_len = from_client ? &g_sessions[i].client_tunnel_len : &g_sessions[i].remote_tunnel_len;
    unsigned char buffer[BUFFER_SIZE];
    unsigned char scratch[MAX_PENDING_BUFFER];
    int valread;

    valread = recv(fd, (char*)buffer, sizeof(buffer), 0);
    if (valread <= 0) {
        if (valread < 0 && socket_error_is_wouldblock()) {
            return 0;
        }
        debug_close_session(i, from_client ? "recv-client-eof-or-error" : "recv-remote-eof-or-error");
        return -1;
    }

    if (from_client &&
        g_sessions[i].reverse_socks_role == REVERSE_SOCKS_ROLE_ACTIVE_TARGET &&
        !g_sessions[i].reverse_socks_paired) {
        g_sessions[i].reverse_socks_paired = 1;
    }

    if (is_tunnel && g_config.tls_obfs) {
        if (append_tunnel_data(stage, stage_len, buffer, (size_t)valread) < 0) {
            debug_close_session(i, from_client ? "tls-stage-overflow-client" : "tls-stage-overflow-remote");
            return -1;
        }
        while (1) {
            size_t actual_len = 0;
            int pop_result = pop_tunnel_frame(stage, stage_len, buffer, &actual_len);

            if (pop_result == 0) {
                break;
            }
            if (pop_result < 0) {
                LOG_WARN("Invalid TLS-obfs frame on session [%d]", i);
                debug_close_session(i, from_client ? "invalid-tls-frame-client" : "invalid-tls-frame-remote");
                return -1;
            }
            if (cipher) {
                rc4_crypt(cipher, buffer, actual_len);
            }
            if (from_client) {
                int control_result = handle_reverse_socks_hello_payload(i, buffer, actual_len);
                if (control_result < 0) {
                    return -1;
                }
                if (control_result > 0) {
                    continue;
                }
                if (g_sessions[i].state == SESSION_STATE_FWD || g_sessions[i].state == SESSION_STATE_SOCKS5_FWD ||
                    g_sessions[i].state == SESSION_STATE_SOCKS5_CONNECTING) {
                    if (handle_forward_payload(i, 0, buffer, actual_len, scratch) < 0) {
                        return -1;
                    }
                } else if (handle_socks5_client_payload(i, buffer, actual_len, scratch) < 0) {
                    return -1;
                }
            } else if (handle_forward_payload(i, 1, buffer, actual_len, scratch) < 0) {
                return -1;
            }
        }
        return 0;
    }

    if (is_tunnel && cipher) {
        rc4_crypt(cipher, buffer, (size_t)valread);
    }

    if (from_client) {
        int control_result = handle_reverse_socks_hello_payload(i, buffer, (size_t)valread);
        if (control_result < 0) {
            return -1;
        }
        if (control_result > 0) {
            return 0;
        }
        if (g_sessions[i].state == SESSION_STATE_FWD || g_sessions[i].state == SESSION_STATE_SOCKS5_FWD ||
            g_sessions[i].state == SESSION_STATE_SOCKS5_CONNECTING) {
            log_unpaired_primary_payload_hint(i, buffer, (size_t)valread);
            return handle_forward_payload(i, 0, buffer, (size_t)valread, scratch);
        }
        return handle_socks5_client_payload(i, buffer, (size_t)valread, scratch);
    }
    return handle_forward_payload(i, 1, buffer, (size_t)valread, scratch);
}

static void try_pair_secondary_listener(SOCKET_TYPE new_socket) {
    int i;
    char local_ep[64];
    char peer_ep[64];

    describe_socket_endpoints(new_socket, local_ep, sizeof(local_ep), peer_ep, sizeof(peer_ep));

    for (i = 0; i < MAX_FDS; i++) {
        if (g_sessions[i].active &&
            g_sessions[i].client_fd != INVALID_SOCKET &&
            g_sessions[i].remote_fd == INVALID_SOCKET &&
            g_sessions[i].state == SESSION_STATE_FWD &&
            !session_waits_for_reverse_socks_hello(i)) {
            g_sessions[i].remote_fd = new_socket;
            g_sessions[i].remote_is_tunnel = g_config.local2_is_tunnel;
            if (init_session_crypto(i) < 0) {
                debug_close_session(i, "ll-pair-init-crypto-failed");
                return;
            }
            announce_session_connected(i);
            return;
        }
    }

    LOG_WARN("No primary connection waiting, dropping secondary connection local=%s peer=%s", local_ep, peer_ep);
    CLOSE_SOCKET(new_socket);
}

static void accept_primary_listener(SOCKET_TYPE server_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    SOCKET_TYPE new_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    int slot;
    if (new_socket == INVALID_SOCKET) {
        LOG_WARN("Primary listener accept failed socket_error=%d", socket_last_error_code());
        return;
    }

    net_set_nonblocking(new_socket);

    slot = alloc_session_slot();
    if (slot < 0) {
        LOG_WARN("Max sessions reached, dropping connection");
        CLOSE_SOCKET(new_socket);
        return;
    }

    reset_session_slot(slot);
    g_sessions[slot].client_fd = new_socket;
    g_sessions[slot].client_is_tunnel = g_config.local_is_tunnel;
    g_sessions[slot].active = 1;
    if (reverse_socks_server_enabled()) {
        g_sessions[slot].reverse_socks_role = REVERSE_SOCKS_ROLE_SERVER_PRIMARY;
    }
    if (g_config.local_addr2) {
        g_sessions[slot].state = SESSION_STATE_FWD;
        if (!reverse_socks_server_enabled()) {
            announce_session_connected(slot);
        }
        return;
    }

    if (g_config.remote_addr) {
        SOCKET_TYPE remote_socket = net_connect(g_config.remote_addr, 1, 0);
        if (remote_socket == INVALID_SOCKET) {
            LOG_ERR("Failed to connect to remote %s", g_config.remote_addr);
            debug_close_session(slot, "primary-connect-remote-failed");
            return;
        }
        g_sessions[slot].remote_fd = remote_socket;
        g_sessions[slot].remote_is_tunnel = g_config.remote_is_tunnel;
        g_sessions[slot].remote_connecting = 1;
        g_sessions[slot].state = SESSION_STATE_FWD;
        if (init_session_crypto(slot) < 0) {
            debug_close_session(slot, "primary-init-crypto-failed");
            return;
        }
        log_session_endpoints(slot, "primary-remote-connect-issued");
        return;
    }

    g_sessions[slot].state = SESSION_STATE_SOCKS5_INIT;
    if (init_session_crypto(slot) < 0) {
        debug_close_session(slot, "socks5-init-crypto-failed");
        return;
    }
    announce_session_connected(slot);
    log_session_endpoints(slot, "socks5-session-ready");
}

void rtp_run(void) {
    fd_set read_fds;
    fd_set write_fds;
    SOCKET_TYPE server_fd = INVALID_SOCKET;
    SOCKET_TYPE server_fd2 = INVALID_SOCKET;
    SOCKET_TYPE max_fd;
    int i;

    net_init();
    for (i = 0; i < MAX_FDS; i++) {
        reset_session_slot(i);
    }
    for (i = 0; i < MAX_UDP_SESSIONS; i++) {
        reset_udp_session_slot(i);
    }

    if (g_config.local_addr) {
        server_fd = net_listen(g_config.local_addr, g_config.use_udp);
        if (server_fd == INVALID_SOCKET) {
            LOG_ERR("Failed to listen on %s", g_config.local_addr);
            exit(1);
        }
    }
    if (g_config.local_addr2) {
        server_fd2 = net_listen(g_config.local_addr2, g_config.use_udp);
        if (server_fd2 == INVALID_SOCKET) {
            LOG_ERR("Failed to listen on %s", g_config.local_addr2);
            exit(1);
        }
    }

    if (g_config.remote_addr && !g_config.local_addr) {
        if (reverse_socks_active_enabled()) {
            if (maintain_reverse_socks_primary_pool() < 0) {
                LOG_ERR("Failed to initialize reverse SOCKS primary pool");
                exit(1);
            }
        } else {
            SOCKET_TYPE r1 = net_connect(g_config.remote_addr, !g_config.use_udp, g_config.use_udp);
            SOCKET_TYPE r2 = INVALID_SOCKET;
            int state = g_config.remote_addr2 ? SESSION_STATE_FWD : SESSION_STATE_SOCKS5_INIT;

            if (g_config.remote_addr2) {
                r2 = net_connect(g_config.remote_addr2, !g_config.use_udp, g_config.use_udp);
            }

            if (r1 == INVALID_SOCKET || (g_config.remote_addr2 && r2 == INVALID_SOCKET)) {
                LOG_ERR("Connection failed");
                if (r1 != INVALID_SOCKET) {
                    CLOSE_SOCKET(r1);
                }
                if (r2 != INVALID_SOCKET) {
                    CLOSE_SOCKET(r2);
                }
                if (!g_config.auto_reconnect) {
                    exit(1);
                }
            } else if (create_active_session(r1, g_config.remote_is_tunnel, !g_config.use_udp,
                                             r2, g_config.remote2_is_tunnel, (!g_config.use_udp && r2 != INVALID_SOCKET),
                                             state) < 0) {
                exit(1);
            }
        }
    }

    while (1) {
        int has_fds = 0;
        int activity;
        struct timeval tv;

        if (reverse_socks_active_enabled()) {
            maintain_reverse_socks_primary_pool();
        } else {
            handle_active_reconnect();
        }
        cleanup_udp_sessions();

        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        max_fd = INVALID_SOCKET;

        if (server_fd != INVALID_SOCKET) {
            FD_SET(server_fd, &read_fds);
            if (server_fd > max_fd) {
                max_fd = server_fd;
            }
            has_fds = 1;
        }
        if (server_fd2 != INVALID_SOCKET) {
            FD_SET(server_fd2, &read_fds);
            if (server_fd2 > max_fd) {
                max_fd = server_fd2;
            }
            has_fds = 1;
        }

        for (i = 0; i < MAX_FDS; i++) {
            if (!g_sessions[i].active) {
                continue;
            }

            if (g_sessions[i].client_fd != INVALID_SOCKET) {
                if (can_read_from_client(i)) {
                    FD_SET(g_sessions[i].client_fd, &read_fds);
                    if (g_sessions[i].client_fd > max_fd) {
                        max_fd = g_sessions[i].client_fd;
                    }
                    has_fds = 1;
                }
                if (g_sessions[i].client_connecting || has_pending_to_client(i)) {
                    FD_SET(g_sessions[i].client_fd, &write_fds);
                    if (g_sessions[i].client_fd > max_fd) {
                        max_fd = g_sessions[i].client_fd;
                    }
                    has_fds = 1;
                }
            }

            if (g_sessions[i].remote_fd != INVALID_SOCKET) {
                if (can_read_from_remote(i)) {
                    FD_SET(g_sessions[i].remote_fd, &read_fds);
                    if (g_sessions[i].remote_fd > max_fd) {
                        max_fd = g_sessions[i].remote_fd;
                    }
                    has_fds = 1;
                }
                if (g_sessions[i].remote_connecting || has_pending_to_remote(i)) {
                    FD_SET(g_sessions[i].remote_fd, &write_fds);
                    if (g_sessions[i].remote_fd > max_fd) {
                        max_fd = g_sessions[i].remote_fd;
                    }
                    has_fds = 1;
                }
            }
        }

        if (g_config.use_udp) {
            for (i = 0; i < MAX_UDP_SESSIONS; i++) {
                if (!g_udp_sessions[i].active || g_udp_sessions[i].remote_fd == INVALID_SOCKET) {
                    continue;
                }

                FD_SET(g_udp_sessions[i].remote_fd, &read_fds);
                if (g_udp_sessions[i].remote_fd > max_fd) {
                    max_fd = g_udp_sessions[i].remote_fd;
                }
                has_fds = 1;

                if (udp_has_pending_to_remote(i)) {
                    FD_SET(g_udp_sessions[i].remote_fd, &write_fds);
                    if (g_udp_sessions[i].remote_fd > max_fd) {
                        max_fd = g_udp_sessions[i].remote_fd;
                    }
                }
            }
        }

        if (!has_fds) {
            sleep_ms(100);
            continue;
        }

        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        activity = select((int)max_fd + 1, &read_fds, &write_fds, NULL, &tv);
        if (activity < 0) {
            LOG_ERR("select error socket_error=%d", socket_last_error_code());
            break;
        }

        for (i = 0; i < MAX_FDS; i++) {
            if (!g_sessions[i].active) {
                continue;
            }

            if (g_sessions[i].client_fd != INVALID_SOCKET && FD_ISSET(g_sessions[i].client_fd, &write_fds)) {
                if (g_sessions[i].client_connecting) {
                    if (complete_endpoint_connect(i, 1) < 0) {
                        continue;
                    }
                } else if (flush_pending_endpoint(i, 1) < 0) {
                    debug_close_session(i, "flush-pending-to-client-failed");
                    continue;
                } else if (g_sessions[i].active && g_sessions[i].close_after_client_flush && !has_pending_to_client(i)) {
                    debug_close_session(i, "close-after-client-flush-complete");
                    continue;
                }
            }

            if (!g_sessions[i].active) {
                continue;
            }

            if (g_sessions[i].remote_fd != INVALID_SOCKET && FD_ISSET(g_sessions[i].remote_fd, &write_fds)) {
                if (g_sessions[i].remote_connecting) {
                    if (complete_endpoint_connect(i, 0) < 0) {
                        continue;
                    }
                } else if (flush_pending_endpoint(i, 0) < 0) {
                    debug_close_session(i, "flush-pending-to-remote-failed");
                    continue;
                }
            }
        }

        if (g_config.use_udp) {
            for (i = 0; i < MAX_UDP_SESSIONS; i++) {
                if (!g_udp_sessions[i].active || g_udp_sessions[i].remote_fd == INVALID_SOCKET) {
                    continue;
                }

                if (FD_ISSET(g_udp_sessions[i].remote_fd, &write_fds) && flush_udp_remote_pending(i) < 0) {
                    close_udp_session(i);
                    continue;
                }

                if (g_udp_sessions[i].active && FD_ISSET(g_udp_sessions[i].remote_fd, &read_fds)) {
                    handle_udp_remote_read(server_fd, i);
                }
            }
        }

        if (server_fd != INVALID_SOCKET && FD_ISSET(server_fd, &read_fds)) {
            if (g_config.use_udp) {
                handle_udp_listener(server_fd);
            } else {
                accept_primary_listener(server_fd);
            }
        }

        if (server_fd2 != INVALID_SOCKET && FD_ISSET(server_fd2, &read_fds) && !g_config.use_udp) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            SOCKET_TYPE new_socket = accept(server_fd2, (struct sockaddr*)&client_addr, &client_len);

            if (new_socket != INVALID_SOCKET) {
                net_set_nonblocking(new_socket);
                try_pair_secondary_listener(new_socket);
            } else {
                LOG_WARN("Secondary listener accept failed socket_error=%d", socket_last_error_code());
            }
        }

        for (i = 0; i < MAX_FDS; i++) {
            if (!g_sessions[i].active) {
                continue;
            }

            if (g_sessions[i].client_fd != INVALID_SOCKET &&
                FD_ISSET(g_sessions[i].client_fd, &read_fds) &&
                handle_endpoint_read(i, 1) < 0) {
                continue;
            }

            if (!g_sessions[i].active) {
                continue;
            }

            if (g_sessions[i].remote_fd != INVALID_SOCKET &&
                FD_ISSET(g_sessions[i].remote_fd, &read_fds)) {
                handle_endpoint_read(i, 0);
            }
        }
    }
}
