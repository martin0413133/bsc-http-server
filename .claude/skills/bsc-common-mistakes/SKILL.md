---
name: bsc-common-mistakes
description: "BiSheng C common mistakes and fixes. When you encounter BSC compilation errors, need to debug code, or want to avoid common pitfalls with ownership, borrowing, safe zones, traits, or generics, use this Skill."
---

# BiSheng C Common Mistakes Skill

## 1. Safe Zone Mistakes

### 1.1 Using `&` instead of `&_Const`/`&_Mut`
```c
_Safe void f(void) {
    int x = 42;
    // int* p = &x;                    // error: '&' forbidden in safe zone
    const int *_Borrow p = &_Const x;  // correct
}
```

### 1.2 Using `++`/`--` result as expression
`++`/`--` are allowed as statements but return `void` — cannot use the result.
```c
_Safe void f(void) {
    int i = 0;
    i++;                               // ok: side-effect only
    for (int j = 0; j < 10; j++) {}    // ok: iteration clause
    // int x = i++;                    // error: result type is void
}
```

### 1.3 Empty params without `void`
```c
// _Safe int f() { return 42; }     // error
_Safe int f(void) { return 42; }    // correct
```

### 1.4 Partial init of structs with pointer fields
Only structs with pointer fields require complete initializers. Basic-type-only structs may be partially initialized.
```c
_Safe {
    struct HasPtr hp = {nullptr, 0};  // ok: complete init (has pointer field)
    // struct HasPtr hp2 = {0};       // error: partial init
    struct NoPtr { int a; int b; };
    struct NoPtr np = {0};            // ok: no pointer fields
}
```

### 1.5 Union member access in safe zone
Unions can be declared/initialized/passed, but member access is forbidden.
```c
_Safe {
    union U { int i; float f; } u = {.i = 1};
    // int x = u.i;                   // error
    _Unsafe { int x = u.i; }          // ok: unsafe escape
}
```

### 1.6 Forbidden casts in safe zone
No cross-category pointer casts (`_Owned`/`_Borrow`/raw), no pointer-integer casts, no float-to-integer casts. Exception: `T *_Owned` to `void *_Owned` is allowed.

### 1.7 Mutable borrow of global in safe zone
Only `&_Const` of globals allowed; `&_Mut` of globals is forbidden.
```c
int g = 10;
_Safe void f(void) {
    // int *_Borrow p = &_Mut g;          // error
    const int *_Borrow p = &_Const g;     // ok
}
```

## 2. Ownership Mistakes

### 2.1 Wrong `_Owned`/`_Borrow` qualifier placement
**#1 LLM mistake.** `_Owned`/`_Borrow` go **after the `*`**, not before the type.
```c
// WRONG                              // CORRECT
// _Owned char* msg                   char *_Owned msg = safe_malloc('\0');
// _Borrow int* r                     const int *_Borrow r = &_Const x;
```
Pattern: always `T *_Owned` and `T *_Borrow`, never `_Owned T*` or `_Borrow T*`.

### 2.2 Using variable after ownership transfer
```c
int *_Owned p = safe_malloc(42);
int *_Owned q = p;        // p moved
// printf("%d\n", *p);    // error: use of moved value
```

### 2.3 Forgetting to release `_Owned` before scope ends
```c
void f(void) {
    int *_Owned p = safe_malloc(42);
    safe_free((void *_Owned)p);   // must release: free, pass, or return
}
```

### 2.4 Pointer arithmetic on `_Owned`
```c
int *_Owned p = safe_malloc(42);
// p++;                    // error: no arithmetic on _Owned
// p[3] = 0;               // error: no [] on plain _Owned
```

For a heap **array** that you want to subscript, use `T *_Owned _ArrayElem`
(allocate with `safe_malloc_array`, free with `safe_free_array`). It supports
`p[i]` but still forbids arithmetic — `_Owned _ArrayElem` is **not** a free
upgrade to a C-style array pointer. See `bsc-ownership` §8.

