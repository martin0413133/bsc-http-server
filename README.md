# cc_httpd

A small, multi-threaded static HTTP server written in **BiSheng C (BSC)** — used as a
worked example of building a real program with BSC's ownership/borrow checker rather than
porting existing C. Serves static files from a document root, supports dynamic routes, and
runs a fixed worker-thread pool behind a bounded accept queue.

## Design in one line

The **business core is `_Unsafe`-free** — all `_Unsafe` (sockets, threads, file IO,
`printf`) lives in `src/platform/` adapters that expose a `_Safe` interface. This split is
enforced at build time (`make` runs `check-layers`).

## Layout

```
src/                 business core — _Unsafe-free, all value-type _Owned structs
  main.cbs           entry point; #includes every other .cbs (single translation unit)
  http_request.cbs   request line + header parsing      -> Request
  http_response.cbs  response construction + wire serialization
  router.cbs         exact method+path routing; path_is_safe() traversal guard
  file_server.cbs    maps a request path to a file under document_root
  config.cbs         parses config.ini text (with range validation)
  handler.cbs        request -> route | static | 404 | 405
  mime.cbs / str_util.cbs   helpers
  platform/          adapters: _Safe interface, _Unsafe inside
    net.cbs          POSIX sockets (listen/accept/recv/send + per-conn timeouts)
    thread_pool.cbs  pthread worker pool + bounded queue
    runtime.cbs      accept loop, signal handling, graceful shutdown
    fs.cbs / log.cbs file read / stderr access log
include/             .hbs headers for the business core
tests/               unit tests (.cbs) + valgrind + curl integration scripts
www/                 default document root
```

## Build & run

Requires the BiSheng C toolchain (paths are set in the `Makefile`):

```sh
make            # build bin/httpd (runs the _Unsafe-free layering check first)
make run        # build + run on the port from config.ini (default 8080)
```

Then:

```sh
curl localhost:8080/         # -> index.html
curl localhost:8080/hello    # -> dynamic route
curl -X POST localhost:8080/ # -> 405 (static serving is GET-only)
```

`Ctrl-C` (SIGINT) or SIGTERM triggers a **graceful shutdown**: the accept loop stops,
in-flight requests drain, worker threads join, and the listen socket is released.

## Configuration (`config.ini`)

```ini
port = 8080
document_root = www
threads = 4
```

Out-of-range `port` falls back to 8080; `threads` is floored at 1 and capped at
`TP_MAX_WORKERS` (64).

## Testing

```sh
make test          # build + run every unit test
make valgrind      # run all unit tests under valgrind (catches UAF that exit codes miss)
make integration   # curl-driven checks (200/404/405, MIME, traversal, 50x concurrency)
make check         # all of the above — the full gate
```

> **Why valgrind is part of the gate:** a BSC program can compile *and* pass tests while
> still use-after-free (see `BSC-IMPROVEMENTS.md` #1 / upstream IJC66K). The borrow checker
> is not yet sufficient on its own here, so `make check` runs valgrind too.

## Notes / scope

- HTTP/1.1 with `Connection: close`; one request per connection.
- Each connection is read with a single `recv` (bounded buffer) — large or multi-segment
  requests (e.g. big POST bodies) are out of scope.
- See `BSC-IMPROVEMENTS.md` for ownership-design notes and language/stdlib feedback
  gathered while building this.
</content>
</invoke>
