#include <stdio.h>
#include <stdlib.h>

#include "beep/mdns.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: mdns_lookup <hostname>\n");
        exit(1);
    }
    char *ip = mdns_getaddrinfo(argv[1]);
    if (ip) {
        fprintf(stderr, "IP: %s\n", ip);
        free(ip);
    } else {
        fprintf(stderr, "Hostname lookup failed\n");
    }
}
