#include "../include/http_request.h"
#include "../include/str_util.h"

_Safe Request Request_parse(const char* _Nonnull raw) {
    Request r = {
        .method = cstring_new(),
        .path = cstring_new(),
        .version = cstring_new(),
        .headers_len = 0,
        .body = cstring_new(),
        .ok = 0
    };
    request_headers_init(&_Mut r, 8);

    size_t i = 0;
    while (raw[i] != '\0' && raw[i] != ' ' && raw[i] != '\r' && raw[i] != '\n') { cstring_push(&_Mut r.method, raw[i]); i++; }
    if (raw[i] != ' ') { return r; }
    i++;
    while (raw[i] != '\0' && raw[i] != ' ' && raw[i] != '\r' && raw[i] != '\n') { cstring_push(&_Mut r.path, raw[i]); i++; }
    if (raw[i] != ' ') { return r; }
    i++;
    while (raw[i] != '\0' && raw[i] != '\r' && raw[i] != '\n') { cstring_push(&_Mut r.version, raw[i]); i++; }
    if (raw[i] != '\r' && raw[i] != '\n') { return r; }
    if (raw[i] == '\r') { i++; }
    if (raw[i] == '\n') { i++; }

    while (raw[i] != '\0' && raw[i] != '\r' && raw[i] != '\n') {
        Header h = { .name = cstring_new(), .value = cstring_new() };
        while (raw[i] != '\0' && raw[i] != ':' && raw[i] != '\r' && raw[i] != '\n') { cstring_push(&_Mut h.name, raw[i]); i++; }
        if (raw[i] == ':') { i++; }
        while (raw[i] == ' ') { i++; }
        while (raw[i] != '\0' && raw[i] != '\r' && raw[i] != '\n') { cstring_push(&_Mut h.value, raw[i]); i++; }
        if (raw[i] == '\r') { i++; }
        if (raw[i] == '\n') { i++; }
        request_headers_push(&_Mut r, h);
    }
    if (raw[i] == '\r') { i++; }
    if (raw[i] == '\n') { i++; }
    while (raw[i] != '\0') { cstring_push(&_Mut r.body, raw[i]); i++; }

    r.ok = (cstring_len(&_Const r.method) > 0 && cstring_len(&_Const r.path) > 0);
    return r;
}

_Safe const cstring* _Borrow _Nullable Request_get_header(const Request* _Borrow this, const char* _Nonnull name) {
    size_t n = this->headers_len;
    for (size_t k = 0; k < n; k++) {
        const Header* _Borrow h = request_header_get(this, k);
        if (cstring_eq_cstr(&_Const h->name, name)) return &_Const h->value;
    }
    return nullptr;
}
