# Process Environment and Resource-Limit Sandbox

A Linux command-line launcher for trusted programs, built in C for the
Unix System Programming assignment. It gives a child process a selected
environment, a working directory and optional CPU and per-file size limits.
The parent waits for the child and reports its result on standard error.

## Build and run

On Linux or Ubuntu in WSL, install a C compiler and Make. On Ubuntu these
are available through `sudo apt install build-essential`.

```sh
make
make demo
```

Run a single example from the project directory:

```sh
export USP_KEEP=hello
./sandbox --cwd /tmp --keep USP_KEEP --env USP_MODE=demo -- \
    "$PWD/demo_program" inspect USP_KEEP USP_MODE HOME
```

The child prints its PID, directory and requested variables. The launcher
prints the same PID, elapsed wall time and process outcome.

## Options

| Option | Meaning |
| --- | --- |
| `--cwd DIR` | Start the child in DIR. Relative directories resolve from the caller's directory. |
| `--keep NAME` | Copy NAME from the launcher's inherited environment if present. |
| `--env NAME=VALUE` | Set a variable in the child. Quote the whole assignment if it contains spaces. |
| `--unset NAME` | Remove a variable from the child's environment. |
| `--cpu SECONDS` | Set a positive whole-second CPU soft limit. |
| `--fsize BYTES` | Set a nonnegative per-file size soft limit in bytes. Zero prevents file growth. |
| `-- /absolute/program [ARG ...]` | End launcher options and pass the executable and arguments directly to `execve`. |

The environment starts empty. Variable options run left to right.
Omitting the directory or a limit keeps the corresponding inherited setting.
The inherited hard limits constrain how high the soft limits may be set.
An absolute executable path avoids any dependency on PATH.

## Demonstration

| Workload | Higher limit | Lower limit |
| --- | --- | --- |
| Two CPU seconds of work | 4 seconds, exit 0 | 1 second, SIGXCPU |
| Write a new 4,096-byte file | 4,096 bytes, exit 0 | 1,024 bytes, SIGXFSZ, partial file remains |

`make demo` also shows missing-directory and missing-executable errors,
an ordinary exit status of 7, and the caller's directory and variables
after the child exits. It uses a temporary directory and deletes its demo files.

## How it works

1. The launcher parses its arguments and starts a monotonic wall clock.
2. `fork` makes a child. The child reads selected inherited values with
   `getenv`, builds a clean environment using `setenv` and `unsetenv`,
   changes directory with `chdir` and configures limits using
   `getrlimit` and `setrlimit`.
3. `execve` replaces the child with the trusted program, passing `argv`
   and the new environment vector (`envp`). The PID stays the same.
4. `waitpid` collects the result. A close-on-exec pipe lets the parent
   distinguish a setup error from an ordinary exit with the same status.

The launcher propagates normal exit codes, uses 125 for setup failures,
2 for invalid usage and 128 plus the signal number for signal termination.
Each setup failure includes the failing operation and system error.
SIGXCPU and SIGXFSZ are reported as limit signals. A program can handle
these signals itself. SIGKILL has several possible causes and is reported
without attributing it to a particular limit.

CPU time measures time spent executing. The elapsed time field measures
wall time. The file size limit applies to the size of each file. Trusted
programs retain their normal filesystem and network permissions.

See [Lecturer_Explanation_Guide.txt](Lecturer_Explanation_Guide.txt) for the
live-demo script, talking points and assignment mapping.
