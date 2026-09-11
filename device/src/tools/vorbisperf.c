/* Decodes a vorbis file as fast as possible */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define CHUNK_SIZE 64 * 1024

#include <tremor/ivorbiscodec.h>
#include <tremor/ivorbisfile.h>

#define OUTPUT_BUFFER_SIZE 4096
char output_buffer[OUTPUT_BUFFER_SIZE];

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: vorbisperf <filename>\n");
        exit(1);
    }

    OggVorbis_File vf;
    memset(&vf, 0, sizeof(vf));

    FILE* file = fopen(argv[1], "r");
    if (!file ) {
        printf("Could not open file\n");
        exit(1);
    }

    int r = ov_open(file, &vf, NULL, 0);
    if (r < 0) {
        printf("Error ov_fopen returned: %d\n", r);
        exit(1);
    }

    struct timeval start, end;

    gettimeofday(&start, NULL);

    int bitstream;
    int bytes = 1;
    while (bytes) {
        bytes = ov_read(&vf, output_buffer, OUTPUT_BUFFER_SIZE, &bitstream);
    }

    gettimeofday(&end, NULL);

    long seconds, useconds;
    seconds  = end.tv_sec  - start.tv_sec;
    useconds = end.tv_usec - start.tv_usec;

    long mtime = ((seconds) * 1000 + useconds/1000.0);

    printf("Elapsed time: %ld milliseconds\n", mtime);
}
