#ifndef CC_FILE_SERVER_HBS
#define CC_FILE_SERVER_HBS
#include "http_request.h"
#include "http_response.h"
#include "config.h"

_Safe Response serve_static(const Config* _Borrow config, const Request* _Borrow req);
#endif
