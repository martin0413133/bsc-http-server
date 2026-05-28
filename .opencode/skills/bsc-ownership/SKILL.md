---
name: bsc-ownership
description: "BiSheng C ownership system. When you need to understand _Owned pointers, move semantics, _Nullable, safe_malloc/safe_free, ownership transfer rules, or _Owned with _ArrayElem/unions/function pointers, use this Skill."
---

# BiSheng C Ownership Skill

## CRITICAL: `_Owned` Syntax

**`_Owned` goes AFTER the `*`, not before the type.** It is a pointer qualifier, like `const`.

```c
// CORRECT — _Owned after *
int *_Owned p = safe_malloc(42);

// WRONG — _Owned before type (DOES NOT COMPILE)
_Owned int* p = safe_malloc(42);
```

Same rule applies to `_Borrow`: write `int *_Borrow`, not `_Borrow int*`.

## 1. Overview

Move semantics for compile-time memory safety. Prevents use-after-free and double-free at compile time. Once ownership transfers, the original variable is dead.

Use `safe_malloc` / `safe_free` (from `bishengc_safety.hbs`) as the idiomatic allocation APIs. Raw `malloc`/`free` require `_Unsafe` context and explicit casts.

## 2. safe_malloc / safe_free

Signatures: `T *_Owned safe_malloc<T>(T t)` — allocates, initializes to `t`, returns `T *_Owned`.
`void safe_free(void *_Owned)` — frees an `_Owned` pointer. No cast needed for `safe_malloc`:
```c
#include "bishengc_safety.hbs"
_Safe void example(void) {
    int *_Owned p = safe_malloc(42);       // no cast needed
    safe_free((void *_Owned)p);            // must cast to void *_Owned
}
```

**malloc/free alternative** (requires `_Unsafe` context):
```c
_Unsafe {
    int *_Owned p = (int *_Owned)malloc(sizeof(int));  // raw->owned cast
    *p = 42;
    free((void *_Owned)p);
}
```

## 3. Owned Pointers

```c
#include "bishengc_safety.hbs"

int *_Owned create(int val) {
    return safe_malloc(val);  // ownership transfers to caller
}

void consume(int *_Owned p) {
    int val = *p;
    safe_free((void *_Owned)p);
}

_Safe int main(void) {
    int *_Owned p = create(21);
    int *_Owned p2 = p;        // ownership MOVES; p is now dead
    consume(p2);               // p2 is now dead
    int *_Owned _Nullable maybe = nullptr;  // nullable owned pointer
    return 0;
}
```

## 4. Multi-level Pointer Release (Inner to Outer)

For multi-level pointers, release from inner to outer. For structs with `_Owned` pointer members, free all `_Owned` members first, then the struct pointer.

```c
#include "bishengc_safety.hbs"
struct S { int *_Owned p; int *_Owned q; };

_Safe void foo(void) {
    // Multi-level pointer: free inner first, then outer
    int *_Owned inner = safe_malloc(1);
    int *_Owned *_Owned pp = safe_malloc(inner);
    safe_free((void *_Owned)*pp);
    safe_free((void *_Owned)pp);

    // Struct with _Owned members: free members first, then struct
    struct S s = {.p = safe_malloc(2), .q = safe_malloc(3)};
    struct S *_Owned sp = safe_malloc(s);
    safe_free((void *_Owned)sp->p);
    safe_free((void *_Owned)sp->q);
    safe_free((void *_Owned)sp);
}
```

## 5. Rules

- **`_Owned` goes after the `*`**: write `int *_Owned`, NOT `_Owned int*`
- `_Owned` can only modify **pointer types**, not non-pointer types
- `_Owned` types have **move semantics**: assignment, passing, returning **transfers** ownership
- After transfer, the original variable is **dead** — any use is a compile error
- Before scope ends, `_Owned` variables **must** have ownership released
- Release by: (a) passing to function taking `_Owned`, (b) `safe_free((void *_Owned)p)`, (c) returning, (d) assigning to another `_Owned` variable
- No pointer arithmetic on `_Owned` pointers (no `+`, `-`, `[]`, `++`, `--`). For an `_Owned` pointer that owns an array and needs `[]`, use `_Owned _ArrayElem` instead — see §8.
- Comparison operators (`==`, `!=`, `<`, etc.) are allowed
- No implicit conversion between `_Owned` and raw pointers in **either** direction — strict type matching
- Explicit cast between `_Owned` and raw pointers requires `_Unsafe` context
- Exception: `T *_Owned` -> `void *_Owned` is allowed in safe context

