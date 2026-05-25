#include "../include/config.h"
#include "../include/str_util.h"
#include "bishengc_safety.hbs"

_Safe Config Config_default(void) {
    cstring dr = cstring_new();
    str_append_cstr(&_Mut dr, "www");
    cstring sc = cstring_new();
    cstring sk = cstring_new();
    Config c = { .port = 8080, .threads = 4, .document_root = dr,
                 .ssl_port = 0, .ssl_cert = sc, .ssl_key = sk };
    return c;
}

_Safe int parse_int(const cstring* _Borrow s) {
    int v = 0;
    size_t n = cstring_len(s);
    for (size_t i = 0; i < n; i++) {
        char ch = cstring_at(s, i);
        if (ch >= '0' && ch <= '9') { v = v * 10 + (ch - '0'); }
    }
    return v;
}

static _Safe char at_or_nul(const cstring* _Borrow s, size_t i) {
    return i < cstring_len(s) ? cstring_at(s, i) : '\0';
}

_Safe Config Config_parse(const cstring* _Borrow text) {
    int port = 8080;
    int threads = 4;
    cstring dr = cstring_new();
    str_append_cstr(&_Mut dr, "www");
    int ssl_port = 0;
    cstring ssl_cert = cstring_new();
    cstring ssl_key = cstring_new();

    size_t i = 0;
    while (at_or_nul(text, i) != '\0') {
        while (at_or_nul(text, i) == ' ' || at_or_nul(text, i) == '\t') { i++; }
        if (at_or_nul(text, i) == '#' || at_or_nul(text, i) == '\n' || at_or_nul(text, i) == '\r') {
            while (at_or_nul(text, i) != '\0' && at_or_nul(text, i) != '\n') { i++; }
            if (at_or_nul(text, i) == '\n') { i++; }
            continue;
        }
        if (at_or_nul(text, i) == '\0') { break; }
        cstring key = cstring_new();
        cstring val = cstring_new();
        while (at_or_nul(text, i) != '\0' && at_or_nul(text, i) != '=' && at_or_nul(text, i) != '\n' && at_or_nul(text, i) != ' ' && at_or_nul(text, i) != '\t') { cstring_push(&_Mut key, at_or_nul(text, i)); i++; }
        while (at_or_nul(text, i) == ' ' || at_or_nul(text, i) == '\t') { i++; }
        if (at_or_nul(text, i) == '=') { i++; }
        while (at_or_nul(text, i) == ' ' || at_or_nul(text, i) == '\t') { i++; }
        while (at_or_nul(text, i) != '\0' && at_or_nul(text, i) != '\n' && at_or_nul(text, i) != '\r' && at_or_nul(text, i) != ' ') { cstring_push(&_Mut val, at_or_nul(text, i)); i++; }
        while (at_or_nul(text, i) != '\0' && at_or_nul(text, i) != '\n') { i++; }
        if (at_or_nul(text, i) == '\n') { i++; }

        if (cstring_eq_cstr(&_Const key, "port")) { port = parse_int(&_Const val); }
        else if (cstring_eq_cstr(&_Const key, "threads")) { threads = parse_int(&_Const val); }
        else if (cstring_eq_cstr(&_Const key, "document_root")) { safe_swap(&_Mut dr, &_Mut val); }
        else if (cstring_eq_cstr(&_Const key, "ssl_port")) { ssl_port = parse_int(&_Const val); }
        else if (cstring_eq_cstr(&_Const key, "ssl_cert")) { safe_swap(&_Mut ssl_cert, &_Mut val); }
        else if (cstring_eq_cstr(&_Const key, "ssl_key")) { safe_swap(&_Mut ssl_key, &_Mut val); }

        cstring_free(key);
        cstring_free(val);
    }

    if (port < 1 || port > 65535) { port = 8080; }
    if (threads < 1) { threads = 1; }

    Config c = { .port = port, .threads = threads, .document_root = dr,
                 .ssl_port = ssl_port, .ssl_cert = ssl_cert, .ssl_key = ssl_key };
    return c;
}
