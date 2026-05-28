---
name: bsc-design
description: "BiSheng C design and architecture decisions. Load this Skill BEFORE writing a plan, proposal, or implementation that involves any non-trivial BSC change — including: new modules or APIs, new functions with _Owned/_Borrow parameters, refactors that change ownership flow, adding features to existing types, choosing where the _Safe/_Unsafe boundary goes, or deciding between value types and *_Owned pointers. Use this Skill at PLANNING time, not after the plan is written. Skip only for: typo fixes, comment edits, single-line bug fixes that don't change types or signatures."
---

# BiSheng C Design Skill

BSC is not just "C with annotations" — the ownership system changes how APIs are shaped.
This skill covers design decisions you face when **starting from a blank file**, not when
porting existing C.

## 1. Rules of Thumb (read this first)

Six rules to consult when designing any BSC API. If you only remember one thing,
remember **Rule 2**.

### Rule 1 — Return values, borrow arguments
Return `_Owned` by value; take `_Borrow` as arguments. Only take `_Owned` arguments when
the function truly **consumes** (moves) the value (e.g. `Vec::push`, `set_string`).

```c
// GOOD: caller keeps ownership, function borrows
_Safe String format_greeting(const String* _Borrow name);

// BAD: caller must move a value it probably still wants
_Safe String format_greeting(String name);
```

### Rule 2 — Make the `_Unsafe` surface as small as possible
Don't mark whole functions `_Unsafe` just because one line needs it. Wrap the minimum
with `_Unsafe { ... }` and keep the function's interface `_Safe`. An `_Unsafe` interface
is **contagious** — every caller pays the cost.

```c
// GOOD: interface _Safe, only the cast is unsafe
_Safe String from_cstr(const char* s) {
    _Unsafe { return String::from(s); }
}

// BAD: whole function is _Unsafe for one line's sake
_Unsafe String from_cstr(const char* s) {
    return String::from(s);
}
```

### Rule 3 — Choose `T` vs `T *_Owned` deliberately

This is a real design decision, not a default. Both are valid; pick based on the type's
size, lifetime, and use sites.

**Return `T` (by value) when:**
- The type is **small to medium** (~< few hundred bytes) — moves are cheap.
- The caller will use it locally and let it drop at scope exit.
- Nullability isn't needed (no "no result" case to represent).
- You want zero-allocation construction.

```c
_Safe String String::new(void);                       // small, local use
_Safe JSON_Value json_parse_file(String filename);    // medium, owned by caller
```

**Return `T *_Owned` when:**
- The type is **large** — pointer-move is `O(1)`, value-move copies bytes.
- The result may be **null** (`T *_Owned _Nullable`) — value types can't naturally
  represent absence without `Option<T>` overhead.
- The result will be **stored in a container** that holds pointers (`Vec<Node *_Owned>`
  for graph/tree nodes — recursive types can't be value-stored).
- The caller's lifetime model needs **explicit `safe_free`** (e.g., handing off to
  C code, custom allocators).

```c
_Safe Buffer *_Owned Buffer::with_capacity(size_t mb);   // large
_Safe Node *_Owned _Nullable find(Tree* _Borrow t, Key k);  // may be absent
```

**Reality check:** parson uses both. `json_parse_file` returns `JSON_Value` by value (the
struct is ~100 bytes), but internally `JSON_Value_Value` holds `JSON_Object *_Owned`
and `JSON_Array *_Owned` because those subtypes are heap-allocated and need to be
nullable. Don't assume value-return is always right; assume **the right choice depends
on size and nullability**.

### Rule 4 — Many `_Const` readers OR one `_Mut` writer (never both)

`_Const` borrows compose: any number of immutable borrows can coexist on the same value.
What the checker forbids is **overlapping a `_Mut` with anything else** — another `_Mut`,
or any `_Const`.

```c
// FINE: many readers, no writer
const T* _Borrow r1 = &_Const x;
const T* _Borrow r2 = &_Const x;
const T* _Borrow r3 = &_Const x;
read(r1); read(r2); read(r3);   // all OK

// BAD: writer overlaps reader
const T* _Borrow r = &_Const x;
T* _Borrow w = &_Mut x;          // error: r is still live
modify(w);

// BAD: two writers overlap
T* _Borrow w1 = &_Mut x;
T* _Borrow w2 = &_Mut x;          // error: w1 is still live
```

