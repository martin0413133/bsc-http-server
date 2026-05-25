#include <stdio.h>
#include "../src/str_util.c"
#include "../src/http_request.c"
#include "../src/http_response.c"
#include "../src/router.c"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

_Safe Response hello(const Request* _Borrow req) { return Response_ok_text("hi", "text/plain"); }

int main(void) {
    Router rt = Router_new();
    Router_add(&_Mut rt, "GET", "/hello", hello);

    Request a = Request_parse("GET /hello HTTP/1.1\r\n\r\n");
    Handler h = Router_match(&_Const rt, &_Const a);
    CHECK(h != nullptr, "match found");

    Request b = Request_parse("GET /nope HTTP/1.1\r\n\r\n");
    CHECK(Router_match(&_Const rt, &_Const b) == nullptr, "no match");

    Request c = Request_parse("POST /hello HTTP/1.1\r\n\r\n");
    CHECK(Router_match(&_Const rt, &_Const c) == nullptr, "method mismatch");

    Request_free(a);
    Request_free(b);
    Request_free(c);
    Router_free(rt);
    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_router OK\n");}
    return 0;
}
