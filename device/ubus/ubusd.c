/*
 * Copyright (C) 2011 Felix Fietkau <nbd@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <sys/socket.h>
#include <sys/uio.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <libubox/blob.h>
#include <libubox/uloop.h>
#include <libubox/usock.h>
#include <libubox/list.h>

#include "ubusd.h"

#include "ubus_trace.h"

static void ubus_msg_unshare(struct ubus_msg_buf *ub)
{
	void *ub_data;

	ub->refcount = 1;
	ub_data = malloc(ub->len);
	if (!ub_data)
		abort();
	memcpy(ub_data, ub->data, ub->len);
	ub->data = ub_data;
}

static void ubus_msg_ref(struct ubus_msg_buf *ub)
{
	if (ub->refcount == ~0)
		ubus_msg_unshare(ub);
	ub->refcount++;
}

struct ubus_msg_buf *ubus_msg_new(void *data, int len, bool shared)
{
	struct ubus_msg_buf *ub;

	ub = calloc(1, sizeof(*ub));
	if (!ub)
		return NULL;

	if (shared) {
		ub->refcount = ~0;
		ub->data = data;
	} else {
		ub->refcount = 1;
		ub->data = malloc(len);
		if (!ub->data) {
			free(ub);
			return NULL;
		}
		if (data) {
			memcpy(ub->data, data, len);
		}
	}

	ub->len = len;
	return ub;
}

void ubus_msg_free(struct ubus_msg_buf *ub)
{
	switch (ub->refcount) {
	case 1:
		free(ub->data);
		free(ub);
		break;
	case ~0:
		free(ub);
		break;
	default:
		ub->refcount--;
		break;
	}
}

static int ubus_msg_writev(int fd, struct ubus_msg_buf *ub, int offset)
{
	struct iovec iov[2];

	UBUS_TRACE("ubus_msg_writev tx_queue seq: %d\n", ub->hdr.seq);

	if (offset < sizeof(ub->hdr)) {
		iov[0].iov_base = ((char *) &ub->hdr) + offset;
		iov[0].iov_len = sizeof(ub->hdr) - offset;
		iov[1].iov_base = (char *) ub->data;
		iov[1].iov_len = ub->len;
		return writev(fd, iov, 2);
	} else {
		offset -= sizeof(ub->hdr);
		return write(fd, ((char *) ub->data) + offset, ub->len - offset);
	}
}

static void ubus_msg_enqueue(struct ubus_client *cl, struct ubus_msg_buf *ub)
{
	if (cl->tx_queue[cl->txq_tail]) {
		fprintf(stderr, "Queue full. Aborting...\n");
		abort();
		return;
	}

	UBUS_TRACE("ubus_msg_enqueue tx_queue seq: %d tail: %d\n", ub->hdr.seq, cl->txq_tail);
    //for (int i=cl->txq_cur; i< cl->txq_tail; i++) {
    //    UBUS_TRACE("  tx_queue[%d] seq: %d\n", i, cl->tx_queue[i]->hdr.seq);
    //}

	ubus_msg_ref(ub);
	cl->tx_queue[cl->txq_tail] = ub;
	cl->txq_tail = (cl->txq_tail + 1) % ARRAY_SIZE(cl->tx_queue);
}

/* takes the msgbuf reference */
void ubus_msg_send(struct ubus_client *cl, struct ubus_msg_buf *ub, bool free)
{
	int written;
	UBUS_TRACE("ubus_msg_send\n");

	if (!cl->tx_queue[cl->txq_cur]) {
		written = ubus_msg_writev(cl->sock.fd, ub, 0);
		UBUS_TRACE("ubus_msg_send written: %d\n", written);
		if (written < 0)
			written = 0;
		else if (written >= ub->len + sizeof(ub->hdr))
			goto out;

		cl->txq_ofs = written;

		/* get an event once we can write to the socket again */
		uloop_fd_add(&cl->sock, ULOOP_READ | ULOOP_WRITE | ULOOP_EDGE_TRIGGER);
	}
	UBUS_TRACE("didn't write enough, enqueueing\n");
	ubus_msg_enqueue(cl, ub);

out:
	if (free)
		ubus_msg_free(ub);
}

static struct ubus_msg_buf *ubus_msg_head(struct ubus_client *cl)
{
	UBUS_TRACE("ubus_msg_head: tx_queue txq_cur: %d\n", cl->txq_cur);
	return cl->tx_queue[cl->txq_cur];
}

static void ubus_msg_dequeue(struct ubus_client *cl)
{
	struct ubus_msg_buf *ub = ubus_msg_head(cl);

	if (!ub)
		return;

	UBUS_TRACE("ubus_msg_dequeue tx_queue txq_cur: %d\n", cl->txq_cur);
	ubus_msg_free(ub);
	cl->txq_ofs = 0;
	cl->tx_queue[cl->txq_cur] = NULL;
	cl->txq_cur = (cl->txq_cur + 1) % ARRAY_SIZE(cl->tx_queue);
}

static void handle_client_disconnect(struct ubus_client *cl)
{
	while (ubus_msg_head(cl))
		ubus_msg_dequeue(cl);

	ubusd_proto_free_client(cl);
	uloop_fd_delete(&cl->sock);
	close(cl->sock.fd);
	free(cl);
}