### Prohibited uses of `_Owned`
- **Global variables** (including function-local `static`)
- **Union members**: `_Owned` cannot modify union type members
  ```c
  union U { int *_Owned p; };  // ERROR
  ```
- **Array elements**: cannot store `_Owned` pointers in arrays, including structs with `_Owned` members as array elements. The same restriction applies to the pointee type of `T *_Owned _ArrayElem` (the inner element type cannot have `_Owned` members)

### Fallback for owned-field array types: raw `T*` with `_Unsafe`

When `T *_Owned _ArrayElem` is rejected because `T` contains `_Owned` fields
(e.g. a `Header` with `cstring` fields), the fallback is a raw `T*` field with
manual `malloc`/`realloc`/`free` entirely in `_Unsafe`. Each `_Unsafe` block
must carry a justification comment.

```c
// cstring has _Owned _ArrayElem buf -> Header "contains owned type"
typedef struct Header { cstring name; cstring value; } Header;

typedef struct Router {
    Route* routes_data;      // raw T* — _Owned _ArrayElem rejected for Route
    size_t routes_len;
    size_t routes_cap;
} Router;

// All allocation, access, and deallocation wrapped in _Unsafe:

static inline _Safe void router_routes_init(Router* _Borrow r, size_t cap) {
    // _Unsafe: raw malloc — safe_malloc_array cannot handle Route (contains _Owned fields)
    _Unsafe {
        r->routes_data = (Route*)malloc(cap * sizeof(Route));
        if (!r->routes_data) bsc_bad_alloc_handler(cap * sizeof(Route));
    }
    r->routes_cap = cap;
}

_Safe void Router_free(Router r) {
    for (size_t i = 0; i < r.routes_len; i++) {
        // _Unsafe: nullable raw pointer subscript (routes_data is raw Route*)
        _Unsafe { Route_free(r.routes_data[i]); }
    }
    // _Unsafe: raw free — safe_free_array cannot handle Route (contains _Owned fields)
    _Unsafe { free((void*)r.routes_data); }
}
```

### Nullable `_Owned` pointers
- `int *_Owned _Nullable p = nullptr;` allows null owned pointers
- `__take_from_raw` and `__move_to_raw` preserve Nullability

### Logical and conditional operators
- Logical operators (`!`, `&&`, `||`) work on `_Owned` pointers (null checks, no ownership consumed)
- `_Owned` pointers can be conditions in `if`/`while`/`do-while`/`for`/ternary, but **NOT** `switch`
- Implicit `_Owned` -> `_Bool` conversion is allowed (does not consume ownership)

### Function pointer matching
- Function pointer types must match `_Owned` annotations exactly — cannot assign an `_Owned`-parameter function to a non-`_Owned`-parameter pointer or vice versa

### Explicit ownership transfer interfaces
- `__move_to_raw(p)` — moves ownership out, returns raw pointer
- `__take_from_raw(p)` — takes ownership from raw pointer, returns `_Owned` pointer
- Both preserve Nullability

### Conversion order matters
When converting between `T *_Owned` and `void *`:
- **Order 1**: `T *_Owned` → `void *_Owned` → `void *` (internal `_Owned` pointers must NOT have ownership)
- **Order 2**: `T *_Owned` → `T *` → `void *` (internal `_Owned` pointers KEEP ownership)

The reverse cast `void *_Owned` → `T *_Owned` is allowed (in `_Unsafe`) when the variable still owns memory, **but the resulting struct's inner `_Owned` members do NOT own their pointees** after the cast. You must either reassign them before reading or treat them as raw. Example: after `struct S *_Owned sp = _Unsafe((struct S *_Owned)memAlloc(...));`, reading `sp->p` as `int *_Owned` is a compile error until `sp->p` is reassigned.

## 6. _Safe / _Unsafe Context

- `_Safe` functions use `safe_malloc`/`safe_free`; raw `malloc`/`free` require `_Unsafe` blocks
- Functions without `_Safe` or `_Unsafe` are non-safe by default

## 7. Raw pointer as heap buffer field

A plain `T *_Owned` **forbids array subscript** (`ptr[i]`). For a heap-allocated array that needs indexing you have two options:

