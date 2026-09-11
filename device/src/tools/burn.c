#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define PRINTERVAL 1

#define BILLION INT64_C(1000000000)
#define THOUSAND INT64_C(1000)

#include "beep/log.h"

#define USE_BEEPLOG
#ifndef USE_BEEPLOG
// All LOG_* macros point to LPRINTF.  Set it to just use fprintf.
#ifdef LPRINTF
#undef LPRINTF
#endif
#define LPRINTF(LCAT, PRI, FMT, ...) \
    do { \
        fprintf(stderr, FMT "\n", ##__VA_ARGS__); \
    } while (0)
#endif  // USE_BEEPLOG

/**********
 * From beeplib, copied here to make it easier to compile for other platforms
 **********/

bool str_to_long(char* s, int32_t* v) {
    long int val;
    char* endptr;

    errno = 0;
    val = strtol(s, &endptr, 10);
    if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
            || (errno != 0 && val == 0)
            || (endptr == s)
            || (*endptr != '\0')) {
        return false;
    }

    *v = val;
    return true;
}

int split_host_port(char* host_port) {
    char* colon = strstr(host_port, ":");
    if (colon == NULL) {
        LOG_ERROR(log_beep_main, "No colon in host:port: %s", host_port);
        exit(1);
    }
    *colon = '\0';

    char* port = colon + 1;
    int32_t val;
    if (!str_to_long(port, &val)) {
        LOG_ERROR(log_beep_main, "No colon in host:port: %s", host_port);
        exit(1);
    }
    return val;
}

/**********
 * End from beeplib
 **********/

struct time_stats {
    int interval;
    int64_t counter;
    int64_t prev_nsecs;
    int64_t prev_prev_nsecs;
    int64_t prev_count;
    int64_t prev_prev_count;
    int64_t next_count;
    bool fast;
};

struct time_stats time_stats_init(int interval, bool fast) {
    struct time_stats time_stats;
    time_stats.counter = 0;
    time_stats.interval = interval;
    time_stats.prev_nsecs = -1;
    time_stats.prev_count = -1;
    time_stats.prev_prev_count = -1;
    time_stats.next_count = -1;
    time_stats.fast = fast;
    return time_stats;
}

void time_stats_print(struct time_stats* time_stats) {
    LOG_DEBUG(log_beep_main, "%" PRId64 " %" PRId64 " %" PRId64 " %" PRId64,
           time_stats->counter,
           time_stats->prev_nsecs,
           time_stats->prev_count,
           time_stats->next_count);
}

int64_t get_nsecs(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    int64_t nsecs = (int64_t) t.tv_sec * BILLION + t.tv_usec * THOUSAND;
    return nsecs;
}

void update_next_count(struct time_stats* time_stats, int64_t nsecs) {
    int64_t nsec_delta = nsecs - time_stats->prev_nsecs;
    int64_t count_delta = time_stats->counter - time_stats->prev_count;
    if (nsec_delta < count_delta) {
        LOG_WARN(log_beep_main, "WARNING: timed operation doesn't take long enough: %" PRId64 " %" PRId64,
               nsec_delta, count_delta);
    }
    int64_t nsecs_per_count = nsec_delta / count_delta;
    //LOG_DEBUG(log_beep_main, "%" PRId64, nsecs_per_count);
    int64_t count_inc =
        (int64_t) time_stats->interval * BILLION / nsecs_per_count;

    // This is a hack specifically for the network test.
    if (!time_stats->fast && count_inc > 1000) {
        count_inc = 1000;
    }
    time_stats->next_count += count_inc;
    //LOG_DEBUG(log_beep_main, "NEXT COUNT: %" PRId64, time_stats->next_count);
}

void update_prevs(struct time_stats* time_stats, int64_t nsecs) {
    time_stats->prev_prev_nsecs = time_stats->prev_nsecs;
    time_stats->prev_nsecs = nsecs;
    time_stats->prev_prev_count = time_stats->prev_count;
    time_stats->prev_count = time_stats->counter;
}