static void client_cb(struct uloop_fd *sock, unsigned int events)
{
	struct ubus_client *cl = container_of(sock, struct ubus_client, sock);
	struct ubus_msg_buf *ub;

	UBUS_TRACE("client_cb\n");

	/* first try to tx more pending data */
	while ((ub = ubus_msg_head(cl))) {
		int written;

		written = ubus_msg_writev(sock->fd, ub, cl->txq_ofs);
		UBUS_TRACE("client_cb: written: %d\n", written);
		if (written < 0) {
			UBUS_TRACE("client_cb: written: -1, err: %s\n", strerror(errno));
			switch(errno) {
			case EINTR:
			case EAGAIN:
				break;
			default:
				goto disconnect;
			}
			break;
		}

		cl->txq_ofs += written;
		if (cl->txq_ofs < ub->len + sizeof(ub->hdr))
			break;

		ubus_msg_dequeue(cl);
	}

	UBUS_TRACE("client_cb DONE writing\n");

	/* prevent further ULOOP_WRITE events if we don't have data
	 * to send anymore */
	if (!ubus_msg_head(cl) && (events & ULOOP_WRITE))
		uloop_fd_add(sock, ULOOP_READ | ULOOP_EDGE_TRIGGER);

	UBUS_TRACE("client_cb AFTER stop writing check\n");

retry:
	if (!sock->eof && cl->pending_msg_offset < sizeof(cl->hdrbuf)) {
		int offset = cl->pending_msg_offset;
		int bytes;

		bytes = read(sock->fd, (char *)&cl->hdrbuf + offset, sizeof(cl->hdrbuf) - offset);
		if (bytes < 0)
			goto out;

		cl->pending_msg_offset += bytes;
		if (cl->pending_msg_offset < sizeof(cl->hdrbuf))
			goto out;

		if (blob_pad_len(&cl->hdrbuf.data) > UBUS_MAX_MSGLEN)
			goto disconnect;

		cl->pending_msg = ubus_msg_new(NULL, blob_raw_len(&cl->hdrbuf.data), false);
		if (!cl->pending_msg)
			goto disconnect;

		memcpy(&cl->pending_msg->hdr, &cl->hdrbuf.hdr, sizeof(cl->hdrbuf.hdr));
		memcpy(cl->pending_msg->data, &cl->hdrbuf.data, sizeof(cl->hdrbuf.data));
	}

	ub = cl->pending_msg;
	if (ub) {
		int offset = cl->pending_msg_offset - sizeof(ub->hdr);
		int len = blob_raw_len(ub->data) - offset;
		int bytes = 0;

		if (len > 0) {
			bytes = read(sock->fd, (char *) ub->data + offset, len);
			if (bytes <= 0)
				goto out;
		}

		if (bytes < len) {
			cl->pending_msg_offset += bytes;
			goto out;
		}

		/* accept message */
		cl->pending_msg_offset = 0;
		cl->pending_msg = NULL;
		ubusd_proto_receive_message(cl, ub);
		goto retry;
	}

out:
	UBUS_TRACE("client_cb OUT\n");
	if (!sock->eof || ubus_msg_head(cl))
		return;

disconnect:
	handle_client_disconnect(cl);
}

static bool get_next_connection(int fd)
{
	struct ubus_client *cl;
	int client_fd;

	client_fd = accept(fd, NULL, 0);
	if (client_fd < 0) {
		switch (errno) {
		case ECONNABORTED:
		case EINTR:
			return true;
		default:
			return false;
		}
	}

	cl = ubusd_proto_new_client(client_fd, client_cb);
	if (cl)
		uloop_fd_add(&cl->sock, ULOOP_READ | ULOOP_EDGE_TRIGGER);
	else
		close(client_fd);

	return true;
}

static void server_cb(struct uloop_fd *fd, unsigned int events)
{
	bool next;

	do {
		next = get_next_connection(fd->fd);
	} while (next);
}

static struct uloop_fd server_fd = {
	.cb = server_cb,
};

static int usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [<options>]\n"
		"Options: \n"
		"  -s <socket>:		Set the unix domain socket to listen on\n"
		"\n", progname);
	return 1;
}

int main(int argc, char **argv)
{
	const char *ubus_socket = UBUS_UNIX_SOCKET;
	int ret = 0;
	int ch;

    ubus_trace_init();
	signal(SIGPIPE, SIG_IGN);

	uloop_init();

	while ((ch = getopt(argc, argv, "s:")) != -1) {
		switch (ch) {
		case 's':
			ubus_socket = optarg;
			break;
		default:
			return usage(argv[0]);
		}
	}

	unlink(ubus_socket);
	server_fd.fd = usock(USOCK_UNIX | USOCK_SERVER | USOCK_NONBLOCK, ubus_socket, NULL);
	if (server_fd.fd < 0) {
		perror("usock");
		ret = -1;
		goto out;
	}
	uloop_fd_add(&server_fd, ULOOP_READ | ULOOP_EDGE_TRIGGER);

	uloop_run();
	unlink(ubus_socket);

out:
	uloop_done();
	return ret;
}
