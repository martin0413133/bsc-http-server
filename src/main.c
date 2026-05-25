// cc_httpd entry point — single translation unit aggregator.
// Business core (no _Unsafe):
#include "str_util.c"
#include "mime.c"
#include "http_request.c"
#include "http_response.c"
#include "config.c"
#include "router.c"
#include "file_server.c"
#include "handler.c"
// Adapters (_Unsafe inside, _Safe interface):
#include "platform/cstring.c"
#include "platform/fs.c"
#include "platform/net.c"
#include "platform/log.c"
#include "platform/thread_pool.c"
#include "platform/runtime.c"

_Safe Response route_hello(const Request* _Borrow req __attribute__((unused))) {
    Response resp = Response_ok_text("<h1>Hello from a cc_httpd route!</h1>", "text/html");
    return resp;
}

_Safe int main(void) {
    cstring text = cstring_new();
    fs_read_file("config.ini", &_Mut text);
    Config config = Config_parse(&_Const text);

    Router router = Router_new();
    Router_add(&_Mut router, "GET", "/hello", route_hello);

    Config *_Owned cfgp = safe_malloc(config);
    Router *_Owned rtp = safe_malloc(router);
    cstring_free(text);
    int rc = runtime_serve(cfgp, rtp);
    return rc;
}