```c
int *_Owned _ArrayElem arr = safe_malloc_array(10, 0);
arr[3] = 3;                            // ok: _ArrayElem allows subscript
// arr += 1;                           // error: still no arithmetic
safe_free_array((void *_Owned _ArrayElem)arr);
```

## 3. Borrowing Mistakes

### 3.1 Mutable borrow while immutable borrow active
```c
int x = 42;
{
    const int *_Borrow r = &_Const x;
    printf("%d\n", *r);
}
int *_Borrow mr = &_Mut x;  // ok: immutable borrow ended
```

### 3.2 Borrow outlives source
```c
// const int *_Borrow dangling(void) {
//     int x = 42;
//     return &_Const x;  // error: borrow outlives local
// }
```

### 3.3 Returning a borrow that depends on a local helper value

One of the most common ownership-model collisions. You compute a local `String`
(or other owned value) to use as a key, then want to return the borrow you looked up
through it.

```c
// WRONG — `key` dies at scope exit, but the returned borrow transitively depends on it
_Safe JSON_Value* _Borrow lookup(Obj* _Borrow this, const char* k) {
    String key = String::from(k);
    JSON_Value* _Borrow child = this->get_value(&_Const key);
    return child;   // error: `key` does not live long enough
}
```

**Three legitimate fixes, in order of preference:**

**Fix A — Take the key as a borrow from the caller** (best). The caller owns the
`String`, its lifetime covers the call:
```c
_Safe JSON_Value* _Borrow lookup(Obj* _Borrow this, const String* _Borrow key) {
    return this->get_value(key);
}
```

**Fix B — Recursion** when the helper value is produced per-step of a traversal.
Pass the remainder of the path into a recursive call; the helper's lifetime is
self-contained to one recursion level:
```c
_Safe static T* _Borrow navigate(Parent* _Borrow p, const Path* _Borrow path, size_t pos) {
    String segment = path->slice(pos, ...);   // local; destroyed at return
    T* _Borrow child = p->get(&_Const segment);
    // tail-call recursion carries child's lifetime, not segment's
    return navigate_further(child, path, next_pos);
}
```

**Fix C — Minimal `_Unsafe` block** when the lookup genuinely has a lifetime the
checker can't model (e.g., hash-map hit is guaranteed to return a pointer into
existing data). Cast through raw pointer:
```c
_Safe JSON_Value* _Borrow lookup(Obj* _Borrow this, const char* k) {
    String key = String::from(k);
    _Unsafe {
        JSON_Value* _Borrow child = (JSON_Value* _Borrow)this->get_value(&_Const key);
        // `key` destroyed at end of block, but the returned borrow points into `this`,
        // not into `key` — checker can't see that.
        return child;
    }
}
```

Use Fix C only when A and B are structurally impossible. Document **why** — next reader
will ask. This is the pattern used by `dotget_value` in parson.

### 3.4 How to read BSC borrow-checker diagnostics

The BSC compiler's borrow diagnostics are more informative than they look at
first. **Always read the `note:` lines, not just the `error:` line** — the
`note:` is where the hint lives.

**Borrow conflict (alias):**
```
file.cbs:22:29: error: cannot borrow `b` as mutable more than once at a time
file.cbs:21:29: note: first mut borrow occurs here
```
The `note:` tells you exactly which earlier line started the conflicting
borrow. Fix: scope the first borrow tighter (wrap it in a block that ends
before the second borrow) or restructure so only one is live at a time.

**Borrow across a conflicting mutation:**
```
error: cannot use `X` because it was mutably borrowed
error: cannot borrow `*X` as mutable more than once at a time
```
Same idea — `note:` points to the first borrow. Typical fix: scope that first
borrow, or do the operation in a different order.

**Use after move (no `note:` today — known gap):**
```
file.cbs:12:13: error: use of moved value: `b`
```
The compiler currently does NOT print a `note:` for where `b` was moved. Use
LSP `hover` on `b`'s declaration to see its full ownership flow (it shows
`Moved into foo()` with the exact line). See `/bsc-compile` §5 for LSP setup.

