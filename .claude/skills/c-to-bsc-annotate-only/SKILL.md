---
name: c-to-bsc-annotate-only
description: "Use when hardening an existing/legacy C codebase with BiSheng C memory safety while it must keep compiling as plain C (dual-build), or when you want compile-time leak / use-after-free / double-free checking via ownership annotations WITHOUT rewriting data structures, adopting member functions / generics / traits / destructors / libcbs types, or losing the C build. The conservative, dual-build-preserving subset of c-to-bsc."
---

# C → BSC: Annotation-Only (Dual-Build-Preserving) Hardening

## Overview

Harden legacy C by **only adding ownership annotations** to existing functions and struct
fields. Stay inside the **macro-erasable subset** of BSC so the *same source* still compiles
as plain C (dual-build). You get compile-time leak / use-after-free / double-free / borrow
checking **without** rewriting anything.

**Core insight (verified):** the safety checking comes from the **annotations + borrow
checker**, NOT from destructors, member functions, generics, or libcbs. So you can skip all
of those and still catch the C heap-bug class at compile time — while keeping the C build.

**REQUIRED BACKGROUND:** `c-to-bsc` (per-pointer annotation mechanics, `__take_from_raw`/
`__move_to_raw`), `bsc-ownership`. This skill is the *conservative subset* of `c-to-bsc`:
when the full translation (libcbs, member functions, `_Owned struct` + destructors) is too
disruptive or would lose the C build, use this instead.

## When to use

- You must keep shipping/building the project as **plain C** during (or after) migration.
- You want the heap-bug safety net but **cannot commit to `.cbs`** or a rewrite.
- The codebase is large; you want **minimum diff, maximum safety ROI**, incremental.

**When NOT to use:** greenfield code (use `bsc-design` + libcbs directly); when you want
automatic RAII / high-level containers and don't need a C build (use full `c-to-bsc`).

## The dual-build subset — what you may use vs what kills the C build

| ✅ Allowed (macro-erasable → dual-build holds) | ❌ Forbidden (BSC-only syntax → C build dies, per-file) |
|---|---|
| `_Safe` / `_Unsafe` on functions; `_Unsafe { }` blocks | member functions `Type::method`, method-call sugar `obj->m()` |
| `_Owned` / `_Borrow` / `const _Borrow` on params, returns, **struct fields** | generics: `Vec<T>`, `safe_malloc<T>`, any `<T>` |
| `_Nonnull` / `_Nullable`; `nullptr` | traits, `_Impl` |
| `&_Const` / `&_Mut` address-of | `~T()` destructors / `_Owned struct` **with a destructor body** |
| raw `malloc`/`free`; `__move_to_raw`/`__take_from_raw` | libcbs types: `String`, `Vec`, `Rc`, … |
| **re-declaring** extern/libc fns with ownership (see below) | |

Verified this session: a member function (`S::get`) and a generic call (`safe_malloc<int>`)
both **fail** under `-xc`; bare `_Owned` also fails under `-xc` (it is not C) — which is why
the **shim** below is mandatory for the C build.

## The one piece of infra: the shim header

The BSC compiler predefines `__bishengc` (verified: `#define __bishengc 1`; plain clang does
NOT define it). Guard the shim with it so a **single, unconditional `#include`** is correct in
both builds — inert under BSC (qualifiers stay real keywords), erasing under plain C. No
per-build `-include` juggling; can't leak into the BSC build, can't be forgotten on the C
build. `nullptr` maps to `((void*)0)` (self-contained — no `<stddef.h>`/`NULL` dependency).

```c
/* bsc_shim.h — #include this everywhere; self-activates only under non-BSC compilers */
#ifndef __bishengc
#define _Safe
#define _Unsafe
#define _Owned
#define _Borrow
#define _Nonnull
#define _Nullable
#define _Const
#define _Mut
#define nullptr ((void*)0)
#define __take_from_raw(p) (p)
#define __move_to_raw(p)   (p)
#endif
```

Verified: with this header `#include`d, a leak is still caught under `-x bsc` (shim inert) and
the same source compiles clean under `-xc` (shim erases), with no libc header required.

## Re-declare external paired resources (no wrapper needed)

For **pointer-typed** C resources, re-declare the acquire/release pair with ownership — the
checker then tracks the raw C pointer directly. No wrapper struct, and the call site is
`_Safe`:

```c
_Safe FILE *_Owned _Nullable fopen(const char *_Nonnull, const char *_Nonnull);
_Safe int                    fclose(FILE *_Owned stream);   /* consume == free */
```

Now `fopen` → must reach exactly one `fclose` on every path (else compile-time leak), and
`f` is unusable after `fclose` (compile-time use-after-free). Under the shim these erase to
the standard prototypes. (`free`, `SSL_CTX_new`/`SSL_CTX_free`, etc. follow the same shape.)

> **Exception — `void*`-returning allocators (`malloc`/`calloc`/`realloc`) stay raw.**
> Re-declaring `malloc` as `void *_Owned` backfires: casting that `void *_Owned` to a typed
> `T *_Owned` is *forbidden in the safe zone* (verified: `conversion ... is forbidden in the
> safe zone`). Keep `malloc` raw and bridge with `__take_from_raw` — which, like raw casts
> and `__move_to_raw`, is **not `_Safe`** and must sit in a minimal `_Unsafe { }` block in the
> BSC build (the shim erases it under C, so dual-build still holds). Re-declaration with
> `_Owned` is only for APIs that return the **final typed** pointer (`fopen`→`FILE*`,
> `strdup`→`char*`).

