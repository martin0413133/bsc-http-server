#!/usr/bin/env bash
# Enforce the layering rule: business code (src/*.cbs, top level) must contain NO _Unsafe.
# All _Unsafe must live in the adapter layer (src/platform/*.cbs).
set -u
cd "$(dirname "$0")/.."

violations=0
for f in src/*.cbs; do
    [ -e "$f" ] || continue
    if grep -nq '_Unsafe' "$f"; then
        echo "VIOLATION: business file '$f' contains _Unsafe:"
        grep -n '_Unsafe' "$f" | sed 's/^/    /'
        violations=1
    fi
done

if [ "$violations" -ne 0 ]; then
    echo "FAIL: business layer must be _Unsafe-free (move syscalls into src/platform/ adapters)."
    exit 1
fi
echo "ok: business layer (src/*.cbs) is _Unsafe-free"
exit 0
