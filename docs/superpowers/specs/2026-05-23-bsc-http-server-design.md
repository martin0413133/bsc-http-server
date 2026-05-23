# cc_httpd — BiSheng C HTTP Server — Design Spec

**Date:** 2026-05-23
**Status:** Approved (architecture); pending implementation plan

## 1. Overview

A lightweight HTTP/1.1 server written in BiSheng C (BSC), serving static files and
simple dynamic routes, using BSC's ownership system for memory safety and a pthread
thread pool for concurrency.

This is the Claude Code implementation (`cc_httpd`) of the same product spec that the
Trae tool implemented as the sibling project `httpx`. It is a **fair tool comparison**:
cc_httpd is implemented **independently from the PRD requirements only** — Claude does
not read httpx's `.cbs`/`.hbs` source. (Shared requirement artifacts — PRD, Makefile
conventions, sample HTML — are fair game; implementation source is not.)

## 2. Goals

- Full HTTP/1.1 request parsing (method, path, version, headers, body).
- Static file serving for GET, with correct MIME types and path-traversal protection.
- Simple route registration with handler callbacks.
- Standard status codes: 200, 400, 404, 500.
- Concurrency via a fixed pthread thread pool.
- Memory safety through BSC ownership/RAII; minimal `_Unsafe` surface.
- Configurable port and document root via `config.ini`.

## 3. Non-Goals

HTTPS/TLS, HTTP/2, async/coroutine IO, keep-alive pipelining beyond basic handling,
CGI/FastCGI, load balancing, directory listing.

## 4. Architecture

### 4.1 Build model

**Single translation unit.** Each concern is a `.hbs` (declarations) + `.cbs`
(definitions) pair. `src/main.cbs` `#include`s the `.cbs` files in dependency order and
is compiled as one unit:

```
/home/zly/bsc/llvm-project/build/bin/clang -Wall -Wextra -g src/main.cbs -o bin/httpd -lpthread
```

Rationale: BSC generic instantiation and `Type::method` definitions are fragile across
translation units; single-TU is the proven, low-risk model. Header include guards
prevent double declaration.

### 4.2 Layering (functional core / imperative shell)

Per project directive: **business code contains zero `_Unsafe`; all `_Unsafe` lives in the
adapter layer, wrapped behind `_Safe` interfaces.** A `_Safe` function with an `_Unsafe { }`
block inside is still `_Safe` and callable from business code (the block is a local escape;
marking the whole function `_Unsafe` would be contagious to every caller).

**Business core (`src/*.cbs`) — 100% `_Safe`, no `_Unsafe`:**

| Module | Responsibility | Key types / ownership |
|---|---|---|
| `str_util` | append/copy/compare on `String` (fills libcbs `String` gap — no native `push_str`) | `_Safe` free functions |
| `mime` | file extension → Content-Type (pure safe char matching, no libc) | `_Safe`, returns `const char*` literal |
| `http_request` | parse method/path/version/headers from raw bytes | `_Owned struct Request`; `Request::parse(const char* _Nonnull raw)` |
| `http_response` | build status/headers/body; `ok`/`not_found`/`bad_request`/`server_error`; serialize | `_Owned struct Response`; `to_string()` → `String` |
| `config` | parse `config.ini` text (`key=value`) → port/threads/doc-root; defaults | `_Owned struct Config`; `Config::parse(text)`, `default_config()` (NO file IO) |
| `router` | register `(method, path) → handler`; exact match; `path_is_safe` traversal guard | `_Owned struct Router { Vec<Route> }` |
| `file_server` | resolve path under doc root, traversal guard, read via `fs` adapter → `Response` | `_Safe serve_static(cfg, req)` |
| `handler` | pure `handle_request(cfg, router, raw_bytes) → Response` — the functional core | `_Safe`, socket-free, unit-testable |

**Adapter layer (`src/platform/*.cbs`) — `_Safe` interface, minimal `_Unsafe` inside:**

