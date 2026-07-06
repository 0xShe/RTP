#include "net.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int split_host_port(const char *addr_str, char *host, size_t host_len, char *port, size_t port_len) {
    const char *colon = strrchr(addr_str, ':');
    size_t host_size;
    size_t port_size;

    if (!addr_str || !colon || colon == addr_str + strlen(addr_str) - 1) {
        return -1;
    }

    host_size = (size_t)(colon - addr_str);
    port_size = strlen(colon + 1);
    if (host_size >= host_len || port_size >= port_len) {
        return -1;
    }

    memcpy(host, addr_str, host_size);
    host[host_size] = '\0';
    memcpy(port, colon + 1, port_size + 1);
    return 0;
}

void net_init(void) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERR("WSAStartup failed");
        exit(1);
    }
#endif
}

void net_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

int net_set_nonblocking(SOCKET_TYPE fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

// Parses address format like "127.0.0.1:1080", "*:1080", ":1080"
static int parse_addr(const char *addr_str, struct sockaddr_in *addr) {
    char host[256];
    char port_str[16];
    unsigned long port;

    if (split_host_port(addr_str, host, sizeof(host), port_str, sizeof(port_str)) < 0) {
        return -1;
    }

    port = strtoul(port_str, NULL, 10);
    if (port == 0 || port > 65535) {
        return -1;
    }

    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons((unsigned short)port);

    if (strlen(host) == 0 || strcmp(host, "*") == 0) {
        addr->sin_addr.s_addr = INADDR_ANY;
    } else if (rtp_inet_pton(AF_INET, host, &addr->sin_addr) != 1) {
        return -1;
    }
    return 0;
}

SOCKET_TYPE net_listen(const char *addr_str, int is_udp) {
    int type = is_udp ? SOCK_DGRAM : SOCK_STREAM;
    SOCKET_TYPE fd = socket(AF_INET, type, 0);
    if (fd == INVALID_SOCKET) return INVALID_SOCKET;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    if (parse_addr(addr_str, &addr) < 0) {
        LOG_ERR("Invalid listen address format: %s", addr_str);
        CLOSE_SOCKET(fd);
        return INVALID_SOCKET;
    }

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERR("Bind failed on %s", addr_str);
        CLOSE_SOCKET(fd);
        return INVALID_SOCKET;
    }

    if (!is_udp) {
        if (listen(fd, 128) < 0) {
            LOG_ERR("Listen failed");
            CLOSE_SOCKET(fd);
            return INVALID_SOCKET;
        }
    }
    return fd;
}

SOCKET_TYPE net_connect(const char *addr_str, int nonblock, int is_udp) {
    int type = is_udp ? SOCK_DGRAM : SOCK_STREAM;
    char host[256];
    char port[16];
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp;

    if (split_host_port(addr_str, host, sizeof(host), port, sizeof(port)) < 0) {
        LOG_ERR("Invalid connect address format: %s", addr_str);
        return INVALID_SOCKET;
    }

    if (host[0] == '\0' || strcmp(host, "*") == 0) {
        LOG_ERR("Connect address must specify a remote host: %s", addr_str);
        return INVALID_SOCKET;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = type;

    if (getaddrinfo(host, port, &hints, &result) != 0) {
        LOG_DEBUG("Failed to resolve remote host: %s", addr_str);
        return INVALID_SOCKET;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        SOCKET_TYPE fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == INVALID_SOCKET) {
            continue;
        }

        if (nonblock) {
            net_set_nonblocking(fd);
        }

        if (connect(fd, rp->ai_addr, (int)rp->ai_addrlen) == 0) {
            freeaddrinfo(result);
            return fd;
        }
#ifdef _WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
#else
        if (errno == EINPROGRESS) {
#endif
            freeaddrinfo(result);
            return fd;
        }

        CLOSE_SOCKET(fd);
    }

    freeaddrinfo(result);
    return INVALID_SOCKET;
}
