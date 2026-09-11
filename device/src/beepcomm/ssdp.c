/*
 * Copyright (c) 2012 Netflix, Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * 
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETFLIX, INC. AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED. IN NO EVENT SHALL NETFLIX OR CONTRIBUTORS BE LIABLE FOR ANY 
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND 
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "beepcomm.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>

#include "mongoose.h"

static char buf[4096];

static const char ddxml[] =
    "<?xml version=\"1.0\"?>\r\n"
    "<root\r\n"
    "    xmlns=\"urn:schemas-upnp-org:device-1-0\">\r\n"
    "    <specVersion>\r\n"
    "        <major>1</major>\r\n"
    "        <minor>0</minor>\r\n"
    "    </specVersion>\r\n"
    "    <device>\r\n"
    "        <deviceType>urn:beep-com:device:beep:1</deviceType>\r\n"
    "        <friendlyName>%s</friendlyName>\r\n"
    "        <manufacturer>Beep, Inc.</manufacturer>\r\n"
    "        <modelName>%s</modelName>\r\n"
    "        <UDN>uuid:%s</UDN>\r\n"
    "    </device>\r\n"
    "</root>\r\n";

static char *generate_ddxml_response(const char *friendly_name,
        const char *model_name, const char *uuid, const char *host_addr,
        uint16_t port) {
    // TODO: Define these buffer sizes w/ non arbitrary padding
    static char resp_buf[sizeof(ddxml) + 256 + 256] = {0,};
    char ddxml_buf[sizeof(ddxml) + 256] = {0,};

    if(!friendly_name || !model_name || !uuid || !host_addr) {
        return NULL;
    }

    int bytes;

    if((bytes = snprintf(ddxml_buf, sizeof(ddxml_buf), ddxml,
            friendly_name, model_name, uuid)) >= sizeof(ddxml_buf)) {
        return NULL;
    }

    if(snprintf(resp_buf, sizeof(resp_buf),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/xml; charset=utf-8\r\n"
            "Content-Length: %d\r\n"
            "Application-URL: http://%s:%d/apps/\r\n"
            "\r\n"
            "%s", bytes, host_addr, port, ddxml_buf) >= sizeof(resp_buf)) {
        return NULL;
    }

    return resp_buf;
}

static int request_handler(struct mg_connection *conn) {
    const struct mg_request_info *ri = mg_get_request_info(conn);
    struct ssdp_context *ssdp_ctx = (struct ssdp_context *)ri->user_data;

    const char *host = mg_get_header(conn, "Host");
    if(!host) {
        mgutil_resp_error(conn, 400, "Bad Request");
        return 1;
    }

    if(!strcmp(ri->request_method, "GET") && !strcmp(ri->uri, "/dd.xml")) {
        pthread_mutex_lock(&ssdp_ctx->lock);
        char *response = generate_ddxml_response(ssdp_ctx->friendly_name,
                ssdp_ctx->model_name, ssdp_ctx->uuid, get_address(host),
                comm_flags.dial_port);
        pthread_mutex_unlock(&ssdp_ctx->lock);

        mg_write(conn, response, strlen(response));
        if (comm_flags.debug) {
            LOG_DEBUG(log_beep_main, "Sending dd.xml to %s:%d",
                   long_to_ip_str(ri->remote_ip), ri->remote_port);
        }
    } else {
        mgutil_resp_error(conn, 404, "Not Found");
        if (comm_flags.debug) {
            LOG_DEBUG(log_beep_main, "Sending HTTP/404 to %s:%d",
                   long_to_ip_str(ri->remote_ip), ri->remote_port);
        }
    }

    return 1;
}

/* ssdp */
static const char ssdp_reply[] =
    "HTTP/1.1 200 OK\r\n"
    "LOCATION: http://%s:%d/dd.xml\r\n"
    "CACHE-CONTROL: max-age=1800\r\n"
    "LAST-MODIFIED: %s\r\n"
    "EXT:\r\n"
    "BOOTID.UPNP.ORG: 1\r\n"
    "SERVER: Linux/2.6 UPnP/1.0 beepcomm_ssdp/1.0\r\n"
    "ST: urn:dial-multiscreen-org:service:dial:1\r\n"
    "USN: uuid:%s::"
    "urn:dial-multiscreen-org:service:dial:1\r\n\r\n";

static char *generate_ssdp_reply(const char *host_addr, int port,
        const char *last_updated, const char *uuid, ssize_t *size) {
    //TODO: Define buffer padding non-arbitrarily
    static char ssdp_buf[sizeof(ssdp_reply) + 256] = {0,};

    if(!host_addr || !last_updated || !uuid || !size) {
        *size = -1;
        return NULL;
    }

    if((*size = snprintf(ssdp_buf, sizeof(ssdp_buf), ssdp_reply,
            host_addr, port, last_updated, uuid)) >= sizeof(ssdp_buf)) {
        *size = -1;
        return NULL;
    }

    return ssdp_buf;
}

