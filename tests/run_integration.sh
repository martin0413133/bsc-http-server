#!/usr/bin/env bash
# Integration tests for cc_httpd: AC-2 (static 200), AC-3 (404), AC-4 (dynamic route),
# AC-5 (concurrency), plus path-traversal protection and 405 method handling.
set -u
cd "$(dirname "$0")/.."

# Talk to our own loopback server directly: ignore any ambient HTTP proxy that would
# otherwise intercept localhost requests (env may set HTTP_PROXY/HTTPS_PROXY).
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY
export no_proxy='*' NO_PROXY='*'

make >/tmp/cc_build.log 2>&1 || { echo "BUILD FAILED"; cat /tmp/cc_build.log; exit 1; }

# Pick a free port — 8080 (the config default) is often taken by another dev server.
PORT=""
for p in $(seq 8973 9099); do
  if ! ss -ltn 2>/dev/null | grep -qE "[:.]$p[[:space:]]"; then PORT=$p; break; fi
done
[ -n "$PORT" ] || { echo "no free port found"; exit 1; }

# Run with a temporary config on that port; restore the real config on exit.
cp config.ini /tmp/cc_config.bak
printf 'port = %s\ndocument_root = www\nthreads = 4\n' "$PORT" > config.ini

pkill -9 -f 'bin/httpd' 2>/dev/null; sleep 0.3
./bin/httpd >/tmp/cc_httpd.log 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null; mv -f /tmp/cc_config.bak config.ini 2>/dev/null' EXIT
sleep 0.6

# Confirm OUR server bound the port (else curls would silently hit a stranger).
grep -q "listening on port $PORT" /tmp/cc_httpd.log || { echo "SERVER FAILED TO START"; cat /tmp/cc_httpd.log; exit 1; }

H="127.0.0.1:$PORT"
fail=0
chk() { if [ "$1" != "$2" ]; then echo "FAIL: $3 (got '$1' want '$2')"; fail=1; else echo "ok: $3"; fi; }

# AC-2: static file 200 + content type
code=$(curl -s -o /dev/null -w '%{http_code}' $H/)
chk "$code" "200" "AC-2 GET / returns 200"
ctype=$(curl -s -D - -o /dev/null $H/ | tr -d '\r' | awk 'tolower($1)=="content-type:"{print $2}')
chk "$ctype" "text/html" "AC-2 index Content-Type text/html"
body=$(curl -s $H/)
case "$body" in *"cc_httpd"*) echo "ok: AC-2 index body served";; *) echo "FAIL: AC-2 index body"; fail=1;; esac

# CSS MIME
ctype=$(curl -s -D - -o /dev/null $H/style.css | tr -d '\r' | awk 'tolower($1)=="content-type:"{print $2}')
chk "$ctype" "text/css" "MIME .css -> text/css"

# AC-3: 404
code=$(curl -s -o /dev/null -w '%{http_code}' $H/nope.html)
chk "$code" "404" "AC-3 missing file 404"

# AC-4: dynamic route
body=$(curl -s $H/hello)
case "$body" in *"cc_httpd route"*) echo "ok: AC-4 /hello dynamic route";; *) echo "FAIL: AC-4 /hello body ('$body')"; fail=1;; esac

# Path traversal blocked
code=$(curl -s -o /dev/null -w '%{http_code}' --path-as-is $H/../Makefile)
chk "$code" "404" "path traversal blocked (404)"

# Non-GET method on a static path -> 405
code=$(curl -s -o /dev/null -w '%{http_code}' -X POST $H/)
chk "$code" "405" "POST / returns 405"

# AC-5: concurrency — 50 parallel requests all 200.
# Wait only on the curl PIDs: a bare `wait` would also block on $SRV (runs forever).
rm -f /tmp/cc_conc.out
pids=""
for i in $(seq 1 50); do
  ( c=$(curl -s --max-time 10 -o /dev/null -w '%{http_code}' $H/); [ "$c" = "200" ] && echo y >> /tmp/cc_conc.out ) &
  pids="$pids $!"
done
wait $pids
ok=$(wc -l < /tmp/cc_conc.out 2>/dev/null | tr -d ' ')
chk "$ok" "50" "AC-5 50 concurrent requests all 200"

echo "----"
if [ "$fail" -eq 0 ]; then echo "ALL INTEGRATION TESTS PASSED"; else echo "SOME TESTS FAILED"; fi
exit $fail
