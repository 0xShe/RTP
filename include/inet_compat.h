#ifndef RTP_INET_COMPAT_H
#define RTP_INET_COMPAT_H

#include <stdio.h>

/*
 * inet_pton / inet_ntop are only declared in ws2tcpip.h when
 * _WIN32_WINNT >= 0x0600 (Vista+). Legacy builds target XP SP2 / Server 2003
 * (0x0502) but still run on Win7; provide IPv4-only fallbacks in that case.
 */
#if defined(_WIN32) && (_WIN32_WINNT < 0x0600)

static inline int rtp_inet_pton(int af, const char *src, void *dst) {
    struct in_addr *addr;
    unsigned a, b, c, d;
    int n;

    if (af != AF_INET || !src || !dst) {
        return -1;
    }

    addr = (struct in_addr *)dst;
    n = sscanf(src, "%u.%u.%u.%u", &a, &b, &c, &d);
    if (n != 4 || a > 255 || b > 255 || c > 255 || d > 255) {
        return 0;
    }

    addr->s_addr = htonl((a << 24) | (b << 16) | (c << 8) | d);
    return 1;
}

static inline const char *rtp_inet_ntop(int af, const void *src, char *dst, socklen_t cnt) {
    const struct in_addr *addr;
    unsigned long val;
    int n;

    if (af != AF_INET || !src || !dst || cnt == 0) {
        return NULL;
    }

    addr = (const struct in_addr *)src;
    val = ntohl(addr->s_addr);
    n = snprintf(dst, cnt, "%lu.%lu.%lu.%lu",
                 (val >> 24) & 0xff,
                 (val >> 16) & 0xff,
                 (val >> 8) & 0xff,
                 val & 0xff);
    if (n < 0 || (socklen_t)n >= cnt) {
        return NULL;
    }
    return dst;
}

#else

#define rtp_inet_pton inet_pton
#define rtp_inet_ntop inet_ntop

#endif

#endif /* RTP_INET_COMPAT_H */
