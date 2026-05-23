# cc_httpd — BiSheng C HTTP Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a memory-safe HTTP/1.1 server in BiSheng C that serves static files and dynamic routes, with a pthread thread pool, implemented independently from the PRD (no reading of the sibling `httpx` source).

**Architecture:** Modular single translation unit — one `.hbs`/`.cbs` pair per concern; `src/main.cbs` `#include`s the `.cbs` files in dependency order and is compiled as one unit. Logic modules (str_util, mime, request, response, config, router, file path guard) are `_Safe` and unit-tested by standalone test programs that assert and exit nonzero on failure. IO/threading modules (socket, thread_pool, server) use minimal `_Unsafe` FFI seams and are verified by integration tests with `curl`.

**Tech Stack:** BiSheng C (clang 15.0.4 BSC build), libcbs (`String`, `Vec`), POSIX sockets + pthread via `_Unsafe` FFI.

---

## Validated Toolchain (use exactly these)

```makefile
CC    := /home/zly/bsc/llvm-project/build/bin/clang
INC   := -I/home/zly/bsc/llvm-project/install/include/libcbs
LIB   := -L/home/zly/bsc/llvm-project/install/lib -lstdcbs -lpthread
FLAGS := -Wall -Wextra -Wno-nullability-completeness -g
```

Build:  `$(CC) $(FLAGS) $(INC) src/main.cbs -o bin/httpd $(LIB)`
Syntax-only check a file: `$(CC) $(FLAGS) $(INC) -fsyntax-only <file>` (file must include its deps).
Build a unit test: `$(CC) $(FLAGS) $(INC) tests/test_X.cbs -o bin/test_X $(LIB)` then `./bin/test_X`.

## Validated BSC Facts (do not relearn the hard way)

1. **libcbs `String` is NOT null-terminated.** `as_str()` returns the raw buffer with no `'\0'`. For C APIs needing a C-string (`open`, `%s`), copy into a local `char[]` and set `buf[len] = '\0'`. For socket output use length-based `send`.
2. **Raw `const char*` defaults to nullable.** Dereferencing/indexing one in a `_Safe` function requires `_Nonnull`: `const char* _Nonnull s`.
3. **`printf`/`fprintf` and all syscalls must be inside `_Unsafe { ... }`.**
4. **`String` has no `push_str`/append.** Only `push(char)`. We add `str_append_cstr`/`str_append` helpers.
5. **`String` API:** `new()`, `with_capacity(cap)`, `push(char)`, `length()`, `at(i)`→`char`, `as_str()`, `slice(start,len)`→`String`, `find(char)`→`size_t`, `equals(&other)`, `is_empty()`. `find` returns the string length when not found (treat `>= length()` as "not found").
6. **`Vec<T>` API:** `new()`, `with_capacity`, `push(v)`, `pop()`, `get(i)`→`const T*`, `get_mut(i)`, `set(i,v)`, `length()`, `remove(i)`, `clear()`, `is_empty()`.
7. **Function-pointer typedefs work:** `typedef Response (*Handler)(const Request* _Borrow);`

## Conventions

- Headers `include/*.hbs` declare; sources `src/*.cbs` define and `#include` their own `.hbs`.
- Each `.cbs` `#include`s the libcbs headers and project headers it needs (idempotent via include guards).
- `src/main.cbs` `#include`s every other `.cbs` in dependency order, then defines `main`.
- Commit after each task with the message shown.

## File Structure

```
cc_httpd/
├── Makefile
├── config.ini
├── include/  str_util.hbs http_request.hbs http_response.hbs mime.hbs
│             file_server.hbs router.hbs config.hbs socket.hbs thread_pool.hbs server.hbs
├── src/      (same basenames).cbs  + main.cbs
├── www/      index.html  style.css
└── tests/    test_str_util.cbs test_mime.cbs test_http_request.cbs
              test_http_response.cbs test_config.cbs test_router.cbs
              test_path_guard.cbs  run_integration.sh
```

---

## Task 0: Scaffold + toolchain smoke test

**Files:**
- Create: `Makefile`, `config.ini`, `www/index.html`, `www/style.css`, `tests/smoke.cbs`

- [ ] **Step 1: Write the smoke test** `tests/smoke.cbs`

```c
#include <stdio.h>
#include "string.hbs"
#include "vec.hbs"

int main(void) {
    String s = String::new();
    s.push('h'); s.push('i');
    Vec<int> v = Vec<int>::new();
    v.push(7);
    _Unsafe { printf("smoke len=%zu vec0=%d\n", s.length(), *v.get(0)); }
    return 0;
}
```

- [ ] **Step 2: Write the Makefile**

```makefile
CC    := /home/zly/bsc/llvm-project/build/bin/clang
INC   := -I/home/zly/bsc/llvm-project/install/include/libcbs
LIB   := -L/home/zly/bsc/llvm-project/install/lib -lstdcbs -lpthread
FLAGS := -Wall -Wextra -Wno-nullability-completeness -g
BINDIR := bin

all: $(BINDIR)/httpd

$(BINDIR)/httpd: src/main.cbs $(wildcard src/*.cbs) $(wildcard include/*.hbs) | $(BINDIR)
	$(CC) $(FLAGS) $(INC) src/main.cbs -o $@ $(LIB)

$(BINDIR):
	mkdir -p $(BINDIR)

# Build any test:  make test-X  (compiles tests/test_X.cbs)
test-%: tests/test_%.cbs | $(BINDIR)
	$(CC) $(FLAGS) $(INC) $< -o $(BINDIR)/test_$* $(LIB)
	./$(BINDIR)/test_$*

smoke: tests/smoke.cbs | $(BINDIR)
	$(CC) $(FLAGS) $(INC) $< -o $(BINDIR)/smoke $(LIB)
	./$(BINDIR)/smoke

run: $(BINDIR)/httpd
	./$(BINDIR)/httpd

clean:
	rm -rf $(BINDIR)

.PHONY: all clean run smoke
```

- [ ] **Step 3: Run smoke** — `make smoke`
Expected: builds and prints `smoke len=2 vec0=7`.

- [ ] **Step 4: Create assets**

`config.ini`:
```ini
# cc_httpd configuration
port = 8080
document_root = www
threads = 4
```

`www/index.html`:
```html
<!DOCTYPE html>
<html><head><title>cc_httpd</title><link rel="stylesheet" href="/style.css"></head>
<body><h1>Hello from cc_httpd (BiSheng C)</h1><p>Static file + dynamic routing, memory-safe.</p></body></html>
```

`www/style.css`:
```css
body { font-family: sans-serif; margin: 40px; } h1 { color: #225; }
```

- [ ] **Step 5: Commit**
```bash
git add Makefile config.ini www tests/smoke.cbs
git commit -m "chore: scaffold cc_httpd build + toolchain smoke test"
```

---

## Task 1: str_util — string helpers (TDD)

**Files:** Create `include/str_util.hbs`, `src/str_util.cbs`, `tests/test_str_util.cbs`

