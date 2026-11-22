#!/bin/sh
# Expected child failures are checked and the demonstration continues.
set -eu
project_dir=$(pwd -P)
launcher="$project_dir/sandbox"
workload="$project_dir/demo_program"
demo_dir=$(mktemp -d)
trap 'rm -rf -- "$demo_dir"' EXIT
export USP_KEEP=from_caller USP_REMOVE=caller_value USP_HIDDEN=caller_only

run() {
    expected=$1
    shift
    status=0
    "$launcher" "$@" || status=$?
    if [ "$status" -ne "$expected" ]; then
        printf 'Expected launcher exit %s, got %s\n' "$expected" "$status" >&2
        exit 1
    fi
}

printf '\n1. Controlled environment and working directory\n'
run 0 --cwd "$demo_dir" --keep USP_KEEP --env USP_MODE=demo \
    --keep USP_REMOVE --unset USP_REMOVE -- "$workload" inspect \
    USP_KEEP USP_MODE USP_REMOVE USP_HIDDEN

printf '\n2. File size comparison (4096 and 1024 bytes)\n'
run 0 --cwd "$demo_dir" --fsize 4096 -- "$workload" file allowed.bin
run 153 --cwd "$demo_dir" --fsize 1024 -- "$workload" file capped.bin
wc -c "$demo_dir/allowed.bin" "$demo_dir/capped.bin"

printf '\n3. CPU comparison (4 and 1 CPU seconds)\n'
run 0 --cpu 4 -- "$workload" cpu
run 152 --cpu 1 -- "$workload" cpu

printf '\n4. Child setup errors and ordinary nonzero exit\n'
run 125 --cwd "$demo_dir/missing" -- "$workload" inspect
run 125 -- "$demo_dir/missing-program"
run 7 -- /bin/sh -c 'exit 7'

printf '\n5. Caller state after the runs\n'
printf 'cwd=%s\nUSP_KEEP=%s\nUSP_REMOVE=%s\nUSP_HIDDEN=%s\n' \
    "$(pwd -P)" "$USP_KEEP" "$USP_REMOVE" "$USP_HIDDEN"
test "$(pwd -P)" = "$project_dir"
test "$USP_KEEP" = from_caller
test "$USP_REMOVE" = caller_value
test "$USP_HIDDEN" = caller_only
