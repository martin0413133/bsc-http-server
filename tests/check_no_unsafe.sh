#!/usr/bin/env bash
# 业务核心 src/*.cbs（不含 src/platform/）禁止出现 _Unsafe。
set -u
viol=0
for f in src/*.cbs; do
    if grep -n '_Unsafe' "$f" >/dev/null 2>&1; then
        echo "FAIL: _Unsafe found in business core: $f"
        grep -n '_Unsafe' "$f"
        viol=1
    fi
done
if [ "$viol" = "0" ]; then echo "ok: no _Unsafe in business core"; fi
exit $viol