- [ ] **Step 1: Write the failing test** `tests/test_str_util.cbs`

```c
#include <stdio.h>
#include <string.h>
#include "../src/str_util.cbs"

static int fails = 0;
#define CHECK(cond, msg) do { if(!(cond)){ _Unsafe{printf("FAIL: %s\n", msg);} fails++; } } while(0)

int main(void) {
    String s = String::new();
    str_append_cstr(&_Mut s, "GET ");
    str_append_cstr(&_Mut s, "/x");
    CHECK(s.length() == 6, "append length");

    // str_to_cbuf null-terminates into a C buffer
    char buf[16];
    str_to_cbuf(&_Const s, buf, 16);
    _Unsafe { CHECK(strcmp(buf, "GET /x") == 0, "to_cbuf content"); }

    // str_eq_cstr
    CHECK(str_eq_cstr(&_Const s, "GET /x"), "eq true");
    CHECK(!str_eq_cstr(&_Const s, "GET /y"), "eq false");

    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_str_util OK\n");}
    return 0;
}
```

- [ ] **Step 2: Verify it fails** — `make test-str_util`
Expected: FAIL — `str_util.cbs` not found / undefined functions.

- [ ] **Step 3: Write `include/str_util.hbs`**

```c
#ifndef CC_STR_UTIL_HBS
#define CC_STR_UTIL_HBS
#include <stddef.h>
#include "string.hbs"

_Safe void str_append_cstr(String* _Borrow s, const char* _Nonnull cstr);
_Safe void str_append(String* _Borrow dst, const String* _Borrow src);
// Copies up to cap-1 bytes of s into out and NUL-terminates. Returns bytes written.
_Safe size_t str_to_cbuf(const String* _Borrow s, char* _Nonnull out, size_t cap);
_Safe _Bool str_eq_cstr(const String* _Borrow s, const char* _Nonnull cstr);
_Safe _Bool str_starts_with(const String* _Borrow s, const char* _Nonnull prefix);
#endif
```

- [ ] **Step 4: Write `src/str_util.cbs`**

```c
#include "../include/str_util.hbs"

_Safe void str_append_cstr(String* _Borrow s, const char* _Nonnull cstr) {
    for (size_t i = 0; cstr[i] != '\0'; i++) { s->push(cstr[i]); }
}

_Safe void str_append(String* _Borrow dst, const String* _Borrow src) {
    size_t n = src->length();
    for (size_t i = 0; i < n; i++) { dst->push(src->at(i)); }
}

_Safe size_t str_to_cbuf(const String* _Borrow s, char* _Nonnull out, size_t cap) {
    if (cap == 0) { return 0; }
    size_t n = s->length();
    if (n > cap - 1) { n = cap - 1; }
    for (size_t i = 0; i < n; i++) { out[i] = s->at(i); }
    out[n] = '\0';
    return n;
}

_Safe _Bool str_eq_cstr(const String* _Borrow s, const char* _Nonnull cstr) {
    size_t i = 0;
    size_t n = s->length();
    while (cstr[i] != '\0') {
        if (i >= n || s->at(i) != cstr[i]) { return 0; }
        i++;
    }
    return i == n;
}

_Safe _Bool str_starts_with(const String* _Borrow s, const char* _Nonnull prefix) {
    size_t n = s->length();
    for (size_t i = 0; prefix[i] != '\0'; i++) {
        if (i >= n || s->at(i) != prefix[i]) { return 0; }
    }
    return 1;
}
```

- [ ] **Step 5: Verify it passes** — `make test-str_util`
Expected: PASS — prints `test_str_util OK`.

- [ ] **Step 6: Commit**
```bash
git add include/str_util.hbs src/str_util.cbs tests/test_str_util.cbs
git commit -m "feat: str_util string helpers (append, to_cbuf, eq, starts_with)"
```

---

## Task 2: mime — extension → Content-Type (TDD)

**Files:** Create `include/mime.hbs`, `src/mime.cbs`, `tests/test_mime.cbs`

- [ ] **Step 1: Write the failing test** `tests/test_mime.cbs`

```c
#include <stdio.h>
#include <string.h>
#include "../src/mime.cbs"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

int main(void) {
    _Unsafe {
        CHECK(strcmp(mime_for_path("a/index.html"), "text/html") == 0, "html");
        CHECK(strcmp(mime_for_path("style.css"), "text/css") == 0, "css");
        CHECK(strcmp(mime_for_path("app.js"), "application/javascript") == 0, "js");
        CHECK(strcmp(mime_for_path("pic.png"), "image/png") == 0, "png");
        CHECK(strcmp(mime_for_path("noext"), "application/octet-stream") == 0, "default");
        CHECK(strcmp(mime_for_path("weird.xyz"), "application/octet-stream") == 0, "unknown");
    }
    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_mime OK\n");}
    return 0;
}
```

- [ ] **Step 2: Verify it fails** — `make test-mime` → FAIL (undefined `mime_for_path`).

- [ ] **Step 3: Write `include/mime.hbs`**

```c
#ifndef CC_MIME_HBS
#define CC_MIME_HBS
// Returns a static Content-Type string for the file extension in `path`.
const char* mime_for_path(const char* _Nonnull path);
#endif
```

- [ ] **Step 4: Write `src/mime.cbs`**

```c
#include "../include/mime.hbs"
#include <string.h>

// Find the last '.' in path; compare suffix case-sensitively (lowercase assets).
const char* mime_for_path(const char* _Nonnull path) {
    _Unsafe {
        const char* dot = strrchr(path, '.');
        if (dot == NULL) { return "application/octet-stream"; }
        const char* ext = dot + 1;
        if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0) { return "text/html"; }
        if (strcmp(ext, "css") == 0)  { return "text/css"; }
        if (strcmp(ext, "js") == 0)   { return "application/javascript"; }
        if (strcmp(ext, "json") == 0) { return "application/json"; }
        if (strcmp(ext, "png") == 0)  { return "image/png"; }
        if (strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0) { return "image/jpeg"; }
        if (strcmp(ext, "gif") == 0)  { return "image/gif"; }
        if (strcmp(ext, "svg") == 0)  { return "image/svg+xml"; }
        if (strcmp(ext, "txt") == 0)  { return "text/plain"; }
        return "application/octet-stream";
    }
}
```

Note: this function is plain C (not `_Safe`) because it does raw pointer/`strcmp` work; that's fine — it's a leaf utility. Keep its body in `_Unsafe`.

- [ ] **Step 5: Verify it passes** — `make test-mime` → PASS (`test_mime OK`).

- [ ] **Step 6: Commit**
```bash
git add include/mime.hbs src/mime.cbs tests/test_mime.cbs
git commit -m "feat: mime type lookup by file extension"
```

---

## Task 3: http_request — request parser (TDD)

**Files:** Create `include/http_request.hbs`, `src/http_request.cbs`, `tests/test_http_request.cbs`

Parse from a NUL-terminated buffer (the server NUL-terminates after `recv`). Extract method, path, version from the request line; collect headers as `Vec<Header>`. Body = bytes after the blank line (stored but not required for GET).

