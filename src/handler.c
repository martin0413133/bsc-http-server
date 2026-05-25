#include "../include/handler.h"
#include "../include/http_request.h"
#include "../include/file_server.h"
#include "../include/str_util.h"

_Safe Response handle_request(const Config* _Borrow cfg, const Router* _Borrow router,
                              const char* _Nonnull raw) {
    Request req = Request_parse(raw);
    if (!req.ok) {
        Response resp = Response_bad_request();
        Request_free(req);
        return resp;
    }
    Handler h = Router_match(router, &_Const req);
    if (h != nullptr) {
        Response resp = h(&_Const req);
        Request_free(req);
        return resp;
    }
    if (!cstring_eq_cstr(&_Const req.method, "GET")) {
        Response resp = Response_method_not_allowed();
        Request_free(req);
        return resp;
    }
    Response resp = serve_static(cfg, &_Const req);
    Request_free(req);
    return resp;
}
