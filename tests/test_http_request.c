#include <stdio.h>
#include "../src/str_util.c"
#include "../src/http_request.c"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

int main(void) {
    const char* raw =
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "User-Agent: test\r\n"
        "\r\n";
    Request req = Request_parse(raw);
    CHECK(cstring_eq_cstr(&_Const req.method, "GET"), "method");
    CHECK(cstring_eq_cstr(&_Const req.path, "/index.html"), "path");
    CHECK(cstring_eq_cstr(&_Const req.version, "HTTP/1.1"), "version");
    CHECK(req.ok, "ok flag");

    const cstring* _Borrow host = Request_get_header(&_Const req, "Host");
    CHECK(host != nullptr && cstring_eq_cstr(host, "localhost"), "host header");
    CHECK(Request_get_header(&_Const req, "Nope") == nullptr, "missing header");

    Request bad = Request_parse("garbage-no-crlf");
    CHECK(!bad.ok, "malformed not ok");

    Request_free(req);
    Request_free(bad);
    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_http_request OK\n");}
    return 0;
}