- [ ] **Step 1: Write the failing test** `tests/test_http_request.cbs`

```c
#include <stdio.h>
#include "../src/str_util.cbs"
#include "../src/http_request.cbs"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

int main(void) {
    const char* raw =
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "User-Agent: test\r\n"
        "\r\n";
    Request req = Request::parse(raw);
    CHECK(str_eq_cstr(&_Const req.method, "GET"), "method");
    CHECK(str_eq_cstr(&_Const req.path, "/index.html"), "path");
    CHECK(str_eq_cstr(&_Const req.version, "HTTP/1.1"), "version");
    CHECK(req.ok, "ok flag");

    const String* _Borrow host = req.get_header("Host");
    CHECK(host != nullptr && str_eq_cstr(host, "localhost"), "host header");
    CHECK(req.get_header("Nope") == nullptr, "missing header");

    Request bad = Request::parse("garbage-no-crlf");
    CHECK(!bad.ok, "malformed not ok");

    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_http_request OK\n");}
    return 0;
}
```

- [ ] **Step 2: Verify it fails** — `make test-http_request` → FAIL.

- [ ] **Step 3: Write `include/http_request.hbs`**

```c
#ifndef CC_HTTP_REQUEST_HBS
#define CC_HTTP_REQUEST_HBS
#include "string.hbs"
#include "vec.hbs"

_Owned struct Header { _Public: String name; String value; };

_Owned struct Request {
_Public:
    String method;
    String path;
    String version;
    Vec<Header> headers;
    String body;
    _Bool ok;
};

// Parse a NUL-terminated raw request. On malformed input, ok = false.
_Safe Request Request::parse(const char* _Nonnull raw);
// Case-sensitive header lookup; nullptr if absent. Borrow tied to `this`.
_Safe const String* _Borrow _Nullable Request::get_header(const Request* _Borrow this, const char* _Nonnull name);
#endif
```

- [ ] **Step 4: Write `src/http_request.cbs`**

```c
#include "../include/http_request.hbs"
#include "../include/str_util.hbs"

// Read chars [*i ..] until any of \r \n ' ' (depending on stop set); append to out.
// Helper internal to this file.
_Safe Request Request::parse(const char* _Nonnull raw) {
    Request r = {
        .method = String::new(), .path = String::new(), .version = String::new(),
        .headers = Vec<Header>::new(), .body = String::new(), .ok = 0
    };
    size_t i = 0;
    // method: up to space
    while (raw[i] != '\0' && raw[i] != ' ' && raw[i] != '\r' && raw[i] != '\n') { r.method.push(raw[i]); i++; }
    if (raw[i] != ' ') { return r; }       // malformed
    i++;
    // path: up to space
    while (raw[i] != '\0' && raw[i] != ' ' && raw[i] != '\r' && raw[i] != '\n') { r.path.push(raw[i]); i++; }
    if (raw[i] != ' ') { return r; }
    i++;
    // version: up to CR/LF
    while (raw[i] != '\0' && raw[i] != '\r' && raw[i] != '\n') { r.version.push(raw[i]); i++; }
    if (raw[i] != '\r' && raw[i] != '\n') { return r; }
    // skip CRLF
    if (raw[i] == '\r') { i++; } if (raw[i] == '\n') { i++; }

    // headers until blank line
    while (raw[i] != '\0' && raw[i] != '\r' && raw[i] != '\n') {
        Header h = { .name = String::new(), .value = String::new() };
        while (raw[i] != '\0' && raw[i] != ':' && raw[i] != '\r' && raw[i] != '\n') { h.name.push(raw[i]); i++; }
        if (raw[i] == ':') { i++; }
        while (raw[i] == ' ') { i++; }                 // skip OWS
        while (raw[i] != '\0' && raw[i] != '\r' && raw[i] != '\n') { h.value.push(raw[i]); i++; }
        if (raw[i] == '\r') { i++; } if (raw[i] == '\n') { i++; }
        r.headers.push(h);
    }
    // skip blank line
    if (raw[i] == '\r') { i++; } if (raw[i] == '\n') { i++; }
    // body (remainder)
    while (raw[i] != '\0') { r.body.push(raw[i]); i++; }

    r.ok = (r.method.length() > 0 && r.path.length() > 0);
    return r;
}

_Safe const String* _Borrow _Nullable Request::get_header(const Request* _Borrow this, const char* _Nonnull name) {
    size_t n = this->headers.length();
    for (size_t k = 0; k < n; k++) {
        const Header* _Borrow h = this->headers.get(k);
        if (str_eq_cstr(&_Const h->name, name)) { return &_Const h->value; }
    }
    return nullptr;
}
```

Note for executor: if the aggregate `Request r = { ... }` initializer is rejected by the
checker for an `_Owned struct`, construct field-by-field instead (`Request r; r.method = String::new(); ...`) and add `__attribute__((ensure_init))` patterns as the `bsc-initialization` skill directs. Fix to compile; behavior identical.

- [ ] **Step 5: Verify it passes** — `make test-http_request` → PASS (`test_http_request OK`).

- [ ] **Step 6: Commit**
```bash
git add include/http_request.hbs src/http_request.cbs tests/test_http_request.cbs
git commit -m "feat: HTTP request parser (request line + headers + body)"
```

---

## Task 4: http_response — response builder (TDD)

**Files:** Create `include/http_response.hbs`, `src/http_response.cbs`, `tests/test_http_response.cbs`

`Response` holds status, body (`String`), and Content-Type. `to_string()` serializes:
status line + `Content-Type` + `Content-Length` + `Connection: close` + CRLF + body.

- [ ] **Step 1: Write the failing test** `tests/test_http_response.cbs`

```c
#include <stdio.h>
#include "../src/str_util.cbs"
#include "../src/http_response.cbs"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

static _Bool contains(const String* _Borrow s, const char* _Nonnull needle) {
    // naive substring via cbuf
    char buf[4096]; str_to_cbuf(s, buf, 4096);
    _Unsafe { return strstr(buf, needle) != NULL; }
}

int main(void) {
    Response ok = Response::ok_text("hello", "text/plain");
    String wire = ok.to_string();
    CHECK(contains(&_Const wire, "HTTP/1.1 200 OK\r\n"), "status line");
    CHECK(contains(&_Const wire, "Content-Type: text/plain\r\n"), "ctype");
    CHECK(contains(&_Const wire, "Content-Length: 5\r\n"), "clen");
    CHECK(contains(&_Const wire, "\r\n\r\nhello"), "body sep");

    Response nf = Response::not_found();
    String w2 = nf.to_string();
    CHECK(contains(&_Const w2, "HTTP/1.1 404 Not Found\r\n"), "404 line");

    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_http_response OK\n");}
    return 0;
}
```
(Add `#include <string.h>` at top for `strstr`.)

- [ ] **Step 2: Verify it fails** — `make test-http_response` → FAIL.

- [ ] **Step 3: Write `include/http_response.hbs`**

