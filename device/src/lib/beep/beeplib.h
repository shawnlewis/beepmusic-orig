#ifndef BEEPLIB_H
#define BEEPLIB_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>


// Get thread ID
pid_t beep_gettid(void);

// File stuff

// Reads in contents of entire file.
// Caller owns the returned string.
// This function uses an unbounded amount of memory.
char* beep_slurp(char* path);

// String stuff

typedef struct {
    char* buf;
    unsigned int len;
} BeepString;


// Returns a copy of <target> with <find> replaced with <replace>
// Caller owns the returned string.
char* beep_str_replace_one(char* target, char* find, const char* replace);

#define URLPARSE_SCHEME_LEN        10
#define URLPARSE_HOST_LEN          100
#define URLPARSE_PATH_LEN          2000


// Somewhat braindead simple url parser.
// WARNING: Probably not secure. Has arbitrary limits on returned string
//    lengths. May create less than ideal matches.
bool beep_urlparse(
        const char *s, char* scheme, char* host,
        int *port, char* path);

// A simple vector implementation

typedef enum {
    BEEP_SUCCESS,
    BEEP_VECTOR_OUT_OF_RANGE,
    BEEP_VECTOR_FULL,
} BeepVectorResult;

typedef struct {
    // constants
    int el_size;
    int max_elements;

    // variables
    void *data;
    int num_elements;
} BeepStaticVector;

#define VECTOR_INDEX(v, i) ((v->data) + (i * v->el_size))

BeepStaticVector* beep_new_vector (int max_elements, int el_size);
int beep_vector_is_full(BeepStaticVector* v);
void* beep_vector_index(BeepStaticVector* v, int i);
BeepVectorResult beep_vector_append(BeepStaticVector* v, void* el);
BeepVectorResult beep_vector_pop(BeepStaticVector* v, unsigned int i);


// Net protocol.

int net_readint(uint8_t* buf);
void net_writeint(uint8_t* buf, int val);

uint8_t* lenbuf(uint8_t* buf, int len);


// Other

void dump_hex(const unsigned char* buf, size_t len);

bool beep_strtoll(char *s, long long int *v);
bool beep_strtol(char *s, long int *v);
bool beep_strtoi(char *s, int *v);

// Returns port, or -1 on error.
// host_port will be modified and will be null-terminated after host.
int split_host_port(char* host_port);

int random_in_range (unsigned int min, unsigned int max);

// This function needs to be kept in sync with the version in
// audio/decode/decode_priv.h
static inline uint64_t beep_millis(void)  {
    struct timespec now;

    //clock_gettime(CLOCK_MONOTONIC, &now);
    clock_gettime(CLOCK_REALTIME, &now);
    return (uint64_t) ((uint64_t) now.tv_sec * 1000ull)
            + ((uint64_t) now.tv_nsec / 1000000ull);
}

// Debugging

void sleep_random_millis(unsigned int min, unsigned int max);

void print_trace(void);
const char* get_caller(void);

#endif