int64_t check_time(struct time_stats* time_stats) {
    int64_t nsecs;
    int first_limit = 10;
    if (time_stats->fast) {
        first_limit = 1000;
    }

    time_stats->counter++;
    if (time_stats->next_count != -1) {
        if (time_stats->counter > time_stats->next_count) {
            nsecs = get_nsecs();
            update_next_count(time_stats, nsecs);
            update_prevs(time_stats, nsecs);
            return time_stats->prev_nsecs - time_stats->prev_prev_nsecs;
        } else {
            return -1;
        }
    } else if (time_stats->prev_nsecs == -1) {
        update_prevs(time_stats, get_nsecs());
    } else if (time_stats->counter > first_limit) {
        update_next_count(time_stats, get_nsecs());
    }
    return -1;
}

void print_stats(struct time_stats* time_stats, char* prefix, char* extra) {
    int64_t count_delta = time_stats->prev_count
                          - time_stats->prev_prev_count;
    int64_t nsecs_delta = time_stats->prev_nsecs
                          - time_stats->prev_prev_nsecs;
    LOG_INFO(log_beep_main, "%s %d.%.6d %" PRId64 " %" PRId64 " %" PRId64 " %s",
            prefix,
            (int) (time_stats->prev_nsecs / BILLION),
            (int) (time_stats->prev_nsecs % BILLION),
            count_delta,
            nsecs_delta,
            count_delta * BILLION / nsecs_delta,
            extra ? extra : "");
}

void burn_cpu(int interval) {
    struct time_stats time_stats = time_stats_init(interval, true);

    while (1) {
        sqrt(rand());
        if (check_time(&time_stats) != -1) {
            print_stats(&time_stats, "CPU", NULL);
        }
    }
}

#define RAM_AMOUNT 4 * 1024 * 1024
#define CHUNK_SIZE 4096

void burn_ram(int interval) {
    struct time_stats time_stats = time_stats_init(interval, true);
    unsigned char* mem;

    while (1) {
        mem = malloc(RAM_AMOUNT);
        for (unsigned char c=0; c<255; c++) {
            for (int i=0; i<RAM_AMOUNT; i+=CHUNK_SIZE) {
                mem[i] = c;
                if (check_time(&time_stats) != -1) {
                    print_stats(&time_stats, "RAM", NULL);
                }
            }
            for (int i=0; i<RAM_AMOUNT; i+=CHUNK_SIZE) {
                if (mem[i] != c) {
                    LOG_ERROR(log_beep_main, "memory corruption: %p", mem + i);
                }
                if (check_time(&time_stats) != -1) {
                    print_stats(&time_stats, "RAM", NULL);
                }
            }
        }
        free(mem);
    }
}

#define DISK_CHUNK_SIZE 64 * 1024

void burn_disk(int interval, const char* fname, int fsize) {
    uint8_t bytes[DISK_CHUNK_SIZE];
    uint8_t rbytes[DISK_CHUNK_SIZE];
    for (int i=0; i<DISK_CHUNK_SIZE; i++) {
        bytes[i] = rand() % 256;
    }
    FILE* f;

    while (1) {
        int total = 0;
        if (remove(fname) != 0) {
            LOG_DEBUG(log_beep_main, "Couldn\'t remove file.");
        }
        f = fopen(fname, "w");
        if (!f) {
            LOG_ERROR(log_beep_main, "Couldn\'t open file.");
            exit(1);
        }
        while (1) {
            int ret = fwrite(bytes, 8, DISK_CHUNK_SIZE / 8, f);
            LOG_INFO(log_beep_main, "DISK Write Time: %" PRId64 " Total: %d",
                    get_nsecs(), total);
            if (ret != DISK_CHUNK_SIZE / 8) {
                LOG_DEBUG(log_beep_main, "Breaking");
                clearerr(f);
                break;
            }
            if (ferror(f)) {
                LOG_DEBUG(log_beep_main, "ferror after fwrite");
                clearerr(f);
                break;
            }
            if (fsync(fileno(f)) != 0) {
                LOG_DEBUG(log_beep_main, "Couldn\'t sync");
                clearerr(f);
                break;
            }
            if (ferror(f)) {
                LOG_DEBUG(log_beep_main, "ferror after fsync");
                clearerr(f);
                break;
            }
            //if (check_time(&time_stats) != -1) {
            //    print_stats(&time_stats, "DISK WRITE", NULL);
            //}
            total += DISK_CHUNK_SIZE;
            if (fsize && total > fsize) {
                break;
            }
        }
        fclose(f);
        f = fopen(fname, "r");
        total = 0;
        while (1) {
            if (fread(rbytes, DISK_CHUNK_SIZE, 1, f) != 1) {
                if (feof(f)) {
                    clearerr(f);
                    break;
                } else {
                    LOG_ERROR(log_beep_main, "disk read error");
                    clearerr(f);
                    break;
                }
            }
            total += DISK_CHUNK_SIZE;
            LOG_INFO(log_beep_main, "DISK Read Time: %" PRId64 " Total: %d",
                    get_nsecs(), total);
            //if (check_time(&time_stats) != -1) {
            //    print_stats(&time_stats, "DISK READ", NULL);
            //}
            for (int i=0; i<DISK_CHUNK_SIZE; i++) {
                if (bytes[i] != rbytes[i]) {
                    LOG_ERROR(log_beep_main, "disk corruption at: %d",
                            fseek(f, 0, SEEK_CUR) + i);
                }
            }
        }
        fclose(f);
    }
}