```c
#ifndef CC_HTTP_RESPONSE_HBS
#define CC_HTTP_RESPONSE_HBS
#include "string.hbs"

_Owned struct Response {
_Public:
    int status;
    String status_text;
    String content_type;
    String body;
};

_Safe Response Response::make(int status, const char* _Nonnull status_text);
_Safe Response Response::ok_text(const char* _Nonnull body, const char* _Nonnull content_type);
_Safe Response Response::ok_body(String body, const char* _Nonnull content_type); // consumes body
_Safe Response Response::not_found(void);
_Safe Response Response::bad_request(void);
_Safe Response Response::server_error(void);
// Serialize full wire bytes (status line + headers + CRLF + body).
_Safe String Response::to_string(const Response* _Borrow this);
#endif
```

- [ ] **Step 4: Write `src/http_response.cbs`**

```c
#include "../include/http_response.hbs"
#include "../include/str_util.hbs"

_Safe Response Response::make(int status, const char* _Nonnull status_text) {
    Response r;
    r.status = status;
    r.status_text = String::new(); str_append_cstr(&_Mut r.status_text, status_text);
    r.content_type = String::new();
    r.body = String::new();
    return r;
}

_Safe Response Response::ok_body(String body, const char* _Nonnull content_type) {
    Response r = Response::make(200, "OK");
    str_append_cstr(&_Mut r.content_type, content_type);
    r.body = body;     // move
    return r;
}

_Safe Response Response::ok_text(const char* _Nonnull body, const char* _Nonnull content_type) {
    String b = String::new(); str_append_cstr(&_Mut b, body);
    return Response::ok_body(b, content_type);
}

_Safe Response Response::not_found(void) {
    Response r = Response::make(404, "Not Found");
    str_append_cstr(&_Mut r.content_type, "text/html");
    str_append_cstr(&_Mut r.body, "<h1>404 Not Found</h1>");
    return r;
}
_Safe Response Response::bad_request(void) {
    Response r = Response::make(400, "Bad Request");
    str_append_cstr(&_Mut r.content_type, "text/html");
    str_append_cstr(&_Mut r.body, "<h1>400 Bad Request</h1>");
    return r;
}
_Safe Response Response::server_error(void) {
    Response r = Response::make(500, "Internal Server Error");
    str_append_cstr(&_Mut r.content_type, "text/html");
    str_append_cstr(&_Mut r.body, "<h1>500 Internal Server Error</h1>");
    return r;
}

// append a base-10 size_t to a String (no sprintf in safe zone)
_Safe void append_size(String* _Borrow s, size_t n) {
    if (n == 0) { s->push('0'); return; }
    char tmp[24]; size_t k = 0;
    while (n > 0) { tmp[k] = (char)('0' + (n % 10)); n /= 10; k++; }
    while (k > 0) { k--; s->push(tmp[k]); }
}

_Safe String Response::to_string(const Response* _Borrow this) {
    String out = String::new();
    str_append_cstr(&_Mut out, "HTTP/1.1 ");
    append_size(&_Mut out, (size_t)this->status);
    out.push(' ');
    str_append(&_Mut out, &_Const this->status_text);
    str_append_cstr(&_Mut out, "\r\nContent-Type: ");
    str_append(&_Mut out, &_Const this->content_type);
    str_append_cstr(&_Mut out, "\r\nContent-Length: ");
    append_size(&_Mut out, this->body.length());
    str_append_cstr(&_Mut out, "\r\nConnection: close\r\n\r\n");
    str_append(&_Mut out, &_Const this->body);
    return out;
}
```

- [ ] **Step 5: Verify it passes** — `make test-http_response` → PASS.

- [ ] **Step 6: Commit**
```bash
git add include/http_response.hbs src/http_response.cbs tests/test_http_response.cbs
git commit -m "feat: HTTP response builder + serialization"
```

---

## Task 5: config — config.ini parser (TDD)

**Files:** Create `include/config.hbs`, `src/config.cbs`, `tests/test_config.cbs`

Parse `key = value` lines from a buffer (skip blanks and `#` comments). The file-reading
variant is an `_Unsafe` seam; the **parsing** is `_Safe` and unit-tested on a string.

- [ ] **Step 1: Write the failing test** `tests/test_config.cbs`

```c
#include <stdio.h>
#include "../src/str_util.cbs"
#include "../src/config.cbs"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

int main(void) {
    const char* text = "# comment\nport = 9090\ndocument_root = public\nthreads=8\n";
    Config c = Config::parse(text);
    CHECK(c.port == 9090, "port");
    CHECK(c.threads == 8, "threads");
    CHECK(str_eq_cstr(&_Const c.document_root, "public"), "doc root");

    Config d = Config::default_config();
    CHECK(d.port == 8080, "default port");
    CHECK(str_eq_cstr(&_Const d.document_root, "www"), "default root");

    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_config OK\n");}
    return 0;
}
```

- [ ] **Step 2: Verify it fails** — `make test-config` → FAIL.

- [ ] **Step 3: Write `include/config.hbs`**

```c
#ifndef CC_CONFIG_HBS
#define CC_CONFIG_HBS
#include "string.hbs"

_Owned struct Config {
_Public:
    int port;
    int threads;
    String document_root;
};

_Safe Config Config::default_config(void);
_Safe Config Config::parse(const char* _Nonnull text);   // parse from buffer
        Config Config::from_file(const char* _Nonnull path); // _Unsafe file-read seam
#endif
```

- [ ] **Step 4: Write `src/config.cbs`**

```c
#include "../include/config.hbs"
#include "../include/str_util.hbs"
#include <stdio.h>
#include <stdlib.h>

_Safe Config Config::default_config(void) {
    Config c; c.port = 8080; c.threads = 4;
    c.document_root = String::new(); str_append_cstr(&_Mut c.document_root, "www");
    return c;
}

// parse decimal int from a String (digits only)
_Safe int parse_int(const String* _Borrow s) {
    int v = 0; size_t n = s->length();
    for (size_t i = 0; i < n; i++) { char ch = s->at(i); if (ch >= '0' && ch <= '9') { v = v*10 + (ch - '0'); } }
    return v;
}

_Safe Config Config::parse(const char* _Nonnull text) {
    Config c = Config::default_config();
    size_t i = 0;
    while (text[i] != '\0') {
        // skip leading spaces
        while (text[i] == ' ' || text[i] == '\t') { i++; }
        if (text[i] == '#' || text[i] == '\n' || text[i] == '\r') {
            while (text[i] != '\0' && text[i] != '\n') { i++; }
            if (text[i] == '\n') { i++; }
            continue;
        }
        if (text[i] == '\0') { break; }
        String key = String::new(); String val = String::new();
        while (text[i] != '\0' && text[i] != '=' && text[i] != '\n' && text[i] != ' ' && text[i] != '\t') { key.push(text[i]); i++; }
        while (text[i] == ' ' || text[i] == '\t') { i++; }
        if (text[i] == '=') { i++; }
        while (text[i] == ' ' || text[i] == '\t') { i++; }
        while (text[i] != '\0' && text[i] != '\n' && text[i] != '\r' && text[i] != ' ') { val.push(text[i]); i++; }
        while (text[i] != '\0' && text[i] != '\n') { i++; }
        if (text[i] == '\n') { i++; }

        if (str_eq_cstr(&_Const key, "port")) { c.port = parse_int(&_Const val); }
        else if (str_eq_cstr(&_Const key, "threads")) { c.threads = parse_int(&_Const val); }
        else if (str_eq_cstr(&_Const key, "document_root")) {
            c.document_root = String::new(); str_append(&_Mut c.document_root, &_Const val);
        }
    }
    return c;
}

Config Config::from_file(const char* _Nonnull path) {
    _Unsafe {
        FILE* f = fopen(path, "rb");
        if (f == NULL) { return Config::default_config(); }
        char buf[4096]; size_t n = fread(buf, 1, sizeof(buf) - 1, f); buf[n] = '\0';
        fclose(f);
        return Config::parse(buf);
    }
}
```

