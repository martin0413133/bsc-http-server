---
name: bsc-common-mistakes
description: "BiSheng C common mistakes and fixes. When you encounter BSC compilation errors, need to debug code, or want to avoid common pitfalls with ownership, borrowing, or safe zones, use this Skill."
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

## 4. Nullability Mistakes

### 4.1 `_Owned` pointer without `_Nullable` when it can be null
```c
// int *_Owned p = nullptr;               // error: _Owned is Nonnull by default
int *_Owned _Nullable p = nullptr;        // correct
```

### 4.2 Dereferencing nullable pointer without null check
```c
_Safe void f(int *_Borrow _Nullable p) {
    // *p = 10;                           // error: nullable pointer
    if (p != nullptr) { *p = 10; }       // correct: null check first
}
```

### 4.3 Passing nullable to nonnull parameter
```c
_Safe void bar(int *_Borrow p) {}     // nonnull param
_Safe void f(int *_Borrow _Nullable p) {
    // bar(p);                          // error: nullable to nonnull
    if (p != nullptr) { bar(p); }      // correct: checked
}
```

## 5. Initialization Mistakes

### 5.1 Using variable before initializing
```c
_Safe void f(void) {
    int x;
    // int y = x;                       // error: uninitialized
    x = 42;
    int y = x;                          // correct
}
```

### 5.2 Array element-by-element assignment not counted
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

### 5.3 Taking address of uninitialized variable
```c
_Safe void f(void) {
    int x;
    // int *_Borrow p = &_Mut x;       // error: x uninitialized
    int x2 = 0;
    int *_Borrow p = &_Mut x2;         // correct
}
```

## 6. Async Mistakes

### 6.1 `_Await` in binary expression
```c
// int result = _Await compute(1) + _Await compute(2);  // error
int a = _Await compute(1);
int b = _Await compute(2);
int result = a + b;
```

### 6.2 Multiple `_Await` in same argument list
```c
// f(_Await g(), _Await h());          // error: multiple _Await at same level
int a = _Await g();
f(a, _Await h());                      // correct: pre-evaluate one
```

## 7. Debugging Runtime Memory Bugs

When BSC code compiles cleanly but **double-frees** or **uses freed memory**
at runtime, follow this triage flow before suspecting the compiler.

### 7.1 Use valgrind, not gdb, to localize double-free

`free(): double free detected in tcache 2` from glibc gives you only the second
free's stack trace under gdb. Valgrind shows BOTH frees and the original `malloc`,
which is what you need to find the aliasing root cause.

```bash
valgrind --error-exitcode=1 --leak-check=no ./binary
```

Look for the "Invalid read" / "Invalid free" report. The "Address X is N bytes inside
a block of size M free'd" line tells you where the same address was freed earlier,
plus the stack at that earlier free.

### 7.2 Rule out test-order pollution before blaming the compiler

If a function works in isolation (standalone repro binary) but fails when called
after other tests in a larger suite, the bug is **state pollution**, not codegen:

- **Mutable globals** (e.g., parser cursors, allocation counters) that earlier
  tests left in a non-zero state
- **Static caches** in your library that don't reset between calls
- **Heap layout sensitivity** — a leak in test N only manifests as a double-free
  in test N+M when allocation sizes happen to alias

Reproduce with: build a minimal `main` that calls **only the failing function**
in a fresh process. If it passes, the bug is contextual.

### 7.3 Inspect the desugared AST (driver mode) before claiming compiler bug

Most "compiler bug" hypotheses turn out to be wrong.
Verify with:

```bash
clang -Xclang -ast-dump -fsyntax-only file.cbs -I./include
```

Look for the `varname_is_moved` flag and the `if (!varname_is_moved) ~Type(varname)`
IfStmt for the variable you suspect. If the compiler's machinery is intact, the
bug is in your library's heap-pointer aliasing.

**Do NOT use `clang -cc1 -fsyntax-only` for this** — it lacks system includes,
which makes every struct with `_Owned` fields spuriously "invalid" in the AST. See the
`/bsc-compile` skill §6 for details.

### 7.4 Common library-level causes of double-free

When the compiler is doing the right thing, the real cause is usually one of:

- **Tagged-struct-as-union**: every instance carries a heap pointer for every
  variant, so two instances can alias if construction shares pointers
  unintentionally. See `/bsc-design` Rule 7 and §2 "Replacing C unions."
- **`safe_swap` between aliased pointers**: a swap that leaves both sides
  pointing at overlapping ownership.
- **Returning a borrow whose underlying owned value is moved by the caller**:
  the borrow becomes dangling.
- **Manual `_Unsafe` byte-assignment overwriting an `_Owned` slot without
  freeing the previous contents**: the old heap pointers leak (single
  copy) or alias (if they were just shifted in by `memmove`).

### 7.5 Triage flow summary

1. Reproduce under valgrind → get both free sites + malloc origin.
2. Test in isolation → confirm/deny test-order pollution.
3. Driver-mode AST dump → confirm compiler move-tracking is correct.
4. Only THEN consider compiler-level bug. In practice, >90% of double-frees in
   BSC projects trace to library-level aliasing, not codegen.

## 8. Quick Reference

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
> For detailed error codes, see `bsc-errors` Skill
> For safe zone rules, see `bsc-safe-zone` Skill
> For ownership rules, see `bsc-ownership` Skill
> For nullability rules, see `bsc-nullability` Skill
> For initialization analysis, see `bsc-initialization` Skill
