#include "wii_sockshim.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

ssize_t sendmsg(int s, const struct msghdr* m, int flags)
{
    uint8_t buf[4096];   // ENET_PROTOCOL_MAXIMUM_MTU
    size_t n = 0;

    for (int i = 0; i < m->msg_iovlen; i++) {
        const size_t len = m->msg_iov[i].iov_len;
        if (n + len > sizeof(buf)) {
            errno = EMSGSIZE;
            return -1;
        }
        memcpy(buf + n, m->msg_iov[i].iov_base, len);
        n += len;
    }
    return sendto(s, buf, n, flags, (const struct sockaddr*)m->msg_name, m->msg_namelen);
}

ssize_t recvmsg(int s, struct msghdr* m, int flags)
{
    if (m->msg_iovlen < 1) {
        errno = EINVAL;
        return -1;
    }
    socklen_t addrlen = m->msg_namelen;
    const ssize_t r = recvfrom(s, m->msg_iov[0].iov_base, m->msg_iov[0].iov_len, flags,
                               (struct sockaddr*)m->msg_name, m->msg_name ? &addrlen : NULL);
    m->msg_namelen = addrlen;
    m->msg_flags = 0;   // truncation can't be detected via recvfrom; ENet sizes its buffer for the max MTU
    return r;
}
