#include <sys/socket.h>
#include <stdlib.h>

#include "beep/debug.h"

#include "uhelpers.h"

struct connecting_sock {
    struct uloop_fd ufd;
    usock_connected_cb done_cb;
    void *priv;
};

static void uloop_connect_fd_ready_cb(struct uloop_fd *ufd, unsigned int events) {
    struct connecting_sock *cs =
        container_of(ufd, struct connecting_sock, ufd);

    int error;
    socklen_t error_len = sizeof(error);
    getsockopt(ufd->fd, SOL_SOCKET, SO_ERROR, &error, &error_len);

    uloop_fd_delete(ufd);

    if (error) {
        LOG_INFO(log_beep_main, "Connection failed on fd: %d", ufd->fd);
        cs->done_cb(false, cs->priv);
    } else {
        LOG_DEBUG(log_beep_main, "Connected fd: %d", ufd->fd);
        cs->done_cb(true, cs->priv);
    }

    free(cs);
}

void usock_wait_connect(int fd, usock_connected_cb done_cb, void *priv) {
    struct connecting_sock *cs = calloc(1, sizeof(struct connecting_sock));
    cs->ufd.cb = uloop_connect_fd_ready_cb;
    cs->ufd.fd = fd;
    cs->done_cb = done_cb;
    cs->priv = priv;

    uloop_fd_add(&cs->ufd, ULOOP_WRITE);
}