- [ ] **Step 5: Verify it passes** — `make test-config` → PASS.

- [ ] **Step 6: Commit**
```bash
git add include/config.hbs src/config.cbs tests/test_config.cbs
git commit -m "feat: config.ini parser with defaults and file loading"
```

---

## Task 6: router + path guard (TDD)

**Files:** Create `include/router.hbs`, `src/router.cbs`, `tests/test_router.cbs`, `tests/test_path_guard.cbs`

Router stores routes `(method, path, Handler)`; `match` returns the handler for an exact
path+method, else `nullptr`. Also provide `path_is_safe` (rejects `..` traversal) here —
it's pure logic and unit-testable.

- [ ] **Step 1: Write failing tests** `tests/test_router.cbs`

```c
#include <stdio.h>
#include "../src/str_util.cbs"
#include "../src/http_request.cbs"
#include "../src/http_response.cbs"
#include "../src/router.cbs"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

Response hello(const Request* _Borrow req) { return Response::ok_text("hi", "text/plain"); }

int main(void) {
    Router rt = Router::new();
    rt.add("GET", "/hello", hello);

    Request a = Request::parse("GET /hello HTTP/1.1\r\n\r\n");
    Handler h = rt.match(&_Const a);
    CHECK(h != nullptr, "match found");

    Request b = Request::parse("GET /nope HTTP/1.1\r\n\r\n");
    CHECK(rt.match(&_Const b) == nullptr, "no match");

    Request c = Request::parse("POST /hello HTTP/1.1\r\n\r\n");
    CHECK(rt.match(&_Const c) == nullptr, "method mismatch");

    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_router OK\n");}
    return 0;
}
```

`tests/test_path_guard.cbs`

```c
#include <stdio.h>
#include "../src/str_util.cbs"
#include "../src/http_request.cbs"
#include "../src/http_response.cbs"
#include "../src/router.cbs"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

int main(void) {
    CHECK(path_is_safe("/index.html"), "normal ok");
    CHECK(path_is_safe("/css/style.css"), "subdir ok");
    CHECK(!path_is_safe("/../etc/passwd"), "dotdot rejected");
    CHECK(!path_is_safe("/a/../../b"), "embedded dotdot rejected");
    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_path_guard OK\n");}
    return 0;
}
```

- [ ] **Step 2: Verify both fail** — `make test-router` and `make test-path_guard` → FAIL.

- [ ] **Step 3: Write `include/router.hbs`**

```c
#ifndef CC_ROUTER_HBS
#define CC_ROUTER_HBS
#include "string.hbs"
#include "vec.hbs"
#include "../include/http_request.hbs"
#include "../include/http_response.hbs"

typedef Response (*Handler)(const Request* _Borrow req);

_Owned struct Route { _Public: String method; String path; Handler handler; };

_Owned struct Router { _Public: Vec<Route> routes; };

_Safe Router Router::new(void);
_Safe void Router::add(Router* _Borrow this, const char* _Nonnull method, const char* _Nonnull path, Handler h);
_Safe Handler _Nullable Router::match(const Router* _Borrow this, const Request* _Borrow req);

// Reject paths containing a ".." segment (traversal). True = safe.
_Safe _Bool path_is_safe(const char* _Nonnull path);
#endif
```

- [ ] **Step 4: Write `src/router.cbs`**

```c
#include "../include/router.hbs"
#include "../include/str_util.hbs"

_Safe Router Router::new(void) { Router r; r.routes = Vec<Route>::new(); return r; }

_Safe void Router::add(Router* _Borrow this, const char* _Nonnull method, const char* _Nonnull path, Handler h) {
    Route rt; rt.method = String::new(); rt.path = String::new(); rt.handler = h;
    str_append_cstr(&_Mut rt.method, method);
    str_append_cstr(&_Mut rt.path, path);
    this->routes.push(rt);
}

_Safe Handler _Nullable Router::match(const Router* _Borrow this, const Request* _Borrow req) {
    size_t n = this->routes.length();
    for (size_t k = 0; k < n; k++) {
        const Route* _Borrow rt = this->routes.get(k);
        if (rt->method.equals(&_Const req->method) && rt->path.equals(&_Const req->path)) {
            return rt->handler;
        }
    }
    return nullptr;
}

_Safe _Bool path_is_safe(const char* _Nonnull path) {
    // reject any ".." occurrence
    for (size_t i = 0; path[i] != '\0'; i++) {
        if (path[i] == '.' && path[i+1] == '.') { return 0; }
    }
    return 1;
}
```

- [ ] **Step 5: Verify both pass** — `make test-router`, `make test-path_guard` → PASS.

- [ ] **Step 6: Commit**
```bash
git add include/router.hbs src/router.cbs tests/test_router.cbs tests/test_path_guard.cbs
git commit -m "feat: router (exact match) + path traversal guard"
```

---

## Task 7: socket — POSIX wrappers (`_Unsafe` FFI seam)

**Files:** Create `include/socket.hbs`, `src/socket.cbs`

No unit test (pure syscalls); verified indirectly by Task 10 integration tests. Keep each
function tiny so the seam is auditable.

- [ ] **Step 1: Write `include/socket.hbs`**

```c
#ifndef CC_SOCKET_HBS
#define CC_SOCKET_HBS
#include <stddef.h>
#include <sys/types.h>
// Create+bind+listen on port; returns listen fd or -1.
int socket_listen(int port, int backlog);
// Accept one connection; returns client fd or -1.
int socket_accept(int listen_fd);
// recv up to cap-1 bytes into buf, NUL-terminate; returns bytes read (>=0) or -1.
ssize_t socket_recv(int fd, char* _Nonnull buf, size_t cap);
// send exactly len bytes (loops on partial); returns 0 on success, -1 on error.
int socket_send_all(int fd, const char* _Nonnull buf, size_t len);
void socket_close(int fd);
#endif
```

- [ ] **Step 2: Write `src/socket.cbs`**

