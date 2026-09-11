#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "beep/debug.h"
#include "beep/mdns.h"

static void mdns_addrinfo_cb(DNSServiceRef sdRef,
        DNSServiceFlags flags, uint32_t interfaceIndex,
        DNSServiceErrorType errorCode, const char *hostname,
        const struct sockaddr *address, uint32_t ttl, void *context) {
    char *ip = context;
    if (errorCode) {
        LOG_ERROR(log_beep_main,
                "DNSServiceGetAddrInfo callback error: %d", errorCode);
    } else {
        inet_ntop(AF_INET,
                  (void*) &(((struct sockaddr_in*) address)->sin_addr),
                  ip, INET_ADDRSTRLEN);
    }
}

// Synchronous mdns name lookup ('abeepdevice.local')
// Caller owns returned ip address. Returns NULL if not found or error.
char *mdns_getaddrinfo(const char* hostname) {
    DNSServiceRef sd_ref;
    char ip[INET_ADDRSTRLEN];
    int err;

    // System configured timeout, can't be controlled by us, we'd have to use
    // select with a timeout to do that.
    err = DNSServiceGetAddrInfo(
            &sd_ref,
            0,                         // flags
            0,                         // interface (0: all interfaces)
            kDNSServiceProtocol_IPv4,
            hostname,
            mdns_addrinfo_cb,
            ip);
    if (err) {
        LOG_ERROR(log_beep_main,
                "mdns_getaddrinfo: DNSServiceGetAddrInfo failed: %d", err);
        return NULL;
    }

    // Use select to wait for a result, this way we can set a timeout.
    int fd = DNSServiceRefSockFD(sd_ref);
    struct timeval tv = {5, 0};
    fd_set readset;
    FD_ZERO(&readset);
    FD_SET(fd, &readset);
    int result = select(fd + 1, &readset, NULL, NULL, &tv);
    if (result < 0) {
        LOG_ERROR(log_beep_main, "mdns_getaddrinfo: select returned < 0: %d",
                result);
        return NULL;
    }
    if (result == 0) {
        // timeout
        return NULL;
    }

    // Blocks until we have a response, or the system timeout fires.
    err = DNSServiceProcessResult(sd_ref);
    if (err) {
        LOG_ERROR(log_beep_main,
                "mdns_getaddrinfo: DNSServiceProcessResult failed: %d", err);
        return NULL;
    }

    char* response = malloc(INET_ADDRSTRLEN);
    strncpy(response, ip, INET_ADDRSTRLEN);

    return response;
}
