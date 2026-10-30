// sendmsg()/recvmsg() for ENet on wut, which has neither (libwut's sockets stop at send/sendto/recv/recvfrom).
// Force-included into the enet target only (see cmake/wiiu.cmake). ENet sends one datagram built from a few
// buffers and receives into a single buffer, so a copy through sendto()/recvfrom() suffices.
//
// cmake/wiiu.cmake probes the toolchain's <sys/socket.h> and defines SMW_SHIM_TYPES when it has no struct
// msghdr / struct iovec; the types, MSG_TRUNC and SOMAXCONN below are only provided when they are missing.
#pragma once
#include <sys/types.h>
#include <sys/socket.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef SMW_SHIM_TYPES
// ENet casts its ENetBuffer { void* data; size_t dataLength; } to this, so the layout must match.
struct iovec {
    void*  iov_base;
    size_t iov_len;
};

struct msghdr {
    void*         msg_name;
    socklen_t     msg_namelen;
    struct iovec* msg_iov;
    int           msg_iovlen;
    void*         msg_control;
    socklen_t     msg_controllen;
    int           msg_flags;
};
#endif

// ENet tests MSG_TRUNC against msg_flags, which recvmsg() below always clears.
#ifndef MSG_TRUNC
#define MSG_TRUNC 0x20
#endif

// Used by enet_socket_listen (upstream's netplay server). Kept small: this is a console.
#ifndef SOMAXCONN
#define SOMAXCONN 5
#endif

ssize_t sendmsg(int s, const struct msghdr* m, int flags);
ssize_t recvmsg(int s, struct msghdr* m, int flags);

#ifdef __cplusplus
}
#endif