**Design implication:** if your API needs multiple readers, take `const T* _Borrow` and
pass freely. If it needs to mutate, take `T* _Borrow` (mut) and ensure no other borrow is
live during the call. If you find yourself wanting overlapping `_Mut` borrows, the design
is wrong — restructure.

```c
// COMMON DESIGN ERROR: tries to mut-borrow `val` twice
JSON_Object* _Borrow obj = val.get_object();   // mut borrow #1
val.validate(&_Mut schema);                     // mut borrow #2 — conflicts

// FIX: scope the first borrow tightly
{
    JSON_Object* _Borrow obj = val.get_object();
    obj->set_string(...);
}   // borrow #1 ends here
val.validate(&_Mut schema);   // now OK
```





### Rule 5 — Pick the unsafe seam deliberately
Every BSC library has at least one `_Unsafe` seam. Place it at the **boundary** where
BSC's model doesn't match reality:

- **FFI / system calls** (`fopen`, `read`, pointer arithmetic on buffers)
- **Interior mutability / shared references** (reference-counted containers like
  `Rc<T>` expose `_Unsafe` peek methods)
- **Performance-critical hot paths** (where the checker would force unnecessary clones)

Lookups that may miss are **not** a seam — return `T *_Borrow _Nullable` (compile-time
nullability tracking keeps the call site `_Safe`). See Rule 8.

Never put the seam in the middle of business logic.

### Rule 6 — Prefer `_Nonnull`; reach for `_Nullable` only when "absent" is real

`_Owned` and `_Borrow` default to `_Nonnull`. Keep them that way unless the API genuinely
has a no-result/optional/absent case to represent. `_Nullable` forces every use site to
null-check before deref — choosing it when the pointer is never actually null just adds
caller burden for no benefit.

**Keep default `_Nonnull`** when:
- The parameter is required (no "absent input" case)
- The return is always produced when the function returns successfully
- The struct field is always populated after construction

**Reach for `_Nullable`** when:
- Allocation may fail and you surface that: `T *_Owned _Nullable try_alloc(size_t)`
- Cleanup paths hold a pointer that may not have been set: `Node *_Owned _Nullable item = nullptr;`
- A struct field is legitimately optional
- A setter accepts "clear" as a valid input: `void set_cache(Cache *_Owned _Nullable c)`

```c
// GOOD — Nonnull by default; caller doesn't null-check
_Safe void render(const Canvas *_Borrow c, const Frame *_Borrow f);

// GOOD — allocation may fail, nullable surfaces it
_Safe Buffer *_Owned _Nullable try_create_buffer(size_t n);

// BAD — declared _Nullable out of caution; callers null-check for no reason
_Safe void render(const Canvas *_Borrow _Nullable c, const Frame *_Borrow _Nullable f);
```

Changing a pointer's nullability (Nonnull ↔ Nullable) on an existing API is a **breaking
change** — every caller's null-check assumption shifts. Decide at design time, not patch time.



## 2. Designing Types

### Value type, `_Owned struct`, or trait?

| Your type has... | Use |
|---|---|
| Plain data (no heap pointers, no cleanup) | Plain `struct` (not `_Owned`) |
| Fields that own heap memory (`String`, `Vec`, `*_Owned`) | `_Owned struct` |
| Needs dynamic dispatch (multiple "kinds" behind one interface) | `_Trait` + `_Impl` |
| A closed set of variants (like a union) | `_Owned struct` with a tag field + inner union replacement |

**Don't reach for `_Trait` unless you actually need dynamic dispatch.** If you know the
concrete type at compile time, a plain `_Owned struct` or generic `<T>` is faster and
clearer.

### Replacing C unions

C unions let you put a `double`, `char*`, and a pointer all in the same storage slot.
In BSC that's unsound: the destructor can't know which variant is active, so it can't
run the right field's destructor.

**Two options:**

1. **Tagged struct** — all fields coexist; destructor runs over all of them (even
   inactive ones). Simple but wastes memory. Parson's `JSON_Value_Value` does this,
   with a FIXME comment acknowledging the cost.

2. **Trait-based sum type** — one trait with one `_Impl` per variant. Dynamic dispatch
   overhead, but memory-efficient and type-safe. Use when variants have very different
   sizes.

**Rule of thumb:** if the largest variant is <3× the smallest, use tagged struct. If
they're wildly different sizes (e.g., a variant holds a 1KB buffer), use traits.