| Module | Responsibility | Seam |
|---|---|---|
| `net` | `_Safe` socket listen/accept; recv → `String`; send `const String*` (length-based); close | sockets/syscalls |
| `fs` | `_Safe` read entire file → `String` + ok flag | `fopen`/`fread` |
| `log` | `_Safe` request/diagnostic logging | `fprintf` |
| `thread_pool` | `_Safe` start/submit/shutdown; fixed N workers; mutex+condvar queue of client **fds** | `pthread`; worker is adapter-internal C-ABI |
| `runtime` | imperative shell: build `ServerCtx`, accept loop, recv→`handle_request`→send, spawn pool | raw `ServerCtx*`/`void*` across threads |
| `main` | load config text via `fs`, parse, register demo routes, run `runtime` | entry point; `#include`s all `.cbs` |

Enforcement: `tests/check_no_unsafe.sh` greps the business `src/*.cbs` files and fails the
build if any `_Unsafe` token appears there (the adapter dir `src/platform/` is exempt).

### 4.3 Component interfaces (sketch)

```c
// common types
_Owned struct Header { _Public: String name; String value; };
// ServerCtx: the read-only shared context handed to each worker (raw ptr across the
// threading seam); holds borrows/pointers to the startup-built Config and Router.
struct ServerCtx { const Config* config; const Router* router; };

// config.hbs (business — parse only; file text is loaded by the runtime via the fs adapter)
_Owned struct Config { _Public: int port; int threads; String document_root; };
_Safe Config Config::default_config(void);
_Safe Config Config::parse(const char* _Nonnull text);

// http_request.hbs
_Owned struct Request {
_Public:
    String method;       // "GET"
    String path;         // "/index.html"
    String version;      // "HTTP/1.1"
    Vec<Header> headers; // parsed headers
    String body;
};
_Safe Request Request::parse(const char* _Borrow buf, size_t len);
_Safe const String* _Borrow Request::get_header(const Request* _Borrow this, const char* name);

// http_response.hbs
_Owned struct Response { _Public: int status; String status_text; Vec<Header> headers; String body; };
_Safe Response Response::with_status(int code, const char* text);
_Safe void     Response::set_body(Response* _Borrow this, String body, const char* content_type);
_Safe String   Response::to_string(const Response* _Borrow this);   // full wire bytes
_Safe Response Response::not_found(void);
_Safe Response Response::bad_request(void);
_Safe Response Response::server_error(void);

// router.hbs
typedef Response (*Handler)(const Request* _Borrow req);
_Owned struct Router { _Public: Vec<Route> routes; };
_Safe Router Router::new(void);
_Safe void   Router::add(Router* _Borrow this, const char* method, const char* prefix, Handler h);
_Safe Handler _Nullable Router::match(const Router* _Borrow this, const Request* _Borrow req);

// platform/net.hbs  (adapter: _Safe interface, _Unsafe inside; fds are int)
_Safe int  net_listen(int port, int backlog);          // listen fd or -1
_Safe int  net_accept(int listen_fd);                  // client fd or -1
_Safe _Bool net_recv_string(int fd, String* _Borrow out);  // fill out with request bytes
_Safe _Bool net_send_string(int fd, const String* _Borrow data);  // length-based send
_Safe void net_close(int fd);

// platform/fs.hbs (adapter)
_Safe _Bool fs_read_file(const char* _Nonnull path, String* _Borrow out);

// platform/handler — the pure functional core (business, _Safe, socket-free)
_Safe Response handle_request(const Config* _Borrow cfg, const Router* _Borrow router,
                              const char* _Nonnull raw);

// platform/thread_pool.hbs (adapter)
_Safe int  thread_pool_start(struct ThreadPool* _Nonnull tp, int n, ConnHandler h, void* _Nonnull ctx);
_Safe void thread_pool_submit(struct ThreadPool* _Nonnull tp, int client_fd);
_Safe void thread_pool_shutdown(struct ThreadPool* _Nonnull tp);
```

