---
name: bsc-initialization
description: "BiSheng C initialization analysis. When you need to understand uninitialized variable detection, field-level init tracking, __attribute__((ensure_init)), __assume_initialized, array initialization rules, or -uninit-check compiler option, use this Skill."
---

# BiSheng C Initialization Analysis Skill

## 1. Overview

BSC performs compile-time data-flow analysis to ensure variables are initialized before use. Analysis tracks initialization at the **struct field level**. By default active in `_Safe` zones; configurable via `-uninit-check`.

## 2. Rules

### Rule 1: All locals must be initialized before use

```c
_Safe void example(void) {
    int x;
    int y = x;  // error: use of uninitialized value: `x`
}
```

### Rule 2: Field-level tracking for structs

Partial field initialization is detected. When all fields are initialized, the struct auto-promotes to fully initialized.

```c
struct Pair { int a; int b; };

_Safe void partial(void) {
    struct Pair p;
    p.a = 1;
    struct Pair q = p;  // error: p.b uninitialized
}

_Safe void full(void) {
    struct Pair p;
    p.a = 1;
    p.b = 2;           // all fields done → p auto-promotes
    struct Pair q = p;  // ok
}
```

### Rule 3: All control-flow paths must initialize

```c
_Safe void example(int cond) {
    int x;
    if (cond) { x = 1; }
    // else path: x uninitialized
    int y = x;  // error: use of possibly uninitialized value: `x`
}
```

### Rule 4: Address-of is a use

Taking `&`, `&_Mut`, or `&_Const` of an uninitialized variable is an error. Exception: `ensure_init` parameters and `__assume_initialized` arguments.

```c
_Safe void example(void) {
    int x;
    int *_Borrow p = &_Mut x;  // error: use of uninitialized value: `x`
}
```

### Rule 5: Functions must initialize return values on all paths

```c
_Safe int example(int cond) {
    if (cond) { return 1; }
}  // error: return value may not be initialized on all paths
```

### Rule 6: Array element assignment does NOT initialize

Element-by-element assignment does not mark an array as initialized. Arrays must use init lists or `__assume_initialized`.

```c
_Safe void bad(void) {
    int arr[3];
    arr[0] = 1; arr[1] = 2; arr[2] = 3;
    int x = arr[0];  // error: arr still uninitialized
}

_Safe void good(void) {
    int arr[3] = {1, 2, 3};  // ok: init list
    int x = arr[0];
}

_Safe void assume(void) {
    int arr[3];
    arr[0] = 1; arr[1] = 2; arr[2] = 3;
    _Unsafe { __assume_initialized(&arr); }
    int x = arr[0];  // ok
}
```

This also applies to array fields inside structs:

```c
typedef struct { int a[2]; int b; } ArrStruct;

_Safe void bad(void) {
    ArrStruct s;
    s.a[0] = 1; s.a[1] = 2; s.b = 3;
    ArrStruct t = s;  // error: s.a uninitialized
}

_Safe void good(void) {
    ArrStruct s = {{1, 2}, 3};
    ArrStruct t = s;  // ok
}
```

### Rule 7: Union — writing any member initializes the whole union

```c
// Compile with -uninit-check=all (union access forbidden in safe zones)
union U { int a; float f; };

void example(void) {
    union U u;
    u.a = 42;
    float f = u.f;  // ok: entire union initialized via u.a
}
```

Writing a struct member within a union variant also initializes the whole union. Known limitation: cross-variant reads of unwritten bytes pass without error.

### Rule 8: Nested structs — arbitrary depth tracking

```c
struct Inner { int x; int y; };
struct Outer { struct Inner inner; int z; };

_Safe void example(void) {
    struct Outer o;
    o.inner.x = 1;
    o.inner.y = 2;  // inner auto-promotes
    o.z = 3;        // o auto-promotes
    struct Outer p = o;  // ok
}
```

### Rule 9: Global and static variables are implicitly initialized

```c
static int global_count;

_Safe void example(void) {
    int x = global_count;  // ok: guaranteed zero-initialized by C spec
}
```

## 3. `__attribute__((ensure_init))`

A parameter attribute that establishes an initialization contract:
- **Caller side**: after the call, `*param` is marked as initialized
- **Callee side**: compiler verifies `*param` is initialized on all return paths

```c
void init_int(int *__attribute__((ensure_init)) out);

_Safe void caller(void) {
    int x;
    _Unsafe { init_int(&x); }  // x marked initialized after call
    int y = x;                  // ok
}

// Compiler validates the contract:
void good_init(int *__attribute__((ensure_init)) out) {
    *out = 42;  // ok
}

void bad_init(int *__attribute__((ensure_init)) out) {
}  // error: ensure_init parameter 'out' not initialized at return
```

### Field-level partial initialization

```c
struct Pair { int a; int b; };
void init_field(int *__attribute__((ensure_init)) p);

_Safe void example(void) {
    struct Pair p;
    _Unsafe {
        init_field(&p.a);  // only p.a marked
        init_field(&p.b);  // p.b marked → p auto-promotes
    }
    struct Pair q = p;  // ok
}
```

