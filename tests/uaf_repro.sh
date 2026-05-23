#!/usr/bin/env bash
# Reproduces the BiSheng C codegen UAF — BSC-IMPROVEMENTS.md issue #1.
# Builds the buggy tail-return form (-DREPRO_BAD) and the bind-then-return fix from the
# SAME source, then runs both under valgrind. Expected: BAD fails (Invalid read freed by
# String_D), GOOD is clean. Both compile without a single borrow-checker complaint.
set -u
cd "$(dirname "$0")/.."

CC=/home/zly/bsc/llvm-project/build/bin/clang
INC=-I/home/zly/bsc/llvm-project/install/include/libcbs
LIB=(-L/home/zly/bsc/llvm-project/install/lib -lstdcbs -lpthread)
SRC=tests/uaf_repro.cbs

WARN="-Wno-nullability-completeness"
$CC -g $WARN "$INC" -DREPRO_BAD "$SRC" -o /tmp/uaf_bad  "${LIB[@]}" || { echo "BAD build failed";  exit 2; }
$CC -g $WARN "$INC"             "$SRC" -o /tmp/uaf_good "${LIB[@]}" || { echo "GOOD build failed"; exit 2; }
echo "both forms compiled cleanly (borrow checker accepted the buggy form)"

echo; echo "=== BAD: return count_a(&_Const h)  (expect Invalid read) ==="
valgrind -q --error-exitcode=1 /tmp/uaf_bad
bad_rc=$?
echo "[bad valgrind rc=$bad_rc — nonzero = UAF reproduced]"

echo; echo "=== GOOD: bind-then-return  (expect clean) ==="
valgrind -q --error-exitcode=1 /tmp/uaf_good
good_rc=$?
echo "[good valgrind rc=$good_rc — 0 = clean]"

echo; echo "----"
if [ "$bad_rc" -ne 0 ] && [ "$good_rc" -eq 0 ]; then
    echo "REPRODUCED: buggy form is UAF under valgrind, fix is clean."
    exit 0
else
    echo "NOT reproduced as expected (bad_rc=$bad_rc good_rc=$good_rc) on this toolchain."
    exit 1
fi