### Destructor design

The destructor body is usually **empty**. BSC auto-inserts destructor calls for every
`_Owned` field. Only write explicit cleanup when:

- The struct holds a **raw pointer** to heap memory (not `*_Owned`). You must
  `safe_free` it manually.
- There's a **resource** other than memory (file handle, socket, lock) that needs
  release.
- You need to run **logic** before cleanup (logging, notifying observers).

```c
_Owned struct FileHandle {
    FILE *_Owned fp;   // destructor fires automatically? NO — FILE* has no destructor

    ~FileHandle(FileHandle this) {
        _Unsafe { fclose(__move_to_raw(this.fp)); }
    }
};
```

---

## 2. Designing APIs

### Value-in, value-out vs. borrow-in, borrow-out

```c
// Pattern A: ownership transfer
_Safe JSON_Object JSON_Object::new(void);            // produces owned value
_Safe void Obj::set_value(Obj* _Borrow this, JSON_Value v);  // consumes `v`

// Pattern B: read-only access
_Safe const String* _Borrow Obj::get_name(const Obj* _Borrow this, size_t i);
_Safe size_t Obj::get_count(const Obj* _Borrow this);

// Pattern C: mutation via borrow
_Safe void Obj::clear(Obj* _Borrow this);
```

**Rule:** read → `_Borrow this` + `_Borrow` returns. Mutate → `_Borrow this` (mut)
+ `void` return or status code. Construct → `_Owned` return, no `this`.

### Returning `_Borrow` that depends on a local

This bites everyone eventually. If you compute a local `String` and want to return a
`_Borrow` to something looked up through it, the checker rejects it because the local
dies at scope exit.

**Three legitimate fixes:**

1. **Restructure** — take the key by borrow from the caller:
   ```c
   _Safe T* _Borrow lookup(Table* _Borrow t, const String* _Borrow key);
   ```

2. **Recursion** — if the local is a transformation of an input, pass the transformed
   value into a recursive call where its lifetime is self-contained.

3. **Minimal `_Unsafe` seam** — for lookups that the checker can't model (hash-map hits),
   cast through a raw pointer inside a tight `_Unsafe` block. Document why. (Parson's
   `dotget_value` does this.)

### Constructor pattern

BSC has no special constructor syntax. Use `Type::new` (and variants `with_capacity`,
`from`, `default`) as a convention:

```c
_Safe String String::new(void);
_Safe String String::with_capacity(size_t cap);
_Unsafe String String::from(const char* cstr);

_Safe Config Config::new(void);
_Safe Config Config::from_file(String path);
```

---

## 3. Designing Ownership Flow

### Where ownership lives

For every heap allocation, decide: **who owns it, for how long, and how does it get
freed?** Then make the types reflect the answer.

```c
// Owner: the struct. Lifetime: with the struct. Freed: by destructor.
_Owned struct Cache { Vec<Entry> entries; };

// Owner: the caller of add(). Lifetime: until it's moved into the vec.
_Safe void Cache::add(Cache* _Borrow this, Entry e);  // consumes `e`

// Owner: the cache. Returned as a borrow for read-only use.
_Safe const Entry* _Borrow Cache::get(const Cache* _Borrow this, size_t i);
```

### Move semantics at API edges

When a function takes `_Owned T` (or `T` by value for an owned type), the caller loses
access to the variable after the call. Design your API so the caller either:

- **Moves it in deliberately** (e.g. `vec.push(item)` — item is consumed, caller expects that)
- **Passes a clone** if they need to keep using the value (`vec.push(item.clone())`)

Don't have "sometimes consumes, sometimes doesn't" functions — pick one and stick to it.

### Sharing: avoid, or use `Rc<T>`

BSC is single-owner by default. If two places need to own the same value, use
`Rc<T>` (reference-counted) from stdlib-advanced. Don't invent your own sharing scheme —
you'll break the ownership invariants.

### Sharing long-lived read-only state with worker threads

`Rc<T>` is single-threaded. For immutable data that must be readable from worker threads
for the lifetime of the process (config, router, lookup tables), the correct pattern is:

1. Heap-allocate with `safe_malloc` to get a stable address.
2. Call `__move_to_raw` inside an `_Unsafe` block to transfer ownership to a raw pointer.
   No destructor will run on it — this is an intentional process-lifetime allocation.
   Document it.