### Safe zone usage

```c
_Safe void init_safe(int *_Borrow __attribute__((ensure_init)) out);

_Safe void caller(void) {
    int x;
    init_safe(&_Mut x);  // ok in safe zone with &_Mut
    int y = x;            // ok
}
```

### Restrictions before `*param` is initialized

Cannot reassign or alias the `ensure_init` pointer before fulfilling the contract:

```c
void bad(int *__attribute__((ensure_init)) out) {
    int local;
    out = &local;   // error: cannot reassign before *out initialized
}

void bad2(int *__attribute__((ensure_init)) out) {
    int *p = out;   // error: cannot alias before *out initialized
}
```

After initialization, free use is allowed:

```c
void ok(int *__attribute__((ensure_init)) out) {
    *out = 42;       // contract fulfilled
    int *p = out;    // ok: *out already initialized
    out = &local;    // ok
}
```

### Delegation

`ensure_init` can delegate to another `ensure_init` function:

```c
void init_val(int *__attribute__((ensure_init)) out);

void init_delegated(int *__attribute__((ensure_init)) out) {
    init_val(out);  // ok: delegates to another ensure_init
}
```

### Redeclaration rules

Same safety-level redeclarations must be consistent (both have `ensure_init` or neither). Different safety levels (`_Safe` vs non-safe) are independent overloads where differences are allowed.

### Function pointer compatibility

`ensure_init` is part of the function type. Cannot assign a non-`ensure_init` function to an `ensure_init` function pointer:

```c
typedef _Safe void (*InitFn)(int *__attribute__((ensure_init)) _Borrow out);

_Safe void has_attr(int *__attribute__((ensure_init)) _Borrow out) { *out = 1; }
_Safe void no_attr(int *_Borrow out) { *out = 1; }

_Safe void test(void) {
    InitFn fn = has_attr;  // ok
    // InitFn fn2 = no_attr;  // error: missing ensure_init
}
```

Indirect calls through function pointers support `ensure_init` tracking:

```c
_Safe void indirect_call(InitFn fn) {
    int x;
    fn(&_Mut x);   // x marked initialized via ensure_init
    int y = x;     // ok
}
```

## 4. `__assume_initialized`

Built-in function to mark a variable as initialized at a program point. No contract verification — user guarantees correctness.

```c
_Safe void example(void) {
    int x;
    _Unsafe { __assume_initialized(&x); }
    int y = x;  // ok
}
```

- Must use `&` prefix: `__assume_initialized(&x)` (not `__assume_initialized(x)`)
- Only in `_Unsafe` blocks
- Path-sensitive: only effective on the CFG path where it executes
- One variable per call
- For arrays: `__assume_initialized(&arr)` (required due to array decay)
- For structs: marks all fields as initialized

### Supported argument forms

The address-of operand must be one of:

| Form | Meaning |
|------|---------|
| `&x` | local variable; if `x` is an `ensure_init` pointer parameter, also marks `*x` initialized |
| `&x.f.g...` | local struct field, any nesting (pure field path) |
| `&*p` | the object pointed-to by an `ensure_init` parameter `p` |
| `&p->f.g...` | field of the object pointed-to by an `ensure_init` parameter (pure field path) |

When all fields of `*p` are marked through `&p->...`, `*p` is automatically
promoted to fully initialized.

```c
struct Pair { int a; int b; };
void assume_through_ptr(struct Pair *__attribute__((ensure_init)) out) {
    out->a = 1;
    _Unsafe { __assume_initialized(&out->b); }   // all fields covered → *out initialized
}
```

### **Array subscripts (`[i]`) are NOT allowed in the path**

Init analysis treats arrays as **single units** — individual elements are not
tracked, so you can only assume the whole array, never an element.

```c
struct WithArr { int arr[3]; };
struct Outer   { struct WithArr s[2]; };

_Safe void example(void) {
    struct WithArr w;
    _Unsafe { __assume_initialized(&w.arr); }       // ok: whole array field

    // _Unsafe { __assume_initialized(&w.arr[0]); }     // error: subscript in path
    // _Unsafe { __assume_initialized(&o.s[0].arr); }   // error: subscript in middle
    // _Unsafe { __assume_initialized(&o.s[0].arr[0]); }// error: subscript at end
}
```

```c
// Path-sensitive:
_Safe void example(int cond) {
    int x;
    if (cond) { _Unsafe { __assume_initialized(&x); } }
    int y = x;  // error: possibly uninitialized (not all paths)
}
```

## 5. Compiler Options

`-uninit-check=<mode>`:

| Mode | Behavior |
|------|----------|
| `none` | Disable initialization analysis |
| `safeonly` (default) | Check in `_Safe` zones only; `ensure_init` contract verification always active |
| `all` | Check in all code (including non-safe) |

Note: `ensure_init` contract verification is active whenever mode is not `none`, even outside safe zones.

> For safe zone rules, see `bsc-safe-zone` Skill
> For ownership initialization patterns, see `bsc-ownership` Skill
> For common initialization mistakes, see `bsc-common-mistakes` Skill