**Lifetime / borrow returns:**
```
error: no _Borrow qualified type found in the function parameters,
       the return type is not allowed to be _Borrow qualified
```
This explains the rule directly — the function returns a `_Borrow` but has
no `_Borrow` param whose lifetime the return could be tied to. Fix: add a
`_Borrow` parameter whose lifetime matches the returned borrow, or return
an owned value instead.

**Not-obvious messages that trip people up:**
- `does not live long enough` — the compiler name for a local whose
  lifetime is shorter than the context demands (usually a returned
  `_Borrow` pointing to a local). See §3.3 for three fixes.
- `cannot cast between _Owned and raw pointer` — you need
  `__take_from_raw` / `__move_to_raw` for ownership transfer, or
  `(T *)&_Mut *p` / `(T *)&_Const *p` for a non-transferring cast.

**When the diagnostic still isn't enough, use LSP hover:**

Hover on the complaining variable — BSC's LSP (when configured; see
`/bsc-compile` §5) reports its **full ownership timeline including the
live range**:

```
Ownership Flow:
line 21: Declared
line 21: Mut borrow of b
Live range: lines 21-21
```

For an owned value, hover also shows `Moved into foo()` with line numbers.
This is the information the use-after-move diagnostic should print but
doesn't — LSP fills the gap.

## 4. Trait Mistakes

### 4.1 Non-pointer trait variable
```c
// _Trait Printable obj;       // error: only pointer form
_Trait Printable* obj = &val;  // correct
```

### 4.2 Missing `struct` keyword
```c
// void S::method(S* this) { ... }               // error (unless typedef-ed)
void struct S::method(struct S* this) { ... }     // correct
```

### 4.3 `_Impl` before methods defined
```c
// Define methods FIRST, then _Impl
void struct Circle::print(struct Circle* this) { ... }
_Impl _Trait Printable for struct Circle;
```

## 5. Nullability Mistakes

### 5.1 `_Owned` pointer without `_Nullable` when it can be null
```c
// int *_Owned p = nullptr;               // error: _Owned is Nonnull by default
int *_Owned _Nullable p = nullptr;        // correct
```

### 5.2 Dereferencing nullable pointer without null check
```c
_Safe void f(int *_Borrow _Nullable p) {
    // *p = 10;                           // error: nullable pointer
    if (p != nullptr) { *p = 10; }       // correct: null check first
}
```

### 5.3 Passing nullable to nonnull parameter
```c
_Safe void bar(int *_Borrow p) {}     // nonnull param
_Safe void f(int *_Borrow _Nullable p) {
    // bar(p);                          // error: nullable to nonnull
    if (p != nullptr) { bar(p); }      // correct: checked
}
```

## 6. Initialization Mistakes

### 6.1 Using variable before initializing
```c
_Safe void f(void) {
    int x;
    // int y = x;                       // error: uninitialized
    x = 42;
    int y = x;                          // correct
}
```

### 6.2 Array element-by-element assignment not counted
```c
_Safe void f(void) {
    int arr[3];
    arr[0] = 1; arr[1] = 2; arr[2] = 3;
    // int x = arr[0];                  // error: arr not considered initialized
    // FIX: use init list or __assume_initialized
    int arr2[3] = {1, 2, 3};
    int x = arr2[0];                    // correct
}
```

### 6.3 Taking address of uninitialized variable
```c
_Safe void f(void) {
    int x;
    // int *_Borrow p = &_Mut x;       // error: x uninitialized
    int x2 = 0;
    int *_Borrow p = &_Mut x2;         // correct
}
```

## 7. Async Mistakes

### 7.1 `_Await` in binary expression
```c
// int result = _Await compute(1) + _Await compute(2);  // error
int a = _Await compute(1);
int b = _Await compute(2);
int result = a + b;
```

### 7.2 Multiple `_Await` in same argument list
```c
// f(_Await g(), _Await h());          // error: multiple _Await at same level
int a = _Await g();
f(a, _Await h());                      // correct: pre-evaluate one
```

## 8. Debugging Runtime Memory Bugs

When BSC code compiles cleanly but **double-frees** or **uses freed memory**
at runtime, follow this triage flow before suspecting the compiler.

