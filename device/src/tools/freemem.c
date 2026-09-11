#include <stdlib.h>
#include <stdio.h>

#define CHUNK_SIZE 64 * 1024

int main(int argc, char **argv) {
    int total_size = 0;
    while (1) {
        if (calloc(1, CHUNK_SIZE) == NULL) {
            fprintf(stderr, "OOM\n");
            exit(0);
        }
        total_size += CHUNK_SIZE;
        fprintf(stderr, "have %.4fM\n", (float) total_size / (1024 * 1024));
    }
}
