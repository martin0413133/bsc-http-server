#ifndef CC_HANDLER_HBS
#define CC_HANDLER_HBS
#include "config.h"
#include "router.h"
#include "http_response.h"

_Safe Response handle_request(const Config* _Borrow cfg, const Router* _Borrow router,
                              const char* _Nonnull raw);
#endif