### 8.1 Use valgrind, not gdb, to localize double-free

`free(): double free detected in tcache 2` from glibc gives you only the second
free's stack trace under gdb. Valgrind shows BOTH frees and the original `malloc`,
which is what you need to find the aliasing root cause.

```bash
valgrind --error-exitcode=1 --leak-check=no ./binary
```

Look for the "Invalid read" / "Invalid free" report. The "Address X is N bytes inside
a block of size M free'd" line tells you where the same address was freed earlier,
plus the stack at that earlier free.

### 8.2 Rule out test-order pollution before blaming the compiler

If a function works in isolation (standalone repro binary) but fails when called
after other tests in a larger suite, the bug is **state pollution**, not codegen:

- **Mutable globals** (e.g., parser cursors, allocation counters) that earlier
  tests left in a non-zero state
- **Static caches** in your library that don't reset between calls
- **Heap layout sensitivity** — a leak in test N only manifests as a double-free
  in test N+M when allocation sizes happen to alias

Reproduce with: build a minimal `main` that calls **only the failing function**
in a fresh process. If it passes, the bug is contextual.

### 8.3 Inspect the desugared AST (driver mode) before claiming compiler bug

Most "compiler bug" hypotheses for destructor double-frees turn out to be wrong.
Verify with:

```bash
clang -Xclang -ast-dump -fsyntax-only file.cbs -I./include
```

Look for the `varname_is_moved` flag and the `if (!varname_is_moved) ~Type(varname)`
IfStmt for the variable you suspect. If the compiler's machinery is intact, the
bug is in your library's heap-pointer aliasing.

**Do NOT use `clang -cc1 -fsyntax-only` for this** — it lacks system includes,
which makes every `_Owned struct` spuriously "invalid" in the AST. See the
`/bsc-compile` skill §6 for details.

### 8.4 Common library-level causes of double-free

When the compiler is doing the right thing, the real cause is usually one of:

- **Tagged-struct-as-union**: every instance carries a heap pointer for every
  variant, so two instances can alias if construction shares pointers
  unintentionally. See `/bsc-design` Rule 7 and §2 "Replacing C unions."
- **`safe_swap` between aliased pointers**: a swap that leaves both sides
  pointing at overlapping ownership.
- **Returning a borrow whose underlying owned value is moved by the caller**:
  the borrow becomes dangling.
- **Manual `_Unsafe` byte-assignment overwriting an `_Owned` slot without
  destructing the previous contents**: the old heap pointers leak (single
  copy) or alias (if they were just shifted in by `memmove`).

### 8.5 Triage flow summary

1. Reproduce under valgrind → get both free sites + malloc origin.
2. Test in isolation → confirm/deny test-order pollution.
3. Driver-mode AST dump → confirm compiler destructor insertion is correct.
4. Only THEN consider compiler-level bug. In practice, >90% of double-frees in
   BSC projects trace to library-level aliasing, not codegen.

### 8.6 Known compiler bug: `_Owned` arg in `if`/`while`/`for` condition is not move-tracked

The BSC destructor desugar pass (`SemaBSCDestructor.cpp::VisitCompoundStmt`) only
runs move-tracking for statements whose **outer** type is `BinaryOperator`,
`DeclStmt`, or `CallExpr`. When a function call consuming an `_Owned` argument
appears **inside an `if`/`while`/`for`/`switch` condition**, the outer statement
is `IfStmt` (etc.), so the move-tracking is skipped entirely. The `is_moved`
flag never gets set to 1, and the destructor fires at scope exit on already-freed
memory → double free.

**Symptom:** `free(): double free detected in tcache 2` immediately after a
function call that takes `_Owned` argument(s) and is wrapped in an `if` condition.

**Minimal reproducer (29 lines):**

```c
#include "bishengc_safety.hbs"
#include <stdlib.h>

_Owned struct Box {
_Public:
    int *_Owned data;
    ~Box(Box this) {
        _Unsafe { free((void*)__move_to_raw(this.data)); }
    }
};

_Safe Box Box::new(void) {
    Box b = { .data = safe_malloc(0) };
    return b;
}

_Safe int consume(Box b) { return 1; }   /* takes ownership */

int main(void) {
    Box b = Box::new();
    if (consume(b) == 1) { }  /* BUG: b not marked moved → double free at } */
    return 0;
}
```

