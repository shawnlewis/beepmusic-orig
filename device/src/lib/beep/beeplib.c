#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef _MIPS_ARCH
#include <execinfo.h>
#endif  // _MIPS_ARCH

#include "beeplib.h"
#include "debug.h"

pid_t beep_gettid(void) {
    return syscall(SYS_gettid);
}

char* beep_slurp(char* path) {
    FILE *in;
    char* result;

    in = fopen(path, "r");
    if (!in) {
        LOG_ERROR(log_beep_main, "Couldn't open file %s\n", path);
        return NULL;
    }
    if (fseek(in, 0, SEEK_END) == -1) {
        LOG_ERROR(log_beep_main, "fseek failed\n");
        return NULL;
    }
    int size = ftell(in);
    if (size == -1) {
        LOG_ERROR(log_beep_main, "ftell failed\n");
        return NULL;
    }
    rewind(in);

    result = malloc(size + 1);
    result[size] = '\0';
    size_t size_read = fread(result, size, 1, in);
    if (size_read != 1) {
        LOG_ERROR(log_beep_main, "fread didn\'t read enough\n");
        return NULL;
    }
    return result;
}

char* beep_str_replace_one(char* target, char* find, const char* replace) {
    char* loc = strstr(target, find);
    if (!loc) {
        return NULL;
    }
    unsigned int found_offset = loc - target;

    unsigned int target_len = strlen(target);
    unsigned int find_len = strlen(find);
    unsigned int replace_len = strlen(replace);

    char* pre_loc = target;
    unsigned int pre_len = found_offset;

    char* post_loc = target + pre_len + find_len;
    unsigned int post_len = target_len - find_len - pre_len;

    unsigned int result_len = target_len - find_len + replace_len;
    char* result = malloc((result_len + 1) * sizeof(char*));
    result[result_len] = '\0';

    memcpy(result, pre_loc, pre_len);
    memcpy(result + pre_len, replace, replace_len);
    memcpy(result + pre_len + replace_len, post_loc, post_len);

    return result;
}

bool beep_urlparse(
        const char *s, char* scheme, char* host,
        int *port, char* path) {
    scheme[0] = '\0';
    host[0] = '\0';
    *port = 80;
    path[0] = '\0';

    int result = sscanf(s, "%9[^:]://%99[^:]:%d/%199[^\n]",
            scheme, host, port, path);
    if (result == 4) {
        goto success;
    }

    result = sscanf(s, "%9[^:]://%99[^:]:%d", scheme, host, port);
    if (result == 3) {
        goto success;
    }

    result = sscanf(s, "%9[^:]://%99[^/]/%199[^\n]", scheme, host, path);
    if (result == 3) {
        goto success;
    }

    path[0] = '\0';
    result = sscanf(s, "%9[^:]://%99[^:/]", scheme, host);
    if (result == 2) {
        goto success;
    }

    return 0;

success:
    if (!strncmp("http", scheme, URLPARSE_SCHEME_LEN)
            || !strncmp("https", scheme, URLPARSE_SCHEME_LEN)) {
        return 1;
    } else {
        return 0;
    }
}

BeepStaticVector* beep_new_vector (int max_elements, int el_size) {
    BeepStaticVector *v = malloc(sizeof(BeepStaticVector));
    v->el_size = el_size;
    v->max_elements = max_elements;
    v->data = calloc(max_elements, el_size);
    v->num_elements = 0;
    return v;
}

void* beep_vector_index(BeepStaticVector* v, int i) {
    if (i >= v->num_elements) {
        return NULL;
    }
    return v->data + (i * v->el_size);
}

int beep_vector_is_full(BeepStaticVector* v) {
    return (v->num_elements == v->max_elements);
}

BeepVectorResult beep_vector_append(BeepStaticVector* v, void* el) {
    if (v->num_elements == v->max_elements) {
        return BEEP_VECTOR_FULL;
    }
    memcpy(v->data + (v->num_elements * v->el_size),
           el,
           v->el_size);
    v->num_elements++;
    return BEEP_SUCCESS;
}

BeepVectorResult beep_vector_pop(BeepStaticVector* v, unsigned int i) {
    if (i >= v->num_elements) {
        return BEEP_VECTOR_OUT_OF_RANGE;
    }
    memmove(v->data + (i * v->el_size),
            v->data + ((i+1) * v->el_size),
            (v->num_elements - i - 1) * v->el_size);
    v->num_elements--;
    return BEEP_SUCCESS;
}

