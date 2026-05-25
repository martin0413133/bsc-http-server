#ifndef CC_HTTP_RESPONSE_HBS
#define CC_HTTP_RESPONSE_HBS
#include "cstring.h"

typedef struct Response {
    int status;
    cstring status_text;
    cstring content_type;
    cstring body;
} Response;

_Safe void Response_free(Response r) {
    cstring_free(r.status_text);
    cstring_free(r.content_type);
    cstring_free(r.body);
}

_Safe Response Response_make(int status, const char* _Nonnull status_text);
_Safe Response Response_ok_text(const char* _Nonnull body, const char* _Nonnull content_type);
_Safe Response Response_ok_body(cstring body, const char* _Nonnull content_type);
_Safe Response Response_not_found(void);
_Safe Response Response_bad_request(void);
_Safe Response Response_method_not_allowed(void);
_Safe cstring Response_to_string(const Response* _Borrow this);
#endif