```c
#include "../include/socket.hbs"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int socket_listen(int port, int backlog) {
    _Unsafe {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { return -1; }
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons((unsigned short)port);
        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
        if (listen(fd, backlog) < 0) { close(fd); return -1; }
        return fd;
    }
}

int socket_accept(int listen_fd) {
    _Unsafe { return accept(listen_fd, NULL, NULL); }
}

ssize_t socket_recv(int fd, char* _Nonnull buf, size_t cap) {
    _Unsafe {
        if (cap == 0) { return -1; }
        ssize_t n = recv(fd, buf, cap - 1, 0);
        if (n < 0) { return -1; }
        buf[n] = '\0';
        return n;
    }
}

int socket_send_all(int fd, const char* _Nonnull buf, size_t len) {
    _Unsafe {
        size_t sent = 0;
        while (sent < len) {
            ssize_t n = send(fd, buf + sent, len - sent, 0);
            if (n <= 0) { return -1; }
            sent += (size_t)n;
        }
        return 0;
    }
}

void socket_close(int fd) { _Unsafe { close(fd); } }
```

- [ ] **Step 3: Syntax check** — `make -n` then build the full project later; for now:
`/home/zly/bsc/llvm-project/build/bin/clang -Wall -Wextra -Wno-nullability-completeness -I/home/zly/bsc/llvm-project/install/include/libcbs -fsyntax-only src/socket.cbs`
Expected: no errors.

- [ ] **Step 4: Commit**
```bash
git add include/socket.hbs src/socket.cbs
git commit -m "feat: POSIX socket wrappers (listen/accept/recv/send_all/close)"
```

---

## Task 8: file_server — static file serving

**Files:** Create `include/file_server.hbs`, `src/file_server.cbs`

Build the on-disk path = `document_root` + request path (default `/` → `/index.html`),
reject unsafe paths (→ 404), read the file (→ 200 with MIME), missing → 404.

- [ ] **Step 1: Write `include/file_server.hbs`**

```c
#ifndef CC_FILE_SERVER_HBS
#define CC_FILE_SERVER_HBS
#include "../include/http_request.hbs"
#include "../include/http_response.hbs"
#include "../include/config.hbs"

// Serve a static file for `req` under config->document_root. Always returns a Response.
_Safe Response serve_static(const Config* _Borrow config, const Request* _Borrow req);
#endif
```

- [ ] **Step 2: Write `src/file_server.cbs`**

```c
#include "../include/file_server.hbs"
#include "../include/str_util.hbs"
#include "../include/router.hbs"   // path_is_safe
#include "../include/mime.hbs"
#include <stdio.h>

// Read entire file into an owned String; returns ok flag via out-param style.
// Internal _Unsafe seam.
_Safe Response serve_static(const Config* _Borrow config, const Request* _Borrow req) {
    // Build path string: document_root + path (map "/" -> "/index.html")
    String rel = String::new();
    str_append(&_Mut rel, &_Const req->path);
    if (rel.equals(&_Const ({ String s = String::new(); str_append_cstr(&_Mut s, "/"); s; }))) {
        // "/" -> "/index.html"
        rel = String::new(); str_append_cstr(&_Mut rel, "/index.html");
    }

    char relbuf[1024]; str_to_cbuf(&_Const rel, relbuf, 1024);
    if (!path_is_safe(relbuf)) { return Response::not_found(); }

    String full = String::new();
    str_append(&_Mut full, &_Const config->document_root);
    str_append(&_Mut full, &_Const rel);
    char pathbuf[2048]; str_to_cbuf(&_Const full, pathbuf, 2048);

    String body = String::new();
    _Bool ok = 0;
    _Unsafe {
        FILE* f = fopen(pathbuf, "rb");
        if (f != NULL) {
            char chunk[4096]; size_t n;
            while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
                for (size_t i = 0; i < n; i++) { body.push(chunk[i]); }
            }
            fclose(f);
            ok = 1;
        }
    }
    if (!ok) { return Response::not_found(); }
    return Response::ok_body(body, mime_for_path(pathbuf));
}
```

Note for executor: the inline `({ ... })` statement-expression to compare against `"/"` may
not be supported — instead implement a small `str_eq_cstr(&_Const req->path, "/")` check
(str_eq_cstr already exists from Task 1). Replace the mapping block with:
```c
String rel = String::new();
if (str_eq_cstr(&_Const req->path, "/")) { str_append_cstr(&_Mut rel, "/index.html"); }
else { str_append(&_Mut rel, &_Const req->path); }
```
Prefer this clean form; the statement-expression above was illustrative.

- [ ] **Step 3: Syntax check** — `-fsyntax-only src/file_server.cbs` → no errors. Fix per `bsc-borrowing`/`bsc-ownership` skills if the move of `body` into `ok_body` is flagged (it's a deliberate move; ensure `body` isn't used after).

- [ ] **Step 4: Commit**
```bash
git add include/file_server.hbs src/file_server.cbs
git commit -m "feat: static file serving with traversal guard + MIME"
```

---

## Task 9: thread_pool — pthread worker pool (`_Unsafe` threading seam)

**Files:** Create `include/thread_pool.hbs`, `src/thread_pool.cbs`

Fixed N workers pull client fds (plain `int`) from a mutex+condvar queue. Only `int`s
cross the thread boundary — no owned heap data is shared. Each worker calls a single
connection-handler callback `void handle_conn(int fd, void* ctx)`.

- [ ] **Step 1: Write `include/thread_pool.hbs`**

```c
#ifndef CC_THREAD_POOL_HBS
#define CC_THREAD_POOL_HBS
#include <stddef.h>
#include <pthread.h>

typedef void (*ConnHandler)(int client_fd, void* _Nonnull ctx);

#define TP_QUEUE_CAP 256
#define TP_MAX_WORKERS 64

struct ThreadPool {
    pthread_t workers[TP_MAX_WORKERS];
    int n_workers;
    int queue[TP_QUEUE_CAP];
    int head, tail, count;
    int stop;
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    ConnHandler handler;
    void* ctx;
};

// Initialize and spawn workers. Returns 0 on success.
int  thread_pool_start(struct ThreadPool* _Nonnull tp, int n_workers, ConnHandler handler, void* _Nonnull ctx);
// Enqueue a client fd (blocks if full). 
void thread_pool_submit(struct ThreadPool* _Nonnull tp, int client_fd);
// Signal stop and join all workers.
void thread_pool_shutdown(struct ThreadPool* _Nonnull tp);
#endif
```

Design note: `ThreadPool` is a **plain `struct`** (not `_Owned`) holding only POD + pthread
primitives, used entirely behind the `_Unsafe` threading seam. This is the deliberate
seam from the spec — BSC's borrow checker can't model threads, so the pool is C-style and
audited as one unit.

- [ ] **Step 2: Write `src/thread_pool.cbs`**

