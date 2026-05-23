#!/usr/bin/env bash
# Build and valgrind every unit test. BSC tests can PASS while memory-unsafe
# (e.g. use-after-free on freed-but-intact memory), so this catches what exit codes miss.
set -u
cd "$(dirname "$0")/.."

UNITS="str_util mime http_request http_response config router path_guard fs file_server handler"
fail=0
for t in $UNITS; do
    make test-$t >/dev/null 2>/tmp/cc_b.err || { echo "$t: BUILD ERROR"; grep error: /tmp/cc_b.err | head -3; fail=1; continue; }
    if valgrind -q --error-exitcode=1 --leak-check=full ./bin/test_$t >/tmp/cc_vg.out 2>&1; then
        echo "$t: ok (valgrind clean)"
    else
        echo "$t: VALGRIND FAILURE"
        grep -E 'Invalid|definitely lost|freed|by 0x' /tmp/cc_vg.out | head -10
        fail=1
    fi
done
exit $fail