// TODO: use beeplib.
int connect_ip(char *ip, int port) {
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1) {
        LOG_ERROR(log_beep_main, "ERROR: call to socket failed, %s", strerror(errno));
        exit(1);
    }

    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    int s = inet_pton(AF_INET, ip, &servaddr.sin_addr);
    if (s <= 0) {
        if (s == 0)
            LOG_ERROR(log_beep_main, "ERROR: inet_pton arg not in presentation format: %s",
                    ip);
        else
            LOG_ERROR(log_beep_main, "ERROR: inet_pton failed, %s", strerror(errno));
        exit(1);
    }
    servaddr.sin_port = htons(port);

    if (connect(socketfd, (struct sockaddr*)&servaddr, sizeof(servaddr))
        < 0) {
        LOG_ERROR(log_beep_main, "ERROR: Couldn't connect");
        exit(1);
    }

    return socketfd;
}

void burn_network_read(int interval, char* ip, int port) {
    struct time_stats time_stats = time_stats_init(interval, false);
    int bytes;
    int64_t total_bytes_prev = 0, total_bytes = 0;
    int64_t elapsed_nsecs;
    char out_buf[1024];

    char* rbuf[4096];

    int socketfd = connect_ip(ip, port);
    while (1) {
        bytes = recv(socketfd, rbuf, 4096, 0);
        if (bytes <= 0) {
            if (bytes == 0) {
                LOG_ERROR(log_beep_main, "ERROR: socket closed");
            } else {
                LOG_ERROR(log_beep_main, "ERROR: socket error, %s", strerror(errno));
            }
            exit(1);
        }
        total_bytes += bytes;
        if ((elapsed_nsecs = check_time(&time_stats)) != -1) {
            sprintf(out_buf, "%" PRId64, (total_bytes - total_bytes_prev)
                                         * BILLION / elapsed_nsecs);
            print_stats(&time_stats, "NETWORK_READ", out_buf);
            total_bytes_prev = total_bytes;
        }
    }
}

void burn_network_write(int interval, char* ip, int port) {
    struct time_stats time_stats = time_stats_init(interval, false);
    int bytes;
    int64_t total_bytes_prev = 0, total_bytes = 0;
    int64_t elapsed_nsecs;
    char out_buf[1024];

    char* buf[4096];
    memset(buf, 'Z', 4096);

    int socketfd = connect_ip(ip, port);
    while (1) {
        //LOG_DEBUG(log_beep_main, "Calling send");
        bytes = send(socketfd, buf, 4096, 0);
        //LOG_DEBUG(log_beep_main, "Done send");
        if (bytes <= 0) {
            if (bytes == 0) {
                LOG_ERROR(log_beep_main, "ERROR: socket closed");
            } else {
                LOG_ERROR(log_beep_main, "ERROR: socket error, %s", strerror(errno));
            }
            exit(1);
        }
        total_bytes += bytes;
        if ((elapsed_nsecs = check_time(&time_stats)) != -1) {
            sprintf(out_buf, "%" PRId64, (total_bytes - total_bytes_prev)
                                         * BILLION / elapsed_nsecs);
            print_stats(&time_stats, "NETWORK_WRITE", out_buf);
            total_bytes_prev = total_bytes;
        }
    }
}

