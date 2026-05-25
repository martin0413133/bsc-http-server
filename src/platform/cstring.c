#ifndef CC_CSTRING_IMPL
#define CC_CSTRING_IMPL
#include "../../include/cstring.h"

_Safe cstring cstring_new(void) {
    char *_Owned _ArrayElem buf = safe_malloc_array<char>(CSTRING_INIT_CAP, '\0');
    cstring s = { .buf = buf, .len = 0, .cap = CSTRING_INIT_CAP };
    return s;
}

_Safe void cstring_free(cstring s) {
    safe_free_array(s.buf);
}

_Safe void cstring_push(cstring* _Borrow s, char c) {
    if (s->len + 1 >= s->cap) {
        size_t new_cap = s->cap * 2;
        cstring new_s;
        new_s.buf = safe_malloc_array<char>(new_cap, '\0');
        new_s.len = s->len;
        new_s.cap = new_cap;
        for (size_t i = 0; i < s->len; i++) { new_s.buf[i] = s->buf[i]; }
        safe_swap(s, &_Mut new_s);
        cstring_free(new_s);
    }
    s->buf[s->len] = c;
    s->len++;
    s->buf[s->len] = '\0';
}

_Safe size_t cstring_len(const cstring* _Borrow s) {
    return s->len;
}

_Safe char cstring_at(const cstring* _Borrow s, size_t i) {
    return s->buf[i];
}

_Safe _Bool cstring_eq(const cstring* _Borrow a, const cstring* _Borrow b) {
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->len; i++) {
        if (a->buf[i] != b->buf[i]) return 0;
    }
    return 1;
}

_Safe void cstring_append_cstr(cstring* _Borrow s, const char* _Nonnull cstr) {
    for (size_t i = 0; cstr[i] != '\0'; i++) {
        cstring_push(s, cstr[i]);
    }
}

_Safe void cstring_append(cstring* _Borrow dst, const cstring* _Borrow src) {
    for (size_t i = 0; i < src->len; i++) {
        cstring_push(dst, src->buf[i]);
    }
}

_Safe size_t cstring_to_cbuf(const cstring* _Borrow s, char* _Nonnull out, size_t cap) {
    if (cap == 0) return 0;
    size_t n = s->len;
    if (n > cap - 1) n = cap - 1;
    for (size_t i = 0; i < n; i++) { out[i] = s->buf[i]; }
    out[n] = '\0';
    return n;
}

_Safe _Bool cstring_eq_cstr(const cstring* _Borrow s, const char* _Nonnull cstr) {
    size_t i = 0;
    while (cstr[i] != '\0') {
        if (i >= s->len) return 0;
        if (s->buf[i] != cstr[i]) return 0;
        i++;
    }
    return i == s->len;
}
#endif
