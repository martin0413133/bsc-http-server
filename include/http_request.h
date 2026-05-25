#ifndef CC_HTTP_REQUEST_HBS
#define CC_HTTP_REQUEST_HBS
#include "cstring.h"

typedef struct Header { cstring name; cstring value; } Header;

_Safe void Header_free(Header h) {
    cstring_free(h.name);
    cstring_free(h.value);
}

typedef struct Request {
    cstring method;
    cstring path;
    cstring version;
    Header* headers_data;
    size_t headers_len;
    size_t headers_cap;
    cstring body;
    _Bool ok;
} Request;

_Safe void Request_free(Request r) {
    cstring_free(r.method);
    cstring_free(r.path);
    cstring_free(r.version);
    for (size_t i = 0; i < r.headers_len; i++) {
        // _Unsafe: nullable raw pointer subscript (headers_data is raw Header*)
        _Unsafe { Header_free(r.headers_data[i]); }
    }
    // _Unsafe: raw free — safe_free_array cannot handle Header (contains _Owned fields)
    _Unsafe { free((void*)r.headers_data); }
    cstring_free(r.body);
}

static inline _Safe void request_headers_init(Request* _Borrow r, size_t cap) {
    // _Unsafe: raw malloc — safe_malloc_array cannot handle Header (contains _Owned cstring fields)
    _Unsafe {
        r->headers_data = (Header*)malloc(cap * sizeof(Header));
        if (!r->headers_data) bsc_bad_alloc_handler(cap * sizeof(Header));
    }
    r->headers_cap = cap;
}

static inline _Safe void request_headers_push(Request* _Borrow r, Header h) {
    if (r->headers_len >= r->headers_cap) {
        // _Unsafe: raw realloc — same reason (Header contains _Owned fields)
        _Unsafe {
            size_t new_cap = r->headers_cap * 2;
            r->headers_data = (Header*)realloc((void*)r->headers_data, new_cap * sizeof(Header));
            if (!r->headers_data) bsc_bad_alloc_handler(new_cap * sizeof(Header));
            r->headers_cap = new_cap;
        }
    }
    // _Unsafe: nullable raw pointer subscript assignment
    _Unsafe { r->headers_data[r->headers_len] = h; }
    r->headers_len = r->headers_len + 1;
}

static inline _Safe const Header* _Borrow request_header_get(const Request* _Borrow r, size_t i) {
    // _Unsafe: BSC cannot create _Borrow from raw array element in _Safe zone
    _Unsafe { return &_Const r->headers_data[i]; }
}

_Safe Request Request_parse(const char* _Nonnull raw);
_Safe const cstring* _Borrow _Nullable Request_get_header(const Request* _Borrow this, const char* _Nonnull name);
#endif
