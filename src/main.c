#include "rtp.h"
#include "log.h"
#include "core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

rtp_config_t g_config;

void print_usage(const char *prog_name) {
    printf("RTP (Red Team Proxy) v%s\n", RTP_VERSION);
    printf("Usage: %s [options]\n\n", prog_name);
    printf("Options:\n");
    printf("  -l, --local <addr>     Local address to listen on (e.g., *:1080, 127.0.0.1:8080)\n");
    printf("  -r, --remote <addr>    Remote address to connect to (e.g., 1.1.1.1:80)\n");
    printf("  -k, --key <key>        Encryption key for traffic\n");
    printf("  --user <name>          SOCKS5 username\n");
    printf("  --pass <pass>          SOCKS5 password\n");
    printf("  -u, --udp              Enable UDP forwarding\n");
    printf("  -q, --quiet            Silent mode, no logs\n");
    printf("  --tls                  Enable TLS disguise/obfuscation\n");
    printf("  --reverse-socks        Enable reverse SOCKS control handshake hardening\n");
    printf("  --reconnect            Enable auto-reconnect for active outbound modes\n");
    printf("  -h, --help             Show this help message\n\n");
    printf("  Note: '@host:port' marks an RTP tunnel endpoint, '*' keeps its wildcard-listen meaning.\n\n");
    printf("  Fwd (L-R): %s -l *:3389 -r 192.168.1.100:3389\n", prog_name);
    printf("  Fwd (L-L): %s -l *:3389 -l *:4489\n", prog_name);
    printf("  Fwd (R-R): %s -r 1.1.1.1:80 -r 2.2.2.2:8080\n", prog_name);
    printf("  Socks:     %s -l *:1080\n", prog_name);
}

// 智能补全地址 (例如把 "9998" 或 ":9998" 补全为 "0.0.0.0:9998")
// 支持 '@' 前缀显式标记 RTP 隧道端点，保留 '*' 作为 0.0.0.0 的兼容写法
char* normalize_addr(const char* input, int *is_tunnel) {
    if (!input) return NULL;
    
    if (input[0] == '@') {
        if (is_tunnel) *is_tunnel = 1;
        input++;
    } else {
        if (is_tunnel) *is_tunnel = 0;
    }

    // 如果包含冒号且不在开头，或者包含点号，说明是完整的 IP:PORT 或 域名:PORT
    if ((strchr(input, ':') != NULL && input[0] != ':') || strchr(input, '.') != NULL) {
        return strdup(input);
    }
    
    // 否则认为是纯端口或以冒号开头的端口
    const char *port = input[0] == ':' ? input + 1 : input;
    char *normalized = malloc(strlen(port) + 16);
    sprintf(normalized, "0.0.0.0:%s", port);
    return normalized;
}

void parse_args(int argc, char **argv) {
    memset(&g_config, 0, sizeof(g_config));
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]); exit(0);
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--local") == 0) {
            if (i + 1 < argc) {
                int is_tunnel = 0;
                char *addr = normalize_addr(argv[++i], &is_tunnel);
                if (!g_config.local_addr) { g_config.local_addr = addr; g_config.local_is_tunnel = is_tunnel; }
                else { g_config.local_addr2 = addr; g_config.local2_is_tunnel = is_tunnel; }
            }
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--remote") == 0) {
            if (i + 1 < argc) {
                int is_tunnel = 0;
                char *addr = normalize_addr(argv[++i], &is_tunnel);
                if (!g_config.remote_addr) { g_config.remote_addr = addr; g_config.remote_is_tunnel = is_tunnel; }
                else { g_config.remote_addr2 = addr; g_config.remote2_is_tunnel = is_tunnel; }
            }
        } else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--key") == 0) {
            if (i + 1 < argc) g_config.crypto_key = argv[++i];
        } else if (strcmp(argv[i], "--user") == 0) {
            if (i + 1 < argc) g_config.socks_user = argv[++i];
        } else if (strcmp(argv[i], "--pass") == 0) {
            if (i + 1 < argc) g_config.socks_pass = argv[++i];
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            g_config.silent = true;
            rtp_log_set_silent(1);
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--udp") == 0) {
            g_config.use_udp = true;
        } else if (strcmp(argv[i], "--tls") == 0) {
            g_config.tls_obfs = true;
        } else if (strcmp(argv[i], "--reverse-socks") == 0) {
            g_config.reverse_socks = true;
        } else if (strcmp(argv[i], "--reconnect") == 0) {
            g_config.auto_reconnect = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(1);
        }
    }

    if (!g_config.local_addr && !g_config.remote_addr) {
        LOG_ERR("Must specify at least -l/--local or -r/--remote");
        print_usage(argv[0]);
        exit(1);
    }
    if (g_config.crypto_key && g_config.crypto_key[0] == '\0') {
        LOG_ERR("Encryption key must not be empty");
        exit(1);
    }
    if ((g_config.socks_user && !g_config.socks_pass) || (!g_config.socks_user && g_config.socks_pass)) {
        LOG_ERR("SOCKS5 authentication requires both --user and --pass");
        exit(1);
    }
    if (g_config.socks_user && g_config.socks_user[0] == '\0') {
        LOG_ERR("SOCKS5 username must not be empty");
        exit(1);
    }
    if (g_config.socks_pass && g_config.socks_pass[0] == '\0') {
        LOG_ERR("SOCKS5 password must not be empty");
        exit(1);
    }
    if (g_config.socks_user && strlen(g_config.socks_user) > 255) {
        LOG_ERR("SOCKS5 username must be <= 255 bytes");
        exit(1);
    }
    if (g_config.socks_pass && strlen(g_config.socks_pass) > 255) {
        LOG_ERR("SOCKS5 password must be <= 255 bytes");
        exit(1);
    }
}

int main(int argc, char **argv) {
    parse_args(argc, argv);

    if (!g_config.silent) {
        LOG_INFO("RTP (Red Team Proxy) starting...");
        if (g_config.local_addr) {
            LOG_INFO("Local address: %s", g_config.local_addr);
        }
        if (g_config.local_addr2) {
            LOG_INFO("Local secondary address: %s", g_config.local_addr2);
        }
        if (g_config.remote_addr) {
            LOG_INFO("Remote address: %s", g_config.remote_addr);
        }
        rtp_log_set_level(LOG_LEVEL_ERROR);
    }

    rtp_run();

    return 0;
}