void usage(void) {
    fprintf(stderr,
            "burn [--cpu]\n"
            "     [--ram]\n"
            "     [--disk=<filename>]\n"
            "     [--netread=<host:port>]\n"
            "     [--netwrite=<host:port>]\n");
}

int main(int argc, char** argv) {
    int c;
    struct option opt;
    static int option_cpu = false;
    static int option_ram = false;
    static char* option_netread = NULL;
    static char* option_netwrite = NULL;
    static char* option_disk = NULL;
    static struct option long_options[] =
         {
             {"cpu", no_argument, &option_cpu, true},
             {"ram", no_argument, &option_ram, true},
             {"disk", required_argument, 0, 0},
             {"netread", required_argument, 0, 0},
             {"netwrite", required_argument, 0, 0},
             {0, 0, 0, 0}
         };
    int option_index = 0;

#ifdef USE_BEEPLOG
    log_beep_main = LOG_CATEGORY_GET("burn");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);
#endif  // USE_BEEPLOG

    while (1) {
        c = getopt_long (argc, argv, "", long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch (c) {
            case 0:
                opt = long_options[option_index];
                if (strcmp(opt.name, "netread") == 0) {
                    option_netread = strdup(optarg);
                } else if (strcmp(opt.name, "netwrite") == 0) {
                    option_netwrite = strdup(optarg);
                } else if (strcmp(opt.name, "disk") == 0) {
                    option_disk = strdup(optarg);
                }
                break;
            case '?':
                usage();
                return 1;
            default:
                LOG_ERROR(log_beep_main, "Programming error: Unhandled argument");
                break;
        }
    }

    if (!option_cpu && !option_ram && !option_netread && !option_netwrite
            && ! option_disk) {
        LOG_ERROR(log_beep_main, "ERROR: You must specify at least one target");
        usage();
        exit(1);
    }

    int pid;
    if (option_cpu) {
        switch (pid = fork()) {
            case 0:
                burn_cpu(PRINTERVAL);
            case -1:
                LOG_ERROR(log_beep_main, "ERROR: Couldn\'t fork, %s", strerror(errno));
                exit(1);
            default:
                break;
        }
    }
    usleep(500000);
    if (option_ram) {
        switch (pid = fork()) {
            case 0:
                burn_ram(PRINTERVAL);
            case -1:
                LOG_ERROR(log_beep_main, "ERROR: Couldn\'t fork, %s", strerror(errno));
                exit(1);
            default:
                break;
        }
    }
    usleep(500000);
    if (option_disk) {
        switch (pid = fork()) {
            case 0:
                burn_disk(PRINTERVAL, option_disk, 0); //1024 * 1024 * 1024);
            case -1:
                LOG_ERROR(log_beep_main, "ERROR: Couldn\'t fork, %s", strerror(errno));
                exit(1);
            default:
                break;
        }
    }
    usleep(500000);
    if (option_netread) {
        int port = split_host_port(option_netread);
        switch (pid = fork()) {
            case 0:
                burn_network_read(PRINTERVAL, option_netread, port);
            case -1:
                LOG_ERROR(log_beep_main, "ERROR: Couldn\'t fork, %s", strerror(errno));
                exit(1);
            default:
                break;
        }
    }
    usleep(500000);
    if (option_netwrite) {
        int port = split_host_port(option_netwrite);
        switch (pid = fork()) {
            case 0:
                burn_network_write(PRINTERVAL, option_netwrite, port);
            case -1:
                LOG_ERROR(log_beep_main, "ERROR: Couldn\'t fork, %s", strerror(errno));
                exit(1);
            default:
                break;
        }
    }
    while (1) {
        sleep(100000);
    }
}