static void server_cb(struct uloop_fd *ufd, unsigned int events) {
    // Control message buffers
    struct iovec iov[1] = {
        {
            .iov_base = buf,
            .iov_len = sizeof(buf) - 1,
        },
    };
    struct sockaddr_in saddr;
    socklen_t addrlen = sizeof(saddr);
    char cmbuf[256];
    struct msghdr mh = {
        .msg_iov = iov,
        .msg_iovlen = 1,
        .msg_name = &saddr,
        .msg_namelen = sizeof(saddr),
        .msg_control = cmbuf,
        .msg_controllen = sizeof(cmbuf),
    };

    struct ssdp_context *ssdp_ctx = container_of(ufd, struct ssdp_context, ufd);

    ssize_t bytes;
    ssize_t send_size = 0;

    struct cmsghdr *cmsg;
    char *incoming_address = NULL;

    memset(buf, 0, sizeof(buf));

    if (-1 == (bytes = recvmsg(ufd->fd, &mh, 0))) {
        LOG_ERROR(log_beep_main, "recvmsg failed: %s", strerror(errno));
        return;
    }

    for(cmsg = CMSG_FIRSTHDR(&mh); cmsg != NULL; cmsg = CMSG_NXTHDR(&mh, cmsg)) {
        if(cmsg->cmsg_level != IPPROTO_IP || cmsg->cmsg_type != IP_PKTINFO) {
            continue;
        }

        struct in_pktinfo *pi = (struct in_pktinfo *)CMSG_DATA(cmsg);
        incoming_address = inet_ntoa(pi->ipi_spec_dst);
        break;
    }

    char *response = generate_ssdp_reply(incoming_address, comm_flags.ssdp_port,
            ssdp_ctx->last_updated, ssdp_ctx->uuid, &send_size);

    // sophisticated SSDP parsing algorithm
    // TODO: Fix this
    if (!strstr(buf, "ST: urn:dial-multiscreen-org:service:dial:1")) {
        return;
    }

    if (comm_flags.debug) {
        LOG_DEBUG(log_beep_main, "Sending SSDP reply to %s:%d",
               inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
    }

    if (-1 == sendto(ufd->fd, response, send_size, 0, (struct sockaddr *)&saddr,
                addrlen)) {
        LOG_ERROR(log_beep_main, "sendto failed: %s", strerror(errno));
        return;
    }
}

struct ssdp_context *start_ssdp(const char *friendly_name,
        const char *model_name, const char *uuid) {
    struct mg_callbacks callbacks;
    struct ip_mreq mreq;
    char port_str[10];
    struct ssdp_context *ssdp_ctx;
    int one=1;
    int fd;
    time_t now = time(0);
    struct tm tm = *gmtime(&now);

    ssdp_ctx = calloc(1, sizeof(struct ssdp_context));

    ssdp_ctx->friendly_name = strdup(friendly_name);
    strftime(ssdp_ctx->last_updated, sizeof(ssdp_ctx->last_updated),
            "%a, %d %b %Y %H:%M:%S %Z", &tm);

    strncpy(ssdp_ctx->model_name, model_name, 32);
    strncpy(ssdp_ctx->uuid, uuid, 37);

    if(pthread_mutex_init(&ssdp_ctx->lock, 0)) {
        LOG_ERROR(log_beep_main, "Failed to init lock mutex");
        abort();
    }

    snprintf(port_str,sizeof(port_str)-1,"%d",comm_flags.ssdp_port);

    // Mongoose
    const char *options[] = {
        "listening_ports", port_str,
        "num_threads", "3",
        NULL
    };

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.begin_request = request_handler;
    ssdp_ctx->mg_ctx = mg_start(&callbacks, ssdp_ctx, options);
    if(!ssdp_ctx->mg_ctx) {
        LOG_ERROR(log_beep_main, "Couldn't start mongoose");
        abort();
    }
    LOG_INFO(log_beep_main, "Serving dd.xml @ http://localhost:%d/dd.xml",
            comm_flags.ssdp_port);

    // SSDP

    struct ifaddrs *ifaddr, *ifa;
    if(-1 == getifaddrs(&ifaddr)) {
        LOG_ERROR(log_beep_main, "getifaddrs failed: %s",
                strerror(errno));
        abort();
    }

    fd = usock(USOCK_UDP | USOCK_SERVER | USOCK_IPV4ONLY | USOCK_NUMERIC
               | USOCK_NOADDRCONFIG,
            "0.0.0.0", "1900");

    if (fd == -1) {
        LOG_ERROR(log_beep_main, "Could not open usock on 0.0.0.0:1900");
        abort();
    }

    if(-1 == setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &one, sizeof(one))) {
        LOG_ERROR(log_beep_main, "Failed to set IP_PKTINFO flag: %s",
                strerror(errno));
        abort();
    }

    for(ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
        mreq.imr_interface.s_addr =
            ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;

        if (-1 == setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                    &mreq, sizeof(mreq))) {
            LOG_ERROR(log_beep_main, "Failed to add membership: %s",
                    strerror(errno));
            abort();
        }

        LOG_INFO(log_beep_main, "Joined SSDP multicast group on %s",
                inet_ntoa(((struct sockaddr_in *)ifa->ifa_addr)->sin_addr));
    }

    ssdp_ctx->ufd.cb = server_cb;
    ssdp_ctx->ufd.fd = fd;

    uloop_fd_add(&ssdp_ctx->ufd, ULOOP_READ);
    freeifaddrs(ifaddr);

    return ssdp_ctx;
}

void stop_ssdp(struct ssdp_context *ssdp_ctx) {
    mg_stop(ssdp_ctx->mg_ctx);
    LOG_INFO(log_beep_main, "dd.xml server stopped");

    uloop_fd_delete(&ssdp_ctx->ufd);
    close(ssdp_ctx->ufd.fd);

    LOG_INFO(log_beep_main, "SSDP stopped");
    free(ssdp_ctx->friendly_name);
    free(ssdp_ctx);
}
