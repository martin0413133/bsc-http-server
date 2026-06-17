#!/usr/bin/env bash
set -u
# 先杀掉可能残留的旧实例，避免端口占用
pkill -f 'bin/httpd' 2>/dev/null
make >/dev/null || { echo "BUILD FAILED"; exit 1; }
./bin/httpd >/tmp/cc_httpd.log 2>&1 &
SRV=$!
sleep 0.5
fail=0
chk() { if [ "$1" != "$2" ]; then echo "FAIL: $3 (got '$1' want '$2')"; fail=1; else echo "ok: $3"; fi; }

# AC-2 静态 200
code=$(curl -s --max-time 5 -o /dev/null -w '%{http_code}' localhost:8080/)
chk "$code" "200" "AC-2 GET / returns 200"
ctype=$(curl -s --max-time 5 -D - -o /dev/null localhost:8080/ | grep -i '^content-type' | tr -d '\r' | awk '{print $2}')
chk "$ctype" "text/html" "AC-2 index Content-Type"

# AC-3 404
code=$(curl -s --max-time 5 -o /dev/null -w '%{http_code}' localhost:8080/nope.html)
chk "$code" "404" "AC-3 missing file 404"

# AC-4 动态路由
body=$(curl -s --max-time 5 localhost:8080/hello)
case "$body" in *"cc_httpd route"*) echo "ok: AC-4 /hello body";; *) echo "FAIL: AC-4 /hello body"; fail=1;; esac

# 路径穿越 -> 404
code=$(curl -s --max-time 5 -o /dev/null -w '%{http_code}' --path-as-is localhost:8080/../Makefile)
chk "$code" "404" "traversal blocked"

# AC-5 并发：50 个并行请求全部 200
# 只 wait 这些 curl 子进程的 PID（不能裸 wait——那会等永不退出的服务器 $SRV）
cpids=""
for i in $(seq 1 50); do
  ( [ "$(curl -s --max-time 5 -o /dev/null -w '%{http_code}' localhost:8080/)" = "200" ] && echo y ) &
  cpids="$cpids $!"
done > /tmp/cc_conc.out
wait $cpids
ok=$(grep -c y /tmp/cc_conc.out 2>/dev/null || echo 0)
chk "$ok" "50" "AC-5 50 concurrent requests"

kill $SRV 2>/dev/null
exit $fail
