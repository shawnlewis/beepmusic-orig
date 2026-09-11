#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: pseudorandomsleep <min_secs> <max_secs>\n");
        return 1;
    }
    int min = atoi(argv[1]);
    int max = atoi(argv[2]);
    int size = max - min;

    char hostname[128];
    gethostname(hostname, 128);

    int len_hostname = strlen(hostname);
    int seed;
    memcpy(&seed, &hostname[len_hostname - 4], 4);
    fprintf(stderr, "seed: %d\n", seed);
    srand(seed);

    // Not exactly uniform but close enough
    int duration_ms = min + (rand() % (size * 1000));

    fprintf(stderr, "Duration: %dms\n", duration_ms);

    usleep(duration_ms * 1000);

    return 0;
}
