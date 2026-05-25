#ifndef CC_ROUTER_HBS
#define CC_ROUTER_HBS
#include "cstring.h"
#include "http_request.h"
#include "http_response.h"

typedef _Safe Response (*Handler)(const Request* _Borrow req);

typedef struct Route { cstring method; cstring path; Handler handler; } Route;

_Safe void Route_free(Route r) {
    cstring_free(r.method);
    cstring_free(r.path);
}

typedef struct Router {
    Route* routes_data;
    size_t routes_len;
    size_t routes_cap;
} Router;

_Safe void Router_free(Router r) {
    for (size_t i = 0; i < r.routes_len; i++) {
        // _Unsafe: nullable raw pointer subscript (routes_data is raw Route*)
        _Unsafe { Route_free(r.routes_data[i]); }
    }
    // _Unsafe: raw free — safe_free_array cannot handle Route (contains _Owned fields)
    _Unsafe { free((void*)r.routes_data); }
}

static inline _Safe void router_routes_init(Router* _Borrow r, size_t cap) {
    // _Unsafe: raw malloc — safe_malloc_array cannot handle Route (contains _Owned cstring fields)
    _Unsafe {
        r->routes_data = (Route*)malloc(cap * sizeof(Route));
        if (!r->routes_data) bsc_bad_alloc_handler(cap * sizeof(Route));
    }
    r->routes_cap = cap;
}

static inline _Safe void router_routes_push(Router* _Borrow r, Route rt) {
    if (r->routes_len >= r->routes_cap) {
        // _Unsafe: raw realloc — same reason (Route contains _Owned fields)
        _Unsafe {
            size_t new_cap = r->routes_cap * 2;
            r->routes_data = (Route*)realloc((void*)r->routes_data, new_cap * sizeof(Route));
            if (!r->routes_data) bsc_bad_alloc_handler(new_cap * sizeof(Route));
            r->routes_cap = new_cap;
        }
    }
    // _Unsafe: nullable raw pointer subscript assignment
    _Unsafe { r->routes_data[r->routes_len] = rt; }
    r->routes_len = r->routes_len + 1;
}

static inline _Safe const Route* _Borrow router_route_get(const Router* _Borrow r, size_t i) {
    // _Unsafe: BSC cannot create _Borrow from raw array element in _Safe zone
    _Unsafe { return &_Const r->routes_data[i]; }
}

_Safe Router Router_new(void);
_Safe void Router_add(Router* _Borrow this, const char* _Nonnull method, const char* _Nonnull path, Handler h);
_Safe Handler _Nullable Router_match(const Router* _Borrow this, const Request* _Borrow req);

_Safe _Bool path_is_safe(const cstring* _Borrow path);
#endif