3. Store the raw pointer as `const T* _Nonnull` in a plain (non-`_Owned`) shared context
   struct passed to each thread.
4. Cross the thread boundary only with plain copy types (`int` fds, indices). Never pass
   `_Borrow` or `_Owned` pointers across threads. `_Borrow → raw` is **forbidden in both
   `_Safe` and `_Unsafe`** — there is no cast from a `_Borrow` pointer to a raw pointer,
   anywhere.

```c
// In _Unsafe block inside the thread-pool setup function:
Config *_Owned cfgp = safe_malloc(config);   // stable heap address; takes ownership
Router *_Owned rtp  = safe_malloc(router);

// Intentional process-lifetime allocation — no paired free.
static struct ServerCtx ctx;
ctx.config = (const Config* _Nonnull)__move_to_raw(cfgp);
ctx.router = (const Router* _Nonnull)__move_to_raw(rtp);

// Worker threads reconstruct a _Borrow from the raw pointer:
_Safe void worker(int fd, void* _Nonnull ctx_raw) {
    struct ServerCtx* ctx = _Unsafe((struct ServerCtx*)ctx_raw);
    const Config* _Borrow cfg = _Unsafe(&_Const *(ctx->config));
    ...
}
```

The explicit `(const T* _Nonnull)` cast is required when assigning into a `_Nonnull`-typed
field, because `__move_to_raw` returns a nullable raw pointer and the field's declared type
enforces nonnull. The `ServerCtx` struct itself must be `static` (or heap-allocated) so its
address remains valid after the setup function returns.

---

## 4. Designing the `_Safe` / `_Unsafe` boundary

### Pick the boundary at design time, not during debugging

Before writing code, list which functions will be `_Safe` and which `_Unsafe`. Common
library structure:

```
Public API (.hbs)
  ├── _Safe for 90% of the surface
  └── _Unsafe for:
        - Constructors that take raw C strings (const char*)
        - Functions that interop with existing C libraries
        - Destructors that close system resources

Internal impl (.cbs)
  ├── _Safe where the model permits
  └── _Unsafe blocks (NOT whole functions) for:
        - pointer arithmetic
        - aliasing that the checker over-rejects
```

### When to use `_Unsafe` blocks inside `_Safe` functions

Totally legitimate when:

- You need `printf`/`puts` (formatted output is considered unsafe in BSC)
- You're doing pointer arithmetic over a known-safe buffer (`string->get(i)` returning a raw char pointer)
- You're casting between `_Owned` and raw for `__take_from_raw` / `__move_to_raw` (C FFI)

Not legitimate for:

- Skipping the borrow checker because it's "annoying"
- Bypassing a diagnostic you don't understand — figure out why it fires first

---

## 5. Anti-Patterns

### Anti-pattern: `_Unsafe` all the way down
Symptom: every function is `_Unsafe`. You're writing C with a .cbs extension.
Fix: mark the public API `_Safe` and push the `_Unsafe` parts into blocks inside each
function. The checker still helps you at the boundaries.

### Anti-pattern: returning raw `T*` instead of `T *_Borrow`
Symptom: a lookup returns `T*` (raw) instead of `T *_Borrow`, forcing callers into
`_Unsafe` just to use it.
Fix: if the function always succeeds, return `T *_Borrow`. If it can fail, return
`T *_Borrow _Nullable` — compile-time nullability tracking keeps callers `_Safe` while
representing the miss case.

### Anti-pattern: designing for C first, then "porting"
Symptom: you wrote a C-style API (`JSON_Value* parse(const char*)`) and are now
fighting the borrow checker.
Fix: start from the BSC types (`_Owned JSON_Value parse(String)`) and let the
signatures drive the implementation.

---



## 6. Quick Reference

| Question | Answer |
|---|---|
| Should I return `T` or `T *_Owned`? | `T` for small + non-null + local use; `*_Owned` for large, nullable, or container-stored |
| Should I take `T`, `T *_Owned`, or `T *_Borrow`? | `_Borrow` for read, mut-`_Borrow` for modify, value/`_Owned` for consume |
| Should this be `_Safe` or `_Unsafe`? | `_Safe` unless you can't — then wrap `_Unsafe` blocks inside |

| Can I share ownership? | Use `Rc<T>` from stdlib-advanced; don't invent sharing schemes |

Before you write a single line of BSC: sketch the types first, annotate who owns
what, decide where the `_Unsafe` seams go. The code almost writes itself after that.
