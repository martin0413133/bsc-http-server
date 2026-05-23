#!/usr/bin/env bash
# Integration tests for cc_httpd: AC-2 (static 200), AC-3 (404), AC-4 (dynamic route),
# AC-5 (concurrency), plus path-traversal protection.
set -u
cd "$(dirname "$0")/.."

make >/tmp/cc_build.log 2>&1 || { echo "BUILD FAILED"; cat /tmp/cc_build.log; exit 1; }

PORT=8080
./bin/httpd >/tmp/cc_httpd.log 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
sleep 0.6

fail=0
chk() { if [ "$1" != "$2" ]; then echo "FAIL: $3 (got '$1' want '$2')"; fail=1; else echo "ok: $3"; fi; }

# AC-2: static file 200 + content type
code=$(curl -s -o /dev/null -w '%{http_code}' localhost:$PORT/)
chk "$code" "200" "AC-2 GET / returns 200"
ctype=$(curl -s -D - -o /dev/null localhost:$PORT/ | tr -d '\r' | awk 'tolower($1)=="content-type:"{print $2}')
chk "$ctype" "text/html" "AC-2 index Content-Type text/html"
body=$(curl -s localhost:$PORT/)
case "$body" in *"cc_httpd"*) echo "ok: AC-2 index body served";; *) echo "FAIL: AC-2 index body"; fail=1;; esac

# CSS MIME
ctype=$(curl -s -D - -o /dev/null localhost:$PORT/style.css | tr -d '\r' | awk 'tolower($1)=="content-type:"{print $2}')
chk "$ctype" "text/css" "MIME .css -> text/css"

# AC-3: 404
code=$(curl -s -o /dev/null -w '%{http_code}' localhost:$PORT/nope.html)
chk "$code" "404" "AC-3 missing file 404"

# AC-4: dynamic route
body=$(curl -s localhost:$PORT/hello)
case "$body" in *"cc_httpd route"*) echo "ok: AC-4 /hello dynamic route";; *) echo "FAIL: AC-4 /hello body ('$body')"; fail=1;; esac

# Path traversal blocked
code=$(curl -s -o /dev/null -w '%{http_code}' --path-as-is localhost:$PORT/../Makefile)
chk "$code" "404" "path traversal blocked (404)"

# AC-5: concurrency — 50 parallel requests all 200
rm -f /tmp/cc_conc.out
for i in $(seq 1 50); do
  ( c=$(curl -s -o /dev/null -w '%{http_code}' localhost:$PORT/); [ "$c" = "200" ] && echo y >> /tmp/cc_conc.out ) &
done
wait
ok=$(wc -l < /tmp/cc_conc.out 2>/dev/null | tr -d ' ')
chk "$ok" "50" "AC-5 50 concurrent requests all 200"

echo "----"
if [ "$fail" -eq 0 ]; then echo "ALL INTEGRATION TESTS PASSED"; else echo "SOME TESTS FAILED"; fi
exit $fail
