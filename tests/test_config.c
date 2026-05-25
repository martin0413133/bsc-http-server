#include <stdio.h>
#include "../src/str_util.c"
#include "../src/config.c"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

int main(void) {
    cstring text = cstring_new();
    str_append_cstr(&_Mut text, "# comment\nport = 9090\ndocument_root = public\nthreads=8\n");
    Config c = Config_parse(&_Const text);
    CHECK(c.port == 9090, "port");
    CHECK(c.threads == 8, "threads");
    CHECK(cstring_eq_cstr(&_Const c.document_root, "public"), "doc root");

    Config d = Config_default();
    CHECK(d.port == 8080, "default port");
    CHECK(cstring_eq_cstr(&_Const d.document_root, "www"), "default root");

    cstring bad = cstring_new();
    str_append_cstr(&_Mut bad, "port = 99999\nthreads = 0\n");
    Config e = Config_parse(&_Const bad);
    CHECK(e.port == 8080, "out-of-range port -> default 8080");
    CHECK(e.threads == 1, "threads floor 1");

    cstring_free(text);
    cstring_free(bad);
    Config_free(c);
    Config_free(d);
    Config_free(e);
    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_config OK\n");}
    return 0;
}
