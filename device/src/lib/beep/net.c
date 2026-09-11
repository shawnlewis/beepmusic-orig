// Some things to fix up:
//   There are lots of "+2" and "-2" to align connections with pollfds.
//     - One (hacky?) way to fix is to keep them aligned, with negative
//       indexes referring to listen sockets.
//     - But better to try to encapsulate somehow.
//   The beep_event_loop function is too long.

#define _BSD_SOURCE /* strdup() */

#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <unistd.h>

#include "beeplib.h"
#include "net.h"
#include "beepports.h"
#include "beep/debug.h"
#include "beep/net_flags.h"


struct pollfd _beep_listen_socket(unsigned short port);

BeepPortState* port_state_new(int port_num, int num_connections) {
    BeepPortState* port_state = malloc(sizeof(BeepPortState));
    port_state->port_num = port_num;
    port_state->pollfds = beep_new_vector(num_connections+1,
                                         sizeof(struct pollfd));
    port_state->conn_states = beep_new_vector(num_connections,
                                             sizeof(ConnState*));
    struct pollfd listen_pollfd = _beep_listen_socket(port_num);
    beep_vector_append(port_state->pollfds, &listen_pollfd);

    // Recursive because a request callback (which runs locked) can call
    // beep_notify which also takes the lock.
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&port_state->mutex, &attr);

    return port_state;
}

int port_state_is_full(BeepPortState* port_state) {
    return beep_vector_is_full(port_state->pollfds);
}

void port_state_add_conn(BeepPortState* port_state,
                         int conn_fd) {
    struct pollfd pollfd = {
        .fd = conn_fd,
        .events = POLLIN
    };
    beep_vector_append(port_state->pollfds, &pollfd);
    ConnState* conn_state = malloc(sizeof(ConnState));
    conn_state->code = BEEP_CONN_VALID;
    conn_state->cursor = 0;
    conn_state->response_cursor = 0;
    beep_vector_append(port_state->conn_states, &conn_state);
}

// Closes the i'th connection.
void port_state_close_conn(BeepPortState* port_state, unsigned int i) {
    int fd = ((struct pollfd*)
              beep_vector_index(port_state->pollfds, i+1))->fd;
    close(fd);
    beep_vector_pop(port_state->pollfds, i+1);

    ConnState** conn_state = beep_vector_index(port_state->conn_states, i);
    free(*conn_state);
    beep_vector_pop(port_state->conn_states, i);
}

void beep_respond(BeepConnection* conn, uint8_t* buf, int len) {
    if (len <= 0 || conn->state->code != BEEP_CONN_VALID) {
        return;
    }
    if (conn->state->response_cursor + len > BEEP_CONN_BUF_SIZE) {
        // TODO: close connection when this happens
        conn->state->code = BEEP_CONN_RESPONSE_FULL;
        return;
    }
    memcpy(conn->state->response_buffer + conn->state->response_cursor,
           buf, len);
    conn->state->response_cursor += len;
    //LOG_DEBUG(log_beep_main, "%d", conn->state->response_cursor);

    // Signal that we have data to write.
    conn->pollfd->events |= POLLOUT;
    //LOG_DEBUG(log_beep_main, "%d", conn->pollfd->fd);
}

void beep_broadcast(BeepPortState* port_state, uint8_t* buf, int len) {
    pthread_mutex_lock(&port_state->mutex);
    uint8_t* output = lenbuf(buf, len);
    struct pollfd* pollfds = port_state->pollfds->data;
    for (int i=0; i<port_state->conn_states->num_elements; i++) {
        ConnState* conn_state =
            *((ConnState**) beep_vector_index(
                port_state->conn_states, i));
        BeepConnection conn = {
            .pollfd = &pollfds[i+1],
            .state = conn_state
        };
        beep_respond(&conn, output, len+4);
    }
    free(output);
    pthread_mutex_unlock(&port_state->mutex);
}

struct pollfd _beep_listen_socket(unsigned short port) {
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd == -1) {//error
        LOG_ERROR(log_beep_main, "Call to socket() failed.");
        exit(1);
    }

    int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);

    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);

    if(bind(listenfd, (struct sockaddr*) &servaddr, sizeof(servaddr)) == -1) {
        LOG_ERROR(log_beep_main, "Call to bind() failed.");
        exit(1);
    }

    // TODO: don't hardcode. Deny connects beyond the number we can handle.
    if(listen(listenfd, 20) == -1){
        LOG_ERROR(log_beep_main, "Call to listen() failed.");
        exit(1);
    }
    LOG_DEBUG(log_beep_main, "listening %d", port);

    struct pollfd listen_pollfd = {
        .fd = listenfd,
        .events = POLLIN
    };

    return listen_pollfd;
}


typedef struct {
    BeepPortState* port_state;
    BeepSocketIncomingCB callback;
    void* userdata;
} BeepPortThreadState;

