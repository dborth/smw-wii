// sendmsg()/recvmsg() for ENet on libwiisocket, which has neither (nor struct msghdr).
// Force-included into the enet target only (see CMakeLists.txt). ENet sends one datagram built from
// a few buffers and receives into a single buffer, so a copy through sendto()/recvfrom() suffices.
#pragma once
#include <sys/types.h>
#include <sys/socket.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// devkitPPC's newlib has no <sys/uio.h>, and nothing in libogc2/libwiisocket defines struct iovec.
// ENet casts its ENetBuffer { void* data; size_t dataLength; } to this, so the layout must match.
struct iovec {
    void*  iov_base;
    size_t iov_len;
};

// libwiisocket doesn't define MSG_TRUNC; ENet tests it against msg_flags, which recvmsg() below always clears.
#ifndef MSG_TRUNC
#define MSG_TRUNC 0x20
#endif

struct msghdr {
    void*         msg_name;
    socklen_t     msg_namelen;
    struct iovec* msg_iov;
    int           msg_iovlen;
    void*         msg_control;
    socklen_t     msg_controllen;
    int           msg_flags;
};

ssize_t sendmsg(int s, const struct msghdr* m, int flags);
ssize_t recvmsg(int s, struct msghdr* m, int flags);

#ifdef __cplusplus
}
#endif