1. **`T *_Owned _ArrayElem`** (preferred when the buffer really is a uniform array of `T`) — supports `[]` and pointer arithmetic, stays in `_Safe`.
2. **Raw `T *` field** — needed when the buffer is reinterpreted (byte buffer of mixed records), is allocated by a non-BSC API (`malloc` from a C library), or otherwise doesn't fit `_ArrayElem`'s uniform-element model.

```c
// WRONG — _Owned ptr forbids subscript; this->data[i] = v is a compile error
struct BadBuf {
    uint8_t *_Owned data;
    size_t   used;
};

// CORRECT — raw T* field; owning struct is responsible for freeing
struct GoodBuf {
    uint8_t *data;   // raw pointer — indexing works
    size_t   used;
    size_t   cap;
};
```

All reads and writes through the raw field must be wrapped in `_Unsafe`:

```c
_Safe void buf_write(GoodBuf *_Borrow _Nonnull this, uint8_t v) {
    _Unsafe this->data[this->used] = v;   // raw subscript — _Unsafe statement
    this->used += 1;
}
```

## 8. `_ArrayElem`: Owned Pointers That Index Into Arrays

A second qualifier, `_ArrayElem`, can be combined with `_Owned` (or `_Borrow` —
see `bsc-borrowing`) to form a pointer **into a heap-allocated array of T**.

```
T *_Owned _ArrayElem p;   // owns a heap array of T, supports p[i], p == q
```

`_Owned _ArrayElem` follows all the rules of plain `_Owned` (move semantics,
must be released, no implicit cast to/from raw, etc.) with these differences:

- **`[]` subscript is allowed** (`p[i] = ...`, `int x = p[i];`)
- Pointer **arithmetic is still forbidden** (`p += 1` is an error — even on
  `_Owned _ArrayElem`)
- Comparison (`==`, `!=`) is allowed
- Allocate / free with **array-specific** APIs (from `bishengc_safety.hbs`):

  ```c
  _Safe T *_Owned _ArrayElem safe_malloc_array<T>(size_t n, T initial);
  _Safe void safe_free_array(void *_Owned _ArrayElem);
  ```

- Raw-pointer conversion uses dedicated builtins (not `__move_to_raw` /
  `__take_from_raw`):
  - `__move_array_to_raw(p)` — moves an `_Owned _ArrayElem` out to a raw pointer
  - `__take_array_from_raw(p)` — takes a raw pointer as `_Owned _ArrayElem`
- **C-style casts between `T *`, `T *_Owned`, and `T *_Owned _ArrayElem` are
  forbidden** — these three are distinct categories. Use the array builtins.
- The **pointee type cannot contain `_Owned` members** (same restriction that
  applies to ordinary arrays of `_Owned`).
- `_ArrayElem` cannot modify a **raw** pointer; it only attaches to `_Owned` or
  `_Borrow`.

```c
#include "bishengc_safety.hbs"

_Safe int main(void) {
    int *_Owned _ArrayElem p = safe_malloc_array(10, 0);  // [0]..[9] = 0
    p[3] = 3;                                             // ok: subscript
    // p += 1;                                            // error: no arithmetic
    int *_Owned _ArrayElem q = safe_malloc_array(10, 1);
    if (p == q) { /* ... */ }                             // ok: comparison
    safe_free_array((void *_Owned _ArrayElem)p);
    safe_free_array((void *_Owned _ArrayElem)q);
    return 0;
}
```

**`_Safe` / `_Unsafe` interop**: the compatibility rules treat `_Owned _ArrayElem`
and `_Borrow _ArrayElem` as **whole qualifiers** — a `_Safe` redeclaration may
add `_Owned _ArrayElem` to an unannotated `_Unsafe` parameter, but you cannot
"upgrade" `_Owned` to `_Owned _ArrayElem` or move between `_Owned` and
`_Owned _ArrayElem` across declarations.

### When to prefer `_Owned _ArrayElem` over a raw pointer field

- Buffer is a uniform array of one element type `T` and you index it as such
  → `_Owned _ArrayElem`.
- Buffer is treated as raw bytes, reinterpreted, or comes from a non-BSC
  allocator → keep raw `T *` field + `_Unsafe` (RawVec).

> See also: `bsc-borrowing`, `bsc-safe-zone`, `bsc-nullability`, `bsc-design`, `bsc-errors` Skills
