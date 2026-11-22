#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int show_environment(int argc, char **argv)
{
    char *cwd = getcwd(NULL, 0);
    if (!cwd) {
        perror("getcwd");
        return 1;
    }
    printf("cwd=%s\n", cwd);
    free(cwd);

    for (int i = 2; i < argc; ++i) {
        const char *value = getenv(argv[i]);
        printf("%s=%s\n", argv[i], value ? value : "(unset)");
    }
    return 0;
}

static int use_cpu(void)
{
    // CLOCK_PROCESS_CPUTIME_ID counts time spent running on the CPU.
    struct timespec start, now;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start) == -1) {
        perror("CPU clock");
        return 1;
    }
    double elapsed;
    do {
        if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now) == -1) {
            perror("CPU clock");
            return 1;
        }
        elapsed = (double)(now.tv_sec - start.tv_sec)
                + (now.tv_nsec - start.tv_nsec) / 1000000000.0;
    } while (elapsed < 2.0);
    puts("Completed two seconds of CPU work.");
    return 0;
}

static int write_file(const char *path)
{
    // Require a new file so the demo cannot overwrite an existing one.
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd == -1) {
        perror(path);
        return 1;
    }
    char block[512] = {0};
    for (size_t total = 0; total < 4096;) {
        ssize_t written = write(fd, block, sizeof block);
        if (written <= 0) {
            perror("write");
            close(fd);
            return 1;
        }
        total += (size_t)written;
    }
    if (close(fd) == -1) {
        perror("close");
        return 1;
    }
    puts("Wrote 4096 bytes.");
    return 0;
}

int main(int argc, char **argv)
{
    printf("program_pid=%jd\n", (intmax_t)getpid());
    fflush(stdout);
    if (argc >= 2 && !strcmp(argv[1], "inspect"))
        return show_environment(argc, argv);
    if (argc == 2 && !strcmp(argv[1], "cpu"))
        return use_cpu();
    if (argc == 3 && !strcmp(argv[1], "file"))
        return write_file(argv[2]);
    fprintf(stderr, "Usage: demo_program inspect [NAME ...] | cpu | file NEW_PATH\n");
    return 2;
}
