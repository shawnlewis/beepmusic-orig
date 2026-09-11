#ifndef BEEP_UHELPERS_H
#define BEEP_UHELPERS_H

#include <libubox/ustream.h>

typedef void (*usock_connected_cb)(bool success, void *priv);

// Waits for a successful connection or error on fd.
// Assumes that you have called connect() on fd already. Uses uloop.
//
// done_cb will be called with success == true or false
void usock_wait_connect(int fd, usock_connected_cb done_cb, void *priv);

#endif