int net_readint(uint8_t* buf) {
    int some_int;
    memcpy(&some_int, buf, 4);
    return ntohl(some_int);
}

void net_writeint(uint8_t* buf, int val) {
    int nint = htonl(val);
    memcpy(buf, &nint, 4);
}

uint8_t* lenbuf(uint8_t* buf, int len) {
    uint8_t* output = malloc(len+4);
    net_writeint(output, len);
    memcpy(output+4, buf, len);
    return output;
}

void dump_hex(const unsigned char* buf, size_t len) {
    for (int i=0; i<len; i++) {
        fprintf(stderr, "%02X", buf[i]);
        if (i % 2 == 1) {
            fprintf(stderr, " ");
        }
        if (i % 16 == 15) {
            fprintf(stderr, "\n");
        }
    }
    fprintf(stderr, "\n");
}

bool beep_strtoll(char *s, long long int *v) {
    long long int val;
    char *endptr;

    errno = 0;
    val = strtoll(s, &endptr, 0);
    if ((errno == ERANGE && (val == LLONG_MAX || val == LLONG_MIN))
            || (errno != 0 && val == 0)
            || (endptr == s)
            || (*endptr != '\0')) {
        return false;
    }

    *v = val;
    return true;
}

bool beep_strtol(char *s, long int *v) {
    long long int val;
    char *endptr;

    errno = 0;
    val = strtoll(s, &endptr, 0);
    if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
            || (errno != 0 && val == 0)
            || (endptr == s)
            || (*endptr != '\0')) {
        return false;
    }

    *v = val;
    return true;
}

bool beep_strtoi(char *s, int *v) {
    long int val;
    char *endptr;

    errno = 0;
    val = strtoll(s, &endptr, 0);
    if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
            || (errno != 0 && val == 0)
            || (endptr == s)
            || (*endptr != '\0')) {
        return false;
    }

    if (val > INT_MAX || val < INT_MIN) {
        errno = ERANGE;
        return false;
    }

    *v = (int)val;
    return true;
}

int split_host_port(char* host_port) {
    char* colon = strstr(host_port, ":");
    if (colon == NULL) {
        LOG_ERROR(log_beep_main, "No colon in host:port: %s\n", host_port);
        exit(1);
    }
    *colon = '\0';

    char* port = colon + 1;
    int val;
    if (!beep_strtoi(port, &val)) {
        LOG_ERROR(log_beep_main, "Port not an integer: %s\n", port);
        exit(1);
    }
    return val;
}

// From: http://stackoverflow.com/questions/2509679/how-to-generate-a-random-number-from-within-a-range-c
/* Would like a semi-open interval [min, max) */
int random_in_range (unsigned int min, unsigned int max) {
    int base_random = rand(); /* in [0, RAND_MAX] */
    if (RAND_MAX == base_random) return random_in_range(min, max);
    /* now guaranteed to be in [0, RAND_MAX) */
    int range       = max - min,
        remainder   = RAND_MAX % range,
        bucket      = RAND_MAX / range;
    /* There are range buckets, plus one smaller interval
       within remainder of RAND_MAX */
    if (base_random < RAND_MAX - remainder) {
        return min + base_random/bucket;
    } else {
        return random_in_range (min, max);
    }
}

void sleep_random_millis(unsigned int min, unsigned int max) {
    usleep(random_in_range(min * 1000, max * 1000));
}

void print_trace() {
#ifdef _MIPS_ARCH
    printf("Can\'t print_trace() on mips\n");
#else
    void *array[10];
    size_t size;
    char **strings;
    size_t i;

    size = backtrace (array, 10);
    strings = backtrace_symbols (array, size);

    fprintf (stderr, "Obtained %zd stack frames.\n", size);

    for (i = 0; i < size; i++)
       fprintf (stderr, "    %s\n", strings[i]);

    free (strings);
#endif  // _MIPS_ARCH
}

const char* get_caller() {
#ifdef _MIPS_ARCH
    return "(no caller available on mips)";
#else
    void *array[3];
    size_t size;
    char **strings;
    char* result;

    size = backtrace (array, 3);
    strings = backtrace_symbols (array, size);

    result = strings[2];

    return result;
#endif  // _MIPS_ARCH

}
