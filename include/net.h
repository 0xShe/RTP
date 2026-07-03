#include "mutated.h"
#ifndef RTP_NET_H
#define RTP_NET_H

#ifdef _WIN32
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#define SOCKET_TYPE SOCKET
#define CLOSE_SOCKET closesocket
#define NET_EWOULDBLOCK WSAEWOULDBLOCK
#define NET_EINPROGRESS WSAEWOULDBLOCK
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#define SOCKET_TYPE int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define CLOSE_SOCKET close
#define NET_EWOULDBLOCK EWOULDBLOCK
#define NET_EINPROGRESS EINPROGRESS
#endif

void net_init(void);
void net_cleanup(void);
int net_set_nonblocking(SOCKET_TYPE fd);
SOCKET_TYPE net_listen(const char *addr_str, int is_udp);
SOCKET_TYPE net_connect(const char *addr_str, int nonblock, int is_udp);

#endif // RTP_NET_H