> **`int` file descriptors are the exception:** `_Owned` is a *pointer* qualifier — an `int`
> fd (`open`/`accept` → `close`) cannot carry it. Re-declaration can't help; either leave the
> fd untracked, or wrap it in `_Owned struct Fd { int fd; ~Fd … }` — but that destructor is
> BSC-only and **breaks dual-build for that file**. Decide per case.

## Workflow

1. **Phase 0 — make it build as BSC, unchanged.** Compile each TU with `clang -x bsc` (BSC is
   a C superset; ordinary C usually compiles clean with **zero** annotations). Fix only:
   identifiers colliding with BSC keywords (e.g. `This`), and rare `-xc`-vs-`-xbsc`
   consistency bugs. Run the test suite on the BSC build → behavior must equal the C build.
2. **Phase 1 — re-declare external paired-resource APIs** with ownership (above).
3. **Phase 2 — annotate signatures + struct fields, bottom-up (leaf modules first).** Mark
   every function `_Safe`; give every pointer param/return `_Owned`/`_Borrow`/`const _Borrow`
   (or deliberately-raw with a reason); annotate owning struct fields `_Owned`. The checker
   **forces** a correct free on every path. Compile (BSC) + run tests after each file.
4. **Keep the C build green** alongside (plain `-xc`; the `#include`d shim self-guards via
   `__bishengc`) so you always have a working, diffable baseline.
5. **Verify:** BSC compiles clean **and** tests pass **and** `valgrind` is clean — because of
   the `return f(&_Const local)` codegen UAF, **"compiles" ≠ "memory-safe"** yet.

## What you get vs give up

**Get:** compile-time leak / use-after-free / double-free / borrow-aliasing checking; the C
build preserved throughout (working baseline, diffable, shippable); zero data-structure
rewrite.

**Give up:** automatic RAII — you still **write** the `free` on every path (the compiler just
forces correctness: "checked manual free", not "automatic free"); no auto-destructor cascade
(a plain `struct` with `_Owned` fields is legal and move-semantic, but you free each field
manually — checker-guarded); deep recursive/cyclic ownership may still need raw pointers +
`_Unsafe`; no high-level containers (consistent with annotate-first — adopt libcbs later,
per `bsc-design`, accepting the dual-build loss for those files).

## Common mistakes

| Mistake | Reality |
|---|---|
| Reaching for full `c-to-bsc` (member fns, libcbs, `_Owned struct`+destructor) | Over-disrupts and **kills the C build** per file. Stay annotation-only unless you've decided to drop dual-build for that file. |
| Expecting annotated C to compile as C without the shim | Bare `_Owned` is a parse error under `-xc`. The shim header is mandatory for the C build. |
| Using `safe_malloc<T>` | Generic syntax — breaks dual-build. Use raw `malloc` + `__take_from_raw` (shimmed to identity). |
| Trusting "it compiled, so it's safe" | The return-borrow codegen UAF (upstream IJC66K) passes the checker but is UAF. **Always run valgrind.** |
| Dropping the `#ifndef __bishengc` guard (or force-defining the qualifiers under BSC) | Then `_Owned` etc. erase under BSC too and you lose all checking. The guard is what keeps the shim inert under `-x bsc`. |
| Annotating a param `_Owned` when the function only reads | `_Owned` = consume (caller's var dies). Reads take `const T *_Borrow`. (See `c-to-bsc` Step 2.5.) |
| Re-declaring `malloc`/`calloc`/`realloc` (`void*`-returning) as `void *_Owned` | The cast to `T *_Owned` is forbidden in the safe zone. Keep them raw + `__take_from_raw`. Only re-declare APIs that return the **final typed** pointer. |
| Writing `malloc` / `__take_from_raw` / `__move_to_raw` / a raw cast on a `_Safe` line | None of these are `_Safe`. Wrap the minimal line in `_Unsafe { }` (shim erases it under C → dual-build holds). |
| Building a heap `_Owned`-field struct by field-by-field assignment | Forbidden in the safe zone ("assign to part of _Owned value"). Construct through a raw pointer inside a small `_Unsafe` block, **or** return a value-type aggregate `T` (`{ .f = … }` aggregate-init) where the API can be by-value. This is the one structural exception to "annotation-only". |

## Real-world impact (verified with the project toolchain)

- Annotation-only (re-declared `xmalloc`/`xfree`, **no** destructor/member-fn/generic):
  forgetting the free → `error: memory leak of value: 'p'`; using after free →
  `error: use of moved value: 'p'`. Core checking works with annotations alone.
- The freed form compiles clean as BSC **and** clean as plain C through the self-guarding
  `#ifndef __bishengc` shim (dual-build confirmed; same source, single `#include`).
- A member function and a `safe_malloc<int>` generic each fail under `-xc` — confirming the
  forbidden-list above is what to avoid to keep the C build.
- A subagent application test (hardening a `malloc`+`fopen` module end-to-end) confirmed the
  annotation-only + dual-build flow works (BSC clean, plain C clean, valgrind 0 errors) and
  surfaced the `void*`-allocator and owned-field-heap-construction gotchas now in Common
  mistakes — i.e. annotation-only needs **one** small `_Unsafe` construction block for any
  heap struct with `_Owned` fields.