```c
#include "../include/thread_pool.hbs"
#include <stdio.h>

static void* tp_worker(void* arg) {
    _Unsafe {
        struct ThreadPool* tp = (struct ThreadPool*)arg;
        for (;;) {
            pthread_mutex_lock(&tp->mtx);
            while (tp->count == 0 && !tp->stop) {
                pthread_cond_wait(&tp->not_empty, &tp->mtx);
            }
            if (tp->stop && tp->count == 0) { pthread_mutex_unlock(&tp->mtx); break; }
            int fd = tp->queue[tp->head];
            tp->head = (tp->head + 1) % TP_QUEUE_CAP;
            tp->count--;
            pthread_mutex_unlock(&tp->mtx);
            tp->handler(fd, tp->ctx);
        }
        return NULL;
    }
}

int thread_pool_start(struct ThreadPool* _Nonnull tp, int n_workers, ConnHandler handler, void* _Nonnull ctx) {
    _Unsafe {
        if (n_workers > TP_MAX_WORKERS) { n_workers = TP_MAX_WORKERS; }
        tp->n_workers = n_workers;
        tp->head = 0; tp->tail = 0; tp->count = 0; tp->stop = 0;
        tp->handler = handler; tp->ctx = ctx;
        pthread_mutex_init(&tp->mtx, NULL);
        pthread_cond_init(&tp->not_empty, NULL);
        for (int i = 0; i < n_workers; i++) {
            if (pthread_create(&tp->workers[i], NULL, tp_worker, tp) != 0) { return -1; }
        }
        return 0;
    }
}

void thread_pool_submit(struct ThreadPool* _Nonnull tp, int client_fd) {
    _Unsafe {
        pthread_mutex_lock(&tp->mtx);
        // simple bounded queue: drop if full (close fd) to avoid blocking accept loop
        if (tp->count == TP_QUEUE_CAP) { pthread_mutex_unlock(&tp->mtx); return; }
        tp->queue[tp->tail] = client_fd;
        tp->tail = (tp->tail + 1) % TP_QUEUE_CAP;
        tp->count++;
        pthread_cond_signal(&tp->not_empty);
        pthread_mutex_unlock(&tp->mtx);
    }
}

void thread_pool_shutdown(struct ThreadPool* _Nonnull tp) {
    _Unsafe {
        pthread_mutex_lock(&tp->mtx);
        tp->stop = 1;
        pthread_cond_broadcast(&tp->not_empty);
        pthread_mutex_unlock(&tp->mtx);
        for (int i = 0; i < tp->n_workers; i++) { pthread_join(tp->workers[i], NULL); }
        pthread_mutex_destroy(&tp->mtx);
        pthread_cond_destroy(&tp->not_empty);
    }
}
```

Note: if `thread_pool_submit` drops a full-queue fd, the connection handler in Task 10
must close fds it receives; for the dropped case, close in submit before returning.
Refine: in the full-queue branch, `socket_close(client_fd)` — but to avoid a dependency
cycle, instead `_Unsafe { close(client_fd); }` directly (add `#include <unistd.h>`).

- [ ] **Step 3: Syntax check** — `-fsyntax-only src/thread_pool.cbs` → no errors.

- [ ] **Step 4: Commit**
```bash
git add include/thread_pool.hbs src/thread_pool.cbs
git commit -m "feat: pthread thread pool (mutex+condvar fd queue)"
```

---

## Task 10: server + main — wire everything together

**Files:** Create `include/server.hbs`, `src/server.cbs`, `src/main.cbs`

`server.cbs` holds the `ServerCtx` (config + router, read-only after startup), the
connection handler (recv → parse → route/static → send → close), and the accept loop.
`main.cbs` is the aggregator: `#include`s all `.cbs` files, loads config, registers demo
routes, runs the server.

- [ ] **Step 1: Write `include/server.hbs`**

```c
#ifndef CC_SERVER_HBS
#define CC_SERVER_HBS
#include "../include/config.hbs"
#include "../include/router.hbs"

struct ServerCtx { const Config* config; const Router* router; };

// Run the accept loop: bind port, spawn pool, accept→submit forever. Returns -1 on bind error.
int server_run(const Config* _Borrow config, const Router* _Borrow router);
#endif
```

- [ ] **Step 2: Write `src/server.cbs`**

```c
#include "../include/server.hbs"
#include "../include/socket.hbs"
#include "../include/thread_pool.hbs"
#include "../include/file_server.hbs"
#include "../include/http_request.hbs"
#include "../include/http_response.hbs"
#include "../include/str_util.hbs"
#include <stdio.h>

#define REQ_BUF 8192

// Handle one connection: read request, route, respond, close. Runs on a worker thread.
static void handle_conn(int fd, void* _Nonnull ctx_raw) {
    _Unsafe {
        struct ServerCtx* ctx = (struct ServerCtx*)ctx_raw;
        char buf[REQ_BUF];
        ssize_t n = socket_recv(fd, buf, REQ_BUF);
        if (n <= 0) { socket_close(fd); return; }

        Request req = Request::parse(buf);
        Response resp = req.ok
            ? ({ Handler h = ctx->router->match(&_Const req);
                 h != nullptr ? h(&_Const req) : serve_static(ctx->config, &_Const req); })
            : Response::bad_request();

        String wire = resp.to_string();
        char* _Nonnull data;  // send via length; build a heap/stack copy
        // Send directly from String buffer using length (not NUL-terminated):
        size_t len = wire.length();
        // Copy into a stack buffer if it fits, else send in chunks from String.
        for (size_t off = 0; off < len; ) {
            char out[4096]; size_t m = 0;
            while (off < len && m < sizeof(out)) { out[m] = wire.at(off); m++; off++; }
            if (socket_send_all(fd, out, m) != 0) { break; }
        }
        // request log
        char mbuf[16], pbuf[256];
        str_to_cbuf(&_Const req.method, mbuf, 16);
        str_to_cbuf(&_Const req.path, pbuf, 256);
        fprintf(stderr, "%s %s -> %d\n", mbuf, pbuf, resp.status);
        socket_close(fd);
    }
}

int server_run(const Config* _Borrow config, const Router* _Borrow router) {
    int listen_fd = socket_listen(config->port, 128);
    if (listen_fd < 0) { return -1; }
    _Unsafe {
        struct ServerCtx ctx;
        ctx.config = config; ctx.router = router;
        struct ThreadPool pool;
        if (thread_pool_start(&pool, config->threads, handle_conn, &ctx) != 0) { return -1; }
        fprintf(stderr, "cc_httpd listening on port %d (threads=%d)\n", config->port, config->threads);
        for (;;) {
            int client = socket_accept(listen_fd);
            if (client < 0) { continue; }
            thread_pool_submit(&pool, client);
        }
    }
    return 0;
}
```

Note for executor: the `?:` with statement-expression for routing may not compile. Use a
plain branch instead:
```c
Response resp;
if (!req.ok) { resp = Response::bad_request(); }
else {
    Handler h = ctx->router->match(&_Const req);
    if (h != nullptr) { resp = h(&_Const req); }
    else { resp = serve_static(ctx->config, &_Const req); }
}
```
Also remove the unused `char* data;` line. Keep the chunked send loop.

