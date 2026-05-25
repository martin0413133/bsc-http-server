#include <stdio.h>
#include <string.h>
#include "../src/str_util.c"

static int fails = 0;
#define CHECK(cond, msg) do { if(!(cond)){ _Unsafe{printf("FAIL: %s\n", msg);} fails++; } } while(0)

int main(void) {
    cstring s = cstring_new();
    str_append_cstr(&_Mut s, "GET ");
    str_append_cstr(&_Mut s, "/x");
    CHECK(cstring_len(&_Const s) == 6, "append length");

    char buf[16];
    cstring_to_cbuf(&_Const s, buf, 16);
    _Unsafe { CHECK(strcmp(buf, "GET /x") == 0, "to_cbuf content"); }

    CHECK(cstring_eq_cstr(&_Const s, "GET /x"), "eq true");
    CHECK(!cstring_eq_cstr(&_Const s, "GET /y"), "eq false");

    cstring_free(s);
    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_str_util OK\n");}
    return 0;
}