(Exact signatures finalized during implementation; this is the shape.)

## 5. Ownership Design

- **`Request` / `Response` / `Config` / `Router` are `_Owned struct`s** holding `String`
  and `Vec`. Destructors are empty — field destructors run automatically (RAII). The
  request-handling path performs **no manual frees** and leaks nothing.
- **Across threads only an `int` fd crosses** the job queue. No heap-owned data is shared
  between threads, so there is no cross-thread ownership transfer to reason about. This
  is a deliberate simplification of the hardest BSC/threading interaction.
- **Router and Config are constructed once at startup, then only read** by all worker
  threads → shared as `const T* _Borrow` (many readers, no writer). The worker thread
  receives a raw context pointer (threading seam) and treats Router/Config as read-only.
- **Handler callbacks** take `const Request* _Borrow` and return an `_Owned Response` by
  value (Rule 1: return owned, borrow arguments).

## 6. The `_Unsafe` Boundary

**All `_Unsafe` lives in `src/platform/` (the adapter layer); business `src/*.cbs` has none.**
Each adapter function is declared `_Safe` and wraps the minimum unsafe lines in an
`_Unsafe { }` block, so business callers stay `_Safe`. The seams:

1. **`net`** — `socket`, `bind`, `listen`, `accept`, `recv`, `send`, `close`. recv copies
   bytes into a `String`; send iterates a `const String*` and writes length-based.
2. **`fs`** — `fopen`/`fread`/`fclose`; returns file contents as an owned `String`.
3. **`log`** — `fprintf` to stderr.
4. **`thread_pool` / `runtime`** — `pthread_*`; the C-ABI worker; and the raw
   `ServerCtx*`/`void*` carried across the thread boundary. The business `handle_request`
   it ultimately calls is `_Safe`.

Parsing, routing, response building, MIME lookup, config parsing, file-server logic, and
the `handle_request` core are all `_Safe` with **no `_Unsafe`**.

## 7. Error Handling

- Syscall failure → log to stderr, return `-1` / close the connection; the accept loop
  continues.
- Malformed request → `400 Bad Request`.
- File not found / not under doc root → `404 Not Found`.
- Path-traversal attempt (`..` escaping doc root) → `404` (treated as not found; never
  serve outside root).
- Handler or file-read error → `500 Internal Server Error`.
- No exceptions; C-style status ints and `_Nullable` pointers.

## 8. Testing & Verification

| AC | Test |
|---|---|
| AC-1 server starts | server binds configured port; `curl` connects |
| AC-2 static 200 | `curl -s localhost:8080/` returns `index.html` body, 200, correct Content-Type |
| AC-3 404 | `curl -i localhost:8080/nope` returns `404` |
| AC-4 dynamic route | `curl -s localhost:8080/hello` returns the handler's custom body |
| AC-5 concurrency | parallel `curl` (or `ab -c 10 -n 100`) all succeed |
| AC-6 memory safety | optional `valgrind` clean run over a request batch |

- Per-file syntax check during dev: `clang -fsyntax-only -x bsc <file>` (with proper
  include path).
- `tests/run.sh` orchestrates: build → start server → run curl assertions → stop server.
- Sample assets: `www/index.html`, `config.ini` (`port=8080`, `document_root=www`).

## 9. File Tree (target)

```
cc_httpd/
├── Makefile
├── config.ini
├── include/   (config|http_request|http_response|mime|file_server|router|socket|thread_pool|server|str_util).hbs
├── src/        same basenames .cbs  + main.cbs (aggregator)
├── www/index.html
└── tests/run.sh
```

## 10. Open Questions (resolved)

- POST handling: parse the body generically (store in `Request.body`); routing is
  GET-focused for demo handlers. Sufficient for the spec.
- Logging: simple stderr request logging (method + path + status). In scope, minimal.
- Keep-alive: handle one request per connection (Connection: close); pipelining is a
  non-goal.