- [ ] **Step 3: Write `src/main.cbs`** (the single compiled TU — includes all `.cbs`)

```c
// cc_httpd entry point — single translation unit aggregator.
#include "str_util.cbs"
#include "mime.cbs"
#include "http_request.cbs"
#include "http_response.cbs"
#include "config.cbs"
#include "router.cbs"
#include "socket.cbs"
#include "file_server.cbs"
#include "thread_pool.cbs"
#include "server.cbs"

// Demo dynamic route.
Response route_hello(const Request* _Borrow req) {
    return Response::ok_text("<h1>Hello from a cc_httpd route!</h1>", "text/html");
}

int main(void) {
    Config config = Config::from_file("config.ini");
    Router router = Router::new();
    router.add("GET", "/hello", route_hello);
    return server_run(&_Const config, &_Const router);
}
```

Note: includes use paths relative to `src/` (since main.cbs is in `src/`). If the compiler
resolves `#include "str_util.cbs"` from `src/`, this works. If not, use `#include "../src/str_util.cbs"` — but since main.cbs lives in `src/`, the bare names should resolve.

- [ ] **Step 4: Build the full server** — `make`
Expected: `bin/httpd` builds with no errors. Fix any borrow/init/move diagnostics per the
relevant BSC skill (`bsc-ownership`, `bsc-borrowing`, `bsc-initialization`, `bsc-common-mistakes`).
Common fixes: ensure each `_Owned struct` field is initialized before the struct is used;
ensure moved values (`body`, `wire`) aren't used after the move.

- [ ] **Step 5: Smoke-run** — `./bin/httpd &` then `curl -s localhost:8080/` → returns index.html; `kill %1`.

- [ ] **Step 6: Commit**
```bash
git add include/server.hbs src/server.cbs src/main.cbs
git commit -m "feat: server accept loop + connection handler + main entry"
```

---

## Task 11: integration tests (all acceptance criteria)

**Files:** Create `tests/run_integration.sh`

- [ ] **Step 1: Write `tests/run_integration.sh`**

```bash
#!/usr/bin/env bash
set -u
make >/dev/null || { echo "BUILD FAILED"; exit 1; }
./bin/httpd >/tmp/cc_httpd.log 2>&1 &
SRV=$!
sleep 0.5
fail=0
chk() { if [ "$1" != "$2" ]; then echo "FAIL: $3 (got '$1' want '$2')"; fail=1; else echo "ok: $3"; fi; }

# AC-2 static 200
code=$(curl -s -o /dev/null -w '%{http_code}' localhost:8080/)
chk "$code" "200" "AC-2 GET / returns 200"
ctype=$(curl -s -D - -o /dev/null localhost:8080/ | grep -i '^content-type' | tr -d '\r' | awk '{print $2}')
chk "$ctype" "text/html" "AC-2 index Content-Type"

# AC-3 404
code=$(curl -s -o /dev/null -w '%{http_code}' localhost:8080/nope.html)
chk "$code" "404" "AC-3 missing file 404"

# AC-4 dynamic route
body=$(curl -s localhost:8080/hello)
case "$body" in *"cc_httpd route"*) echo "ok: AC-4 /hello body";; *) echo "FAIL: AC-4 /hello body"; fail=1;; esac

# path traversal -> 404
code=$(curl -s -o /dev/null -w '%{http_code}' --path-as-is localhost:8080/../Makefile)
chk "$code" "404" "traversal blocked"

# AC-5 concurrency: 50 parallel requests all 200
ok=0
for i in $(seq 1 50); do
  ( [ "$(curl -s -o /dev/null -w '%{http_code}' localhost:8080/)" = "200" ] && echo y ) &
done > /tmp/cc_conc.out
wait
ok=$(grep -c y /tmp/cc_conc.out 2>/dev/null || echo 0)
chk "$ok" "50" "AC-5 50 concurrent requests"

kill $SRV 2>/dev/null
exit $fail
```

- [ ] **Step 2: Run** — `bash tests/run_integration.sh`
Expected: every line `ok:`; exit 0. Fix server bugs until all pass.

- [ ] **Step 3: Commit**
```bash
git add tests/run_integration.sh
git commit -m "test: integration tests for AC-2..AC-5 + traversal"
```

---

## Task 12: memory safety check + final polish

- [ ] **Step 1: Valgrind a short run** (AC-6)
```bash
./bin/httpd & SRV=$!; sleep 0.3
valgrind --leak-check=full --error-exitcode=99 ./bin/httpd >/tmp/vg.log 2>&1 &
# (or run valgrind as the server and curl a few requests, then SIGTERM)
```
Practical form: start `valgrind ./bin/httpd`, `curl` ~10 mixed requests (/, /hello, /nope), then `kill -INT`; confirm no "definitely lost" from our code. Note: the accept loop is infinite — use a request counter or signal to exit for a clean valgrind run, OR accept that the steady-state per-request path (Request/Response RAII) is what matters; document the result.
If leaks appear in our request path, fix with the `bsc-ownership` skill (likely a missing move or a `String`/`Vec` not dropped).

- [ ] **Step 2: Zero-warning build** — `make clean && make`
Expected: no warnings except the suppressed libcbs nullability ones. Address any from our code.

- [ ] **Step 3: Update CLAUDE.md compile section** — replace the `[FILL IN: ...]` block in
`CLAUDE.md` "BSC Project Compile Command" with the validated toolchain (CC/INC/LIB/FLAGS,
`make`, `make test-X`, `make smoke`).

- [ ] **Step 4: Final commit**
```bash
git add -A
git commit -m "docs: record verified compile command; memory-safety notes"
```

---

## Self-Review Notes (coverage map)

- **FR-1 listen/accept** → Task 7 socket + Task 10 server_run.
- **FR-2 parse request** → Task 3.  **FR-3 GET static/dynamic** → Tasks 8, 6, 10.
- **FR-4 status codes** → Task 4 (200/400/404/500).  **FR-5 MIME** → Task 2.
- **FR-6 route registration** → Task 6.  **NFR-1 concurrency** → Task 9 + AC-5 test.
- **NFR-2 memory safety** → `_Owned`/RAII throughout + Task 12 valgrind.
- **NFR-4 config** → Task 5.
- **AC-1..AC-6** → Task 11 + Task 12.
- **Path traversal** → Task 6 `path_is_safe` + Task 8 + Task 11 test.

## Risk Notes for Executor

- BSC `_Owned struct` aggregate initializers and statement-expressions are the two
  constructs most likely to need rewriting to field-by-field form — the plan flags each.
- Moves of `String`/`Response` into builders (`ok_body`, returning by value) must not be
  followed by use of the moved variable; the checker enforces this.
- When a `.cbs` is `#include`d by both a test and `main.cbs`, that's fine — they are
  separate translation units.
- If a borrow-checker error is unclear, `hover` via LSP (see `bsc-lsp`) or consult
  `bsc-errors` before adding `_Unsafe`. Do not widen the unsafe surface to silence the checker.
