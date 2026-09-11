#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define BEEP_CLIENT

#include "beeplib.h"
//#include "beep/protocol.h"
#include "beep/config.h"
#include "debug.h"
#include "net_flags.h"

#include "client.h"

int MAX_RETRIES = 30;
int RETRY_INTERVAL_MILLIS = 500;
bool _setup = false;

static void setup_library(void) {
    if(_setup)
        return;

    _setup = true;
}

int beep_connect(const char* ip_addr, unsigned short port) {
    setup_library();

    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1) {
        LOG_ERROR(log_beep_main, "Call to socket failed %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in servaddr;

    bzero(&servaddr, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    inet_pton(AF_INET, ip_addr, &servaddr.sin_addr);
    servaddr.sin_port = htons(port);

    //LOG_DEBUG(log_beep_main, "Connecting");
    if (connect(socketfd, (struct sockaddr*)&servaddr, sizeof(servaddr))
        < 0) {
        beep_disconnect(socketfd);
        socketfd = -1;
    }
    //LOG_DEBUG(log_beep_main, "Done Connecting");

    return socketfd;
}

void beep_disconnect(int socketfd) {
    if (socketfd > 0) {
        int ret = close(socketfd);

        // Ignore EBADF, it means fd isn't a valid open file descriptor, which
        // probably means we already closed.
        if (ret == -1 && errno != EBADF) {
            LOG_ERROR(log_beep_main, "close failed: %s", strerror(errno));
        }
    }
}

int _beep_recv_amount(int socketfd, uint8_t* buf, int total) {
    int received_amount = 0;
    while (received_amount < total) {
        if (net_flags.network_delay) {
            sleep_random_millis(0, net_flags.network_delay);
        }
        int amount = recv(socketfd,
                      buf + received_amount,
                      total - received_amount,
                      0);
        if (amount == 0 || amount == -1) {
            beep_disconnect(socketfd);
            LOG_ERROR(log_beep_main, "Beep client received incomplete message.");
            return -1;
        }
        received_amount += amount;
    }
    assert(received_amount == total);
    return received_amount;
}

bool beep_send(int socketfd, const uint8_t* buf, int total) {
    int sent_amount = 0;
    //LOG_ERROR(log_beep_main, "beep_send: %d %d", socketfd, total);
    while (sent_amount < total) {
        if (net_flags.network_delay) {
            sleep_random_millis(0, net_flags.network_delay);
        }
        int amount = 0;
        for(int retries = 0; retries < MAX_RETRIES; retries++) {
            //LOG_ERROR(log_beep_main, "beep_send single: %d", total - sent_amount);
            int ret = send(socketfd,
                          buf + sent_amount,
                          total - sent_amount,
                          0);
            if (ret > 0) {
                amount = ret;
                break;
            } else if(errno == EAGAIN || errno == EWOULDBLOCK) {
                //LOG_ERROR(log_beep_main, "beep_send: retrying: %d %d", ret, errno);
                usleep(RETRY_INTERVAL_MILLIS * 1000);
            } else {
                beep_disconnect(socketfd);
                LOG_ERROR(log_beep_main, "beep_send failed: %s", strerror(errno));
                return false;
            }
        }
        if(!amount) {
            beep_disconnect(socketfd);
            LOG_ERROR(log_beep_main, "beep_send failed (too many retries)");
            return false;
        } else {
            //LOG_INFO(log_beep_main, "beep_send: sent!");
            sent_amount += amount;
        }
    }
    //LOG_INFO(log_beep_main, "beep_send: done!");
    assert(sent_amount == total);
    return true;
}

uint8_t* beep_get_message(int socketfd) {
    int message_size;
    if (_beep_recv_amount(socketfd, (uint8_t*) &message_size, 4) == -1) {
        return NULL;
    }
    message_size = ntohl(message_size);
    //LOG_DEBUG(log_beep_main, "Receiving amount: %d", message_size);

    uint8_t* buf = malloc(message_size);
    if (_beep_recv_amount(socketfd, buf, message_size) == -1) {
        free(buf);
        return NULL;
    }
    return buf;
}

// TODO merge beep_command and beep_command_len, use beep_send instead of
// createing a new buffer and then sending it.

uint8_t* beep_command(int socketfd, int command, const char* args) {
    uint8_t* response = NULL;
    int largs = strlen(args) + 1;
    uint8_t* args_message = malloc(largs+8);
    net_writeint(args_message, largs+4);
    net_writeint(args_message+4, command);
    memcpy(args_message+8, args, largs);

    if (!beep_send(socketfd, args_message, largs+8)) {
        goto end;
    }
    //LOG_DEBUG(log_beep_main, "Sent amount: %d", largs+8);

    response = beep_get_message(socketfd);
    //LOG_DEBUG(log_beep_main, "response: %s", response);

end:
    free(args_message);
    return response;
}

uint8_t* beep_command_len(int socketfd, int command, uint8_t* args, int largs) {
    uint8_t* response = NULL;
    uint8_t* args_message = malloc(largs+8);
    net_writeint(args_message, largs+4);
    net_writeint(args_message+4, command);
    memcpy(args_message+8, args, largs);

    if (!beep_send(socketfd, args_message, largs+8)) {
        goto end;
    }
    LOG_DEBUG(log_beep_main, "Sent amount: %d", largs+8);

    response = beep_get_message(socketfd);
    LOG_DEBUG(log_beep_main, "response: %s", response);

end:
    free(args_message);
    return response;
}

uint8_t* beep_request(
        int socketfd, int target_id, int subtarget_id, int request_id,
        uint8_t* args, int args_len) {
    uint8_t* response = NULL;
    uint8_t* message = malloc(args_len+16);

    net_writeint(message, args_len+12);
    net_writeint(message+4, target_id);
    net_writeint(message+8, subtarget_id);
    net_writeint(message+12, request_id);
    memcpy(message+16, args, args_len);

    if (!beep_send(socketfd, message, args_len+16)) {
        goto end;
    }
    //LOG_DEBUG(log_beep_main, "Sent amount: %d", largs+8);

    response = beep_get_message(socketfd);
    //LOG_DEBUG(log_beep_main, "response: %s", response);

end:
    free(message);
    return response;
}
