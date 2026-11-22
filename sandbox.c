#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

enum Stage { ENVIRONMENT, DIRECTORY, SIGNALS, GET_CPU, SET_CPU, GET_FILE, SET_FILE, EXEC };
struct Failure { enum Stage stage; int error; };

static const char *const stage_names[] = {
    "environment", "chdir", "signal setup", "getrlimit(CPU)", "setrlimit(CPU)",
    "getrlimit(FSIZE)", "setrlimit(FSIZE)", "execve"
};

static void child_error(int fd, enum Stage stage)
{
    struct Failure failure = {stage, errno};
    while (write(fd, &failure, sizeof failure) == -1 && errno == EINTR) {}
    _exit(125);
}

static int parse_limit(const char *value, rlim_t *limit)
{
    if (!*value || value[strspn(value, "0123456789")])
        return -1;

    errno = 0;
    uintmax_t number = strtoumax(value, NULL, 10);
    *limit = (rlim_t)number;
    return errno || (uintmax_t)*limit != number || *limit == RLIM_INFINITY ? -1 : 0;
}

static void apply_limit(int fd, int resource, rlim_t soft,
                        enum Stage get_stage, enum Stage set_stage)
{
    if (soft == RLIM_INFINITY)
        return;

    struct rlimit cap;
    if (getrlimit(resource, &cap) == -1)
        child_error(fd, get_stage);
    cap.rlim_cur = soft;
    if (setrlimit(resource, &cap) == -1)
        child_error(fd, set_stage);
}

static void reset_limit_signals(int fd)
{
    struct sigaction action = {.sa_handler = SIG_DFL};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGXCPU, &action, NULL) == -1 ||
        sigaction(SIGXFSZ, &action, NULL) == -1)
        child_error(fd, SIGNALS);

    sigset_t unblock;
    sigemptyset(&unblock);
    sigaddset(&unblock, SIGXCPU);
    sigaddset(&unblock, SIGXFSZ);
    if (sigprocmask(SIG_UNBLOCK, &unblock, NULL) == -1)
        child_error(fd, SIGNALS);
}

static void usage(FILE *output)
{
    fputs("Usage: ./sandbox [--cwd DIR] [--keep NAME] [--env NAME=VALUE]\n"
          "                 [--unset NAME] [--cpu SECONDS] [--fsize BYTES]\n"
          "                 -- /absolute/program [ARG ...]\n"
          "Environment starts empty. Options are applied left to right.\n", output);
}

static int valid_name(const char *name)
{
    return *name && !strchr(name, '=');
}

static int build_environment(char **inherited, char **options, int end)
{
    // Save requested inherited values before switching to the clean environment.
    for (int i = 1; i < end; i += 2)
        if (!strcmp(options[i], "--keep"))
            inherited[i] = getenv(options[i + 1]);

    static char *empty[] = {NULL};
    environ = empty;

    for (int i = 1; i < end; i += 2) {
        char *option = options[i];
        char *value = options[i + 1];
        int result = 0;
        if (!strcmp(option, "--keep") && inherited[i])
            result = setenv(value, inherited[i], 1);
        if (!strcmp(option, "--env")) {
            char *separator = strchr(value, '=');
            *separator = '\0';
            result = setenv(value, separator + 1, 1);
            *separator = '=';
        }
        if (!strcmp(option, "--unset"))
            result = unsetenv(value);
        if (result == -1)
            return -1;
    }
    return 0;
}

static int report_exit(pid_t pid, int status, double elapsed, const struct Failure *failure)
{
    fprintf(stderr, "child_pid=%jd elapsed_seconds=%.3f ", (intmax_t)pid, elapsed);
    if (failure) {
        fprintf(stderr, "exit_status=%d setup_failure=%s: %s\n",
                WEXITSTATUS(status), stage_names[failure->stage], strerror(failure->error));
        return 125;
    }
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        fprintf(stderr, "exit_status=%d\n", code);
        return code;
    }

    int signum = WTERMSIG(status);
    fprintf(stderr, "signal=%d (%s)", signum, strsignal(signum));
    if (signum == SIGXCPU)
        fputs(" limit_failure=CPU time", stderr);
    if (signum == SIGXFSZ)
        fputs(" limit_failure=file size", stderr);
    fputc('\n', stderr);
    return 128 + signum;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        usage(stdout);
        return 0;
    }

    const char *directory = NULL;
    rlim_t cpu = RLIM_INFINITY;
    rlim_t file_size = RLIM_INFINITY;
    int end = 1;
    for (; end < argc && strcmp(argv[end], "--"); end += 2) {
        if (end + 1 == argc)
            goto invalid;
        const char *option = argv[end];
        const char *value = argv[end + 1];
        if (!strcmp(option, "--cwd"))
            directory = value;
        else if (!strcmp(option, "--cpu") && !parse_limit(value, &cpu) && cpu > 0)
            continue;
        else if (!strcmp(option, "--fsize") && !parse_limit(value, &file_size))
            continue;
        else if ((!strcmp(option, "--keep") || !strcmp(option, "--unset")) &&
                 valid_name(value))
            continue;
        else if (!strcmp(option, "--env") && value[0] != '=' && strchr(value, '='))
            continue;
        else
            goto invalid;
    }
    if (end + 1 >= argc || argv[end + 1][0] != '/')
        goto invalid;

    char **inherited = calloc((size_t)argc, sizeof *inherited);
    if (!inherited) {
        perror("calloc");
        return 125;
    }

    // The child writes setup errors here. execve closes the write end on success.
    int errors[2];
    struct timespec start, finish;
    if (pipe(errors) == -1 ||
        fcntl(errors[1], F_SETFD, FD_CLOEXEC) == -1 ||
        clock_gettime(CLOCK_MONOTONIC, &start) == -1) {
        perror("launcher setup");
        return 125;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 125;
    }
    if (pid == 0) {
        close(errors[0]);
        if (build_environment(inherited, argv, end) == -1) {
            // Environment construction uses the same reporting channel as exec.
            child_error(errors[1], ENVIRONMENT);
        }
        if (directory && chdir(directory) == -1)
            child_error(errors[1], DIRECTORY);
        reset_limit_signals(errors[1]);
        apply_limit(errors[1], RLIMIT_CPU, cpu, GET_CPU, SET_CPU);
        apply_limit(errors[1], RLIMIT_FSIZE, file_size, GET_FILE, SET_FILE);
        execve(argv[end + 1], &argv[end + 1], environ);
        child_error(errors[1], EXEC);
    }

    free(inherited);
    close(errors[1]);
    int status;
    pid_t result;
    do {
        result = waitpid(pid, &status, 0);
    } while (result == -1 && errno == EINTR);
    if (result == -1 || clock_gettime(CLOCK_MONOTONIC, &finish) == -1) {
        perror("wait or clock");
        return 125;
    }

    struct Failure failure;
    ssize_t read_size;
    do {
        read_size = read(errors[0], &failure, sizeof failure);
    } while (read_size == -1 && errno == EINTR);
    close(errors[0]);
    if (read_size != 0 && read_size != (ssize_t)sizeof failure) {
        fprintf(stderr, "child_pid=%jd error reading child setup status\n", (intmax_t)pid);
        return 125;
    }

    double elapsed = (double)(finish.tv_sec - start.tv_sec)
                   + (finish.tv_nsec - start.tv_nsec) / 1000000000.0;
    return report_exit(pid, status, elapsed, read_size ? &failure : NULL);

invalid:
    usage(stderr);
    return 2;
}
