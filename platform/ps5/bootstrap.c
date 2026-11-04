/* Portable probe contract. This host executable is not a PS5 payload. */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef PE_PS5_HOST_VALIDATE
#error A verified PS5 runtime adapter is required before compiling a payload
#endif

static int read_probe(const char *path) {
    unsigned char buffer[1024];
    size_t total = 0, count;
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "file-read=failed errno=%d\n", errno);
        return 1;
    }
    while ((count = fread(buffer, 1, sizeof buffer, file)) != 0) total += count;
    int failed = ferror(file);
    if (fclose(file) != 0) failed = 1;
    printf("file-read=%s bytes=%zu\n", failed ? "failed" : "passed", total);
    return failed;
}

static int write_probe(const char *path) {
    static const char marker[] = "pe-ps5-bootstrap probe\n";
    char actual[sizeof marker] = {0};
    /* Exclusive creation: never truncate a save, asset, or existing probe. */
    FILE *file = fopen(path, "wbx");
    if (!file) {
        fprintf(stderr, "file-write=failed errno=%d (requires unused scratch path)\n", errno);
        return 1;
    }
    int failed = fwrite(marker, 1, sizeof marker - 1, file) != sizeof marker - 1;
    if (fflush(file) != 0) failed = 1;
    if (fclose(file) != 0) failed = 1;
    file = fopen(path, "rb");
    if (!file) failed = 1;
    else {
        if (fread(actual, 1, sizeof actual, file) != sizeof marker - 1 ||
            ferror(file) || memcmp(actual, marker, sizeof marker - 1) != 0) failed = 1;
        if (fclose(file) != 0) failed = 1;
    }
    if (remove(path) != 0) failed = 1;
    printf("file-write-roundtrip=%s\n", failed ? "failed" : "passed");
    return failed;
}

int main(int argc, char **argv) {
    puts("boot=host-validation-only log=stdio");
    if (fflush(stdout) != 0) return 1;
    if (argc != 3) {
        fprintf(stderr, "usage: %s READ_FIXTURE UNUSED_SCRATCH_PATH\n", argv[0]);
        return 1;
    }
    int failed = read_probe(argv[1]);
    failed |= write_probe(argv[2]);
    puts("controller=unavailable graphics-clear=unavailable ps5-runtime=unverified");
    /* 2 denotes unavailable capability; passing portable I/O is not a port. */
    return failed ? 1 : 2;
}
