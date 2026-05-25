#ifndef CC_CSTRING_HBS
#define CC_CSTRING_HBS
#include <stddef.h>
#include "bishengc_safety.hbs"

#define CSTRING_INIT_CAP 16

typedef struct cstring {
    char *_Owned _ArrayElem buf;
    size_t len;
    size_t cap;
} cstring;

_Safe cstring cstring_new(void);
_Safe void cstring_free(cstring s);
_Safe void cstring_push(cstring* _Borrow s, char c);
_Safe size_t cstring_len(const cstring* _Borrow s);
_Safe char cstring_at(const cstring* _Borrow s, size_t i);
_Safe _Bool cstring_eq(const cstring* _Borrow a, const cstring* _Borrow b);
_Safe void cstring_append_cstr(cstring* _Borrow s, const char* _Nonnull cstr);
_Safe void cstring_append(cstring* _Borrow dst, const cstring* _Borrow src);
_Safe size_t cstring_to_cbuf(const cstring* _Borrow s, char* _Nonnull out, size_t cap);
_Safe _Bool cstring_eq_cstr(const cstring* _Borrow s, const char* _Nonnull cstr);
#endif
