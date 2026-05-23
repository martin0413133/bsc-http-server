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

### 4.2 Module layout

| Module | Responsibility | Key types / ownership |
|---|---|---|
| `str_util` | append `const char*` / `String` into a `String` (fills the libcbs `String` gap — no native `push_str`) | free functions on `String* _Borrow` |
| `config` | parse `config.ini` (`key=value`) → port + document root | `_Owned struct Config { String document_root; int port; }`, RAII |
| `http_request` | parse method/path/version/headers from raw request bytes | `_Owned struct Request`; built by `Request::parse(const char* _Borrow buf, size_t len)` |
| `http_response` | build status line + headers + body; `ok` / `not_found` / `bad_request` / `server_error` helpers; serialize to bytes | `_Owned struct Response`; `to_string()` → `String` |
| `mime` | file extension → Content-Type (static table) | free function returning `const char*` |
| `file_server` | resolve requested path under doc root, **path-traversal guard**, read file → `Response` | returns `_Owned Response` |
| `router` | register `(method, path-prefix) → handler fn ptr`; match (exact before prefix) | `_Owned struct Router { Vec<Route> routes; }`; **read-only after startup** |
| `socket` | POSIX `socket/bind/listen/accept/recv/send/close` wrappers | `_Unsafe` FFI seam; fds are plain `int` |
| `thread_pool` | fixed N workers; mutex + condvar job queue of client **fds** | `_Unsafe` threading seam; queue holds `int` only |
| `server` | accept loop → enqueue fd → worker parses request, routes (static file or handler), sends response | aggregates modules |
| `main` | load config, register demo routes (e.g. `/hello`), start server; `#include`s all `.cbs` | entry point |

### 4.3 Component interfaces (sketch)

```c
// config.hbs
_Owned struct Config { _Public: String document_root; int port; };
_Safe Config Config::default(void);
        Config Config::from_file(const char* path);   // _Unsafe seam for file IO

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

// socket.hbs  (all _Unsafe internally; fds are int)
int  socket_listen(int port);          // returns listen fd or -1
int  socket_accept(int listen_fd);     // returns client fd or -1
ssize_t socket_recv(int fd, char* buf, size_t cap);
ssize_t socket_send(int fd, const char* buf, size_t len);
void socket_close(int fd);

// thread_pool.hbs
_Owned struct ThreadPool { /* workers, mutex, cond, queue of int fds, stop flag */ };
ThreadPool ThreadPool::start(int n_workers, ServerCtx* ctx);  // _Unsafe threading seam
void       ThreadPool::submit(ThreadPool* _Borrow this, int client_fd);
void       ThreadPool::shutdown(ThreadPool* _Borrow this);
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

Exactly three seams; everything else is `_Safe`:

1. **Syscalls / FFI** — socket and file operations (`socket`, `bind`, `listen`,
   `accept`, `recv`, `send`, `close`, `open`/`read`/`fstat`), and `printf`/`fprintf`
   logging.
2. **Threading** — `pthread_create`, `pthread_mutex_*`, `pthread_cond_*`, and the
   raw context pointer passed to the worker function.
3. **Raw string boundary** — `const char* ` ↔ `String` via `String::from` /
   `__take_from_raw` / `__move_to_raw` where the request buffer enters the safe zone.

Parsing, routing, response building, MIME lookup, and config storage are all `_Safe`.

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