**AST evidence** — dump with `clang -Xclang -ast-dump -fsyntax-only`:
- Working pattern (`int r = consume(b); if (r == 1) {}`): AST shows
  `BinaryOperator '=' b_is_moved = 1` after the call.
- Buggy pattern (`if (consume(b) == 1) {}`): NO `b_is_moved = 1` assignment,
  only the initial `b_is_moved = 0` and the destructor `if (!b_is_moved)` check.

**Workaround** — store the call result in a local before testing:

```c
// BAD — double free
if (consume(owned_value) == 1) { ... }

// GOOD — extract the call
int result = consume(owned_value);
if (result == 1) { ... }

// ALSO GOOD — call as standalone statement
consume(owned_value);
if (some_other_check) { ... }
```

**This bug applies to**:
- `if (call(owned) ...)` and `if (... call(owned) ...)`
- `while (call(owned) ...)`, `for (...; call(owned); ...)`
- `switch (call(owned))`
- Any condition expression containing a `CallExpr` that consumes `_Owned` args

**This bug does NOT apply to**:
- `T result = call(owned);` followed by `if (result ...)` — DeclStmt is checked
- `call(owned);` as a standalone statement — CallExpr is checked
- `lhs = call(owned);` as a top-level assignment — BinaryOperator is checked

**When you suspect this bug:** convert one suspected call site to the workaround
form. If the double-free goes away, you've confirmed it. Apply the same workaround
to all sibling sites in the same scope.

### 8.7 Known compiler bug: destructor of `_Owned` local fires BEFORE a return expression that borrows it

The destructor desugar pass emits the destructor `IfStmt` for each `_Owned` local
as a sibling statement **before** the `ReturnStmt` at scope end. If the return
expression is a call that reads through a borrow of that local
(`return f(&_Const b, ...)`), the call runs **on already-destructed memory** and
returns a wrong value. No crash, no warning — just a silently-corrupt return.

This is distinct from §8.6: that one is a missing move-flag assignment in an
`if`/`while` condition. This one is misordering between destructor insertion
and the `ReturnStmt`'s operand evaluation, even when no move is involved.

**Symptom:** a `_Safe` function returning `_Bool` or `int` that contains an
`_Owned` local and a `return f(...borrow of that local...);` returns the wrong
value. Adding any `_Unsafe { printf(...); }` between the call and the return
makes it "start working" — because the print forces the value into a named
temporary whose lifetime spans the destructor.

**Minimal reproducer (30 lines):**

```c
#include "string.hbs"
#include <stdio.h>

_Safe static String make(void) { _Unsafe { return String::from("x"); } }

_Safe static _Bool buggy(const String* _Borrow a) {
    String b = make();
    return a->equals(&_Const b);   /* BUG: returns 0 even though a == b */
}

_Safe static _Bool stashed(const String* _Borrow a) {
    String b = make();
    _Bool r = a->equals(&_Const b);
    return r;                       /* OK: returns 1 */
}

int main(void) {
    String a = make();
    _Unsafe {
        printf("buggy   = %d\n", (int)buggy  (&_Const a));  /* prints 0 */
        printf("stashed = %d\n", (int)stashed(&_Const a));  /* prints 1 */
    }
    return 0;
}
```

**AST evidence** — dump with `clang -Xclang -ast-dump -fsyntax-only`.

Buggy pattern's `CompoundStmt`:
```
├── DeclStmt: b = make()
├── DeclStmt: b_is_moved = 0
├── IfStmt: if (!b_is_moved) ~String(b)     ← destructor fires HERE
└── ReturnStmt
    └── CallExpr: a->equals(&_Const b)      ← b read AFTER destructor
```

Stashed pattern's `CompoundStmt`:
```
├── DeclStmt: b = make()
├── DeclStmt: b_is_moved = 0
├── DeclStmt: r = a->equals(&_Const b)      ← b read BEFORE destructor
├── IfStmt:  if (!b_is_moved) ~String(b)
└── ReturnStmt: return r
```