void* _beep_socket_thread(void* arg) {
    BeepPortThreadState* port_thread_state = arg;
    BeepPortState* port_state = port_thread_state->port_state;
    // Prevent us from getting a SIGPIPE and dying. SIGPIPE gets sent if
    // we write to a connection that was closed. TODO: don't do that.
    signal( SIGPIPE, SIG_IGN );

    struct sockaddr_in cliaddr;
    unsigned int clilen = sizeof(cliaddr);
    struct pollfd* pollfds;
    while (1) {
        int nfds = port_state->pollfds->num_elements;
        pollfds = port_state->pollfds->data;

        //LOG_DEBUG(log_beep_main, "Polling");

        // Poll with a timeout, as another thread might call beep_notify,
        // which means we now have a socket that write has been enabled on.
        // We could instead use another socket as a signal that something
        // has changed.
        poll(pollfds, nfds, 100);
        pthread_mutex_lock(&port_state->mutex);
        for (int i=0; i<nfds; i++) {
            int close_conn = 0;
            if (!pollfds[i].revents) {
                continue;
            }
            //LOG_DEBUG(log_beep_main, "Got event on fd: %d %d", i, pollfds[i].revents);
            if (i == POLLFD_INDEX_LISTEN) {
                if (!port_state_is_full(port_state)) {
                    int fd = accept(
                            pollfds[POLLFD_INDEX_LISTEN].fd,
                            (struct sockaddr*)&cliaddr, &clilen);
                    port_state_add_conn(port_state, fd);
                } else {
                    LOG_ERROR(log_beep_main, "THIS IS A PROBLEM. port state is full. Aborting...");
                    abort();
                    // TODO: We should deny port_state if we're over the limit
                }
            } else /* A connection socket */ {
                ConnState* conn_state =
                    *((ConnState**) beep_vector_index(
                        port_state->conn_states, i-1));
                if (pollfds[i].revents & POLLOUT
                    && !(pollfds[i].revents & POLLHUP)) {
                    if (net_flags.network_delay) {
                        sleep_random_millis(0, net_flags.network_delay);
                    }
                    int amount = write(
                            pollfds[i].fd,
                            conn_state->response_buffer,
                            conn_state->response_cursor);
                    if (amount == -1) {
                        close_conn = 1;
                    } else {
                        memmove(conn_state->response_buffer,
                                conn_state->response_buffer + amount,
                                BEEP_CONN_BUF_SIZE - amount);
                        conn_state->response_cursor -= amount;
                        if ((conn_state->response_cursor) == 0) {
                            pollfds[i].events &= ~POLLOUT;
                        }
                    }
                }
                if (pollfds[i].revents & POLLIN) {
                    // TODO: handle buffer overflow. If the buffer fills up
                    //     but callback does not process it there is a
                    //     programming error (message too long). Probably
                    //     just close the connection.
                    if (net_flags.network_delay) {
                        sleep_random_millis(0, net_flags.network_delay);
                    }
                    int amount = read(
                            pollfds[i].fd,
                            conn_state->buffer + conn_state->cursor,
                            BEEP_CONN_BUF_SIZE - conn_state->cursor);
                    //LOG_DEBUG(log_beep_main, "Received amount %d", amount);
                    if (amount <= 0) {
                        // amount == 0 means the connection is close.d
                        // amount == -1 is an error.
                        if (amount < 0) {
                            LOG_WARN(log_beep_main,
                                    "Got socket read error :%s",
                                    strerror(errno));
                        }
                        close_conn = 1;
                    } else {
                        conn_state->cursor += amount;
                        int nhandled;
                        do {
                            BeepConnection conn = {
                                .pollfd = &pollfds[i],
                                .state = conn_state
                            };
                            nhandled = port_thread_state->callback(
                                    &conn, port_thread_state->userdata);
                            memmove(conn_state->buffer,
                                    conn_state->buffer + nhandled,
                                    BEEP_CONN_BUF_SIZE - nhandled);
                            conn_state->cursor -= nhandled;
                        } while (conn_state->code == BEEP_CONN_VALID
                                 && nhandled);
                    }
                    if (conn_state->cursor == BEEP_CONN_BUF_SIZE) {
                        LOG_WARN(log_beep_main, "Buffer full. Connection sent invalid data");
                        close_conn = 1;

                    }
                }
                if (pollfds[i].revents & POLLHUP) {
                    LOG_INFO(log_beep_main, "Received POLLHUP. Closing.");
                    close_conn = 1;
                }
                if (conn_state->code != BEEP_CONN_VALID) {
                    LOG_WARN(log_beep_main, "Invalid connection state: %d", conn_state->code);
                    close_conn = 1;
                }
                if (close_conn) {
                    LOG_INFO(log_beep_main, "Closing");
                    port_state_close_conn(port_state, i-1);
                }
            }

            // If we get here it means we processed something, because
            // otherwise we would have continued at the top of the for
            // loop. break because we may have modified the port_state
            // array (port_state_close_conn for example).
            break;
        }
        pthread_mutex_unlock(&port_state->mutex);
    }
}

BeepPortState* beep_register_port(int port_num,
                                  int num_connections,
                                  BeepSocketIncomingCB callback,
                                  void* userdata) {
    BeepPortThreadState* port_thread_state =
            malloc(sizeof(BeepPortThreadState));
    port_thread_state->port_state = port_state_new(port_num, num_connections);
    port_thread_state->callback = callback;
    port_thread_state->userdata = userdata;

    pthread_t* thread = malloc(sizeof(pthread_t));
    pthread_create(thread, NULL, _beep_socket_thread, port_thread_state);

    return port_thread_state->port_state;
}

int beep_net_read(BeepConnection* conn, uint8_t** buf, int len) {
    // Really simple we'll just keep returning 0 until we there is enough data
    // the caller has requested.  This will probably get slammed and need
    // to be redone.
    if (conn->state->cursor < len) {
        return 0;
    }
    *buf = (uint8_t *) conn->state->buffer;
    return len;
}

int beep_net_read_message(BeepConnection* conn, uint8_t** buf, int* len) {
    if (conn->state->cursor < 4) {
        return 0;
    }
    *len = net_readint(conn->state->buffer);
    //LOG_DEBUG(log_beep_main, "Cursor: %d, Length: %d", conn->state->cursor, length);
    if (conn->state->cursor < *len + 4)
        return 0;
    *buf = ((uint8_t*) conn->state->buffer) + 4;
    return *len + 4;
}

void beep_net_write_message(BeepConnection* conn, uint8_t* buf, int len) {
    uint8_t* output = lenbuf(buf, len);
    beep_respond(conn, output, len+4);
    free(output);
}