Same set of nodes, different order. The pass inserts the destructor `IfStmt` at
the end of the statement list regardless of whether the trailing `ReturnStmt`'s
operand still borrows the owned local.

**Triggering conditions** — all three must hold:
1. The function has an `_Owned` local (`String`, any `_Owned struct`) still in scope at return.
2. The return expression is a `CallExpr` that takes a `*_Borrow` to that local (directly or transitively through another arg).
3. The callee actually **dereferences** that borrow to compute the return value. Callees that ignore the borrow argument don't trigger the bug.

**Workaround** — stash the call result in a named local first:

```c
// BAD — wrong return value
_Safe _Bool check(const String* _Borrow a) {
    String b = make();
    return a->equals(&_Const b);
}

// GOOD — same code, result stashed in a local
_Safe _Bool check(const String* _Borrow a) {
    String b = make();
    _Bool r = a->equals(&_Const b);
    return r;
}
```

The stashing forces the use of `b` into an earlier `DeclStmt`, which completes
before the destructor slot. The `ReturnStmt` then only reads `r` (a plain
`_Bool`, no borrow), so the destructor ordering no longer matters.

**How to notice this bug in the wild:** the values coming back from a helper
function look inverted or nonsensical, but the helper's own logic is correct.
Insert a `printf` between the last borrow-use and the return — if the bug
disappears, you're hitting §8.7. Apply the stash workaround.

**This bug applies to any return-expression shape where the return value depends
on a callee reading through a borrow of an in-scope `_Owned` local**, including:
- `return owned_local.method(...);` where the method reads `this`
- `return helper(&_Const owned_local);`
- `return outer(inner(&_Const owned_local));`

**This bug does NOT apply when:**
- The return expression doesn't reference the `_Owned` local at all
- The return value is already a plain copy computed before the return statement
- The `_Owned` local has been moved out before the return (then the destructor
  is skipped by `b_is_moved=1`, and there's no freed-memory read)

## 9. Quick Reference

| Mistake | Fix |
|---------|-----|
| `_Owned char*` | `char *_Owned` — qualifier after `*` |
| `_Borrow int*` | `int *_Borrow` — qualifier after `*` |
| `&` in `_Safe` | Use `&_Const` or `&_Mut` |
| `int x = i++` in `_Safe` | `i++` ok as statement; result is `void` |
| `_Safe int f()` | `_Safe int f(void)` |
| Partial init in safe | Required only for structs **with pointer fields** |
| Union `.member` in safe | Forbidden — use `_Unsafe {}` escape |
| Cast categories in safe | No cross-casts (except `T *_Owned` to `void *_Owned`) |
| `&_Mut` global in safe | Forbidden — only `&_Const` of globals |
| `_Trait T var` | `_Trait T* ptr` |
| Missing `struct` | `struct S` unless typedef-ed |
| Use after move | Use before transferring ownership |
| Owned not freed | `safe_free`, pass, or return before scope ends |
| `_Await` in expr | Assign to variable first |
| Multiple `_Await` in args | Pre-evaluate one: `int a = _Await g(); f(a, _Await h());` |
| `_Owned _Borrow` | Illegal — pick one |
| Borrow of borrow | `T *_Borrow *_Borrow` — restriction (pick single level) |
| `_Owned` without `_Nullable` | Add `_Nullable` if pointer can be null |
| Deref nullable | Null-check first: `if (p != nullptr) { *p = ... }` |
| Array init by element | Use init list `{1,2,3}` or `__assume_initialized` |
| Uninitialized local | Initialize before use; field-level tracking applies |
| Wrong return value from borrow of owned local | Stash into a local first; see §8.7 |

> For detailed error codes, see `bsc-errors` Skill
> For safe zone rules, see `bsc-safe-zone` Skill
> For ownership rules, see `bsc-ownership` Skill
> For nullability rules, see `bsc-nullability` Skill
> For initialization analysis, see `bsc-initialization` Skill
