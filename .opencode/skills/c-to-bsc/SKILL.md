---
name: c-to-bsc
description: "BiSheng C translation from C. When you need to translate/convert/port C code (.c/.h) to BiSheng C (.cbs/.hbs), add ownership annotations, or migrate a C project to BSC, you MUST use this Skill first."
---

# C to BiSheng C Translation Skill

## MANDATORY: Read This Before Any C-to-BSC Translation

If you are translating C code to BSC, you MUST follow this process. **The fundamental rule is: keep the file structure as-is, only ADD ownership annotations.** Do NOT rename `.c`/`.h` → `.cbs`/`.hbs`; do NOT strip platform-specific code. Compile with `clang -x bsc file.c`. A translation is about analyzing every pointer and adding BSC annotations — not about restructuring the codebase.

## TOP RULE: Safe by Default — `_Unsafe` is Compiler-Driven, Not Author-Driven

**Mark every function `_Safe`. Write every body as if it were `_Safe`. Add `_Unsafe` ONLY where the compiler reports an error you cannot fix any other way.**

You do not get to predict which lines "feel unsafe" and pre-wrap them. The borrow checker and safe-zone checker are the source of truth — if a line compiles in `_Safe`, it stays in `_Safe`. The workflow is:

1. Annotate the function signature `_Safe`.
2. Translate the body as plain BSC — no `_Unsafe` anywhere.
3. Compile. For each diagnostic the compiler reports:
   - First try to **fix it without `_Unsafe`** (convert `&` to `&_Const`/`&_Mut`, replace raw `malloc`/`free` with `safe_malloc`/`safe_free`, add missing `_Owned`/`_Borrow`/`_Nullable`, restructure the line).
   - Only if no `_Safe`-zone form exists (variadic `printf`, raw pointer arithmetic on a non-container buffer, FFI cast, raw subscripting through `T*`, etc.), wrap **just the offending statement** in `_Unsafe stmt;` (no braces) or `_Unsafe { ... }` (smallest block).
4. Re-compile. Repeat until clean. Do not pre-emptively widen any `_Unsafe` block "in case" — leave it tight; the compiler will tell you if you need more.

**Forbidden shortcuts:**

- ❌ Marking a whole function `_Unsafe` because one line inside needed an escape.
- ❌ Wrapping a multi-line block in `_Unsafe { ... }` when only one statement triggered the error.
- ❌ Adding `_Unsafe` "defensively" before compiling — write the safe form first, let the compiler decide.
- ❌ Treating a borrow-checker error as a reason to escape to `_Unsafe`. Borrow errors mean the ownership annotations are wrong; fix the annotations, do not bypass the checker.

**`_Unsafe` is contagious — `_Unsafe` on a function forces every caller into `_Unsafe` context.** Keeping the function `_Safe` and the escape limited to one line keeps the cost local. A function that contains an `_Unsafe { ... }` block is **still `_Safe`** — that is the whole point of the block.

The rest of this Skill expands on what to write inside `_Safe` and how to fix specific diagnostics. When a section says "use `_Unsafe` for X", it means: the compiler will reject the safe form for X — wrap the minimal failing line, not the surrounding logic.

## TOP RULE 2: Every Pointer in a Function Signature MUST Be Annotated

**Every pointer that appears in a function declaration, function definition, or parameter list MUST carry `_Owned`, `_Borrow`, or be deliberately raw with a structural reason.** A bare `T *` in a signature is NEVER acceptable as a "default" or "TODO" — it is a positive claim that the pointer is raw (interior pointer, arithmetic cursor, output double-pointer, function pointer, opaque void* in generic contexts, or inside an `_Unsafe`-only API).

This applies to:

- Every parameter: `f(T *p)` is wrong unless `p` is genuinely raw. Use `f(T *_Owned p)`, `f(T *_Borrow p)`, or `f(const T *_Borrow p)`.
- Every return type: `T *f(...)` is wrong unless the returned pointer is genuinely raw. Use `T *_Owned f(...)` (caller frees) or `T *_Borrow f(...)` (caller does not free; lifetime tied to an input).
- Both the declaration in the `.h`/`.hbs` AND the definition in the `.c`/`.cbs` — they must match.

**Default decision tree (apply to every pointer in every signature):**

1. Does the function free the pointer, transfer it onward, or store it in a long-lived owner? → `T *_Owned`.
2. Does the function read through it without mutating? → `const T *_Borrow`.
3. Does the function mutate through it but does not free? → `T *_Borrow`.
4. Is it an output double-pointer (`T **out`), function pointer, opaque cast intermediary, or a cursor for arithmetic? → raw `T *` is correct, with a one-line comment explaining why.

If none of (1)–(4) clearly applies, you have not finished analyzing the function. **Do not leave it raw as a "good enough" placeholder** — the missing annotation will silently pass C, fail under BSC's borrow checker at the call site, or worse, type-check but encode the wrong ownership model.

```c
// WRONG — bare T*; reader cannot tell if caller frees, callee frees, or both
Node *find(Container *c, int id);

// CORRECT — read-only lookup, returns reference into c
const Node *_Borrow find(const Container *_Borrow c, int id);

// CORRECT — factory: callee allocates, caller frees
Node *_Owned create(int id);

// CORRECT — consumer: callee takes ownership of item
void enqueue(Container *_Borrow c, Node *_Owned item);
```

**Header / source consistency.** The annotation must appear in the declaration too, not just the definition. A `.h` that exposes `Node *find(Container *c, int id)` while the `.c` defines `const Node *_Borrow find(const Container *_Borrow c, int id)` will produce mismatched-type diagnostics or silently let callers misuse the API.

**Self-check at translation time:** before moving to the next function, scan its signature line-by-line and confirm every `*` has a qualifier (`_Owned` / `_Borrow`) or a deliberate raw-with-reason justification. If you cannot decide, that is a signal to read the function body and its callers — not a signal to leave it bare.

**MINIMUM BAR**: A valid BSC translation of any non-trivial C project MUST contain `_Owned` annotations. Any C code that calls `malloc`/`free` has ownership semantics that MUST be expressed in BSC. If your translation has zero `_Owned` annotations, it is incomplete — go back to Step 3 and re-analyze. "The code is too complex for ownership" is NOT a valid reason to skip annotations; use `_Unsafe` blocks for the complex parts and annotate what you can.

## CRITICAL RULE: `_Owned` Is Non-Nullable By Default

`_Owned` pointers CANNOT be `NULL` unless you add `_Nullable`:

```c
// WRONG — compiler error: _Owned pointer cannot be null
cJSON *_Owned item = NULL;

// CORRECT — add _Nullable when the pointer may be NULL
cJSON *_Owned _Nullable item = nullptr;

// CORRECT — initialized immediately, no _Nullable needed
cJSON *_Owned item = safe_malloc<cJSON>((cJSON){0});
```

**Rule**: If an `_Owned` variable is ever assigned `NULL`/`nullptr`, initialized to `NULL`, or checked against `NULL`, it MUST be declared `T *_Owned _Nullable`. Use `nullptr` instead of `NULL` in BSC code.

## Translation Process

### Step 1: Change as Little as Possible (Fundamental Rule)

**Change the C codebase as little as you can.** Only ADD BSC ownership annotations. Change anything else only if the compiler genuinely requires it.

- Keep `.c`/`.h` file extensions; compile with `clang -x bsc file.c` (or `-xbsc`, both work at driver and `-cc1`).
- Platform-specific code (`#ifdef _WIN32`, `__declspec(...)`, `__cdecl`, `__stdcall`, `extern "C"` guards, visibility macros) generally works as-is. BSC is clang-based; these either apply to the target platform or are silently ignored. Only touch them if a specific construct triggers a compile error.
- `#include` paths stay as-is (still `.h`). The one new include you typically need: `#include "bishengc_safety.hbs"` wherever you use `safe_malloc`/`safe_free`.

Why: incremental migration lets the project build as plain C AND as BSC from the same sources; keeps review diffs focused on ownership; preserves cross-platform code if you need it later.

### Step 2: Default Functions to `_Safe`; Add `_Unsafe` Only When the Compiler Demands It

This step is the application of the **TOP RULE** at the top of this Skill. Read it again before continuing.

When you add or modify a function signature, mark it `_Safe`. Write the body as if it were fully `_Safe`. Compile. The compiler — not your intuition — tells you which lines need to escape to `_Unsafe`. For operations the safety checker genuinely cannot model — variadic calls like `printf`, raw pointer arithmetic, FFI, pointer conversions requiring casts, raw subscripting through `T*` — wrap ONLY the offending statement in an `_Unsafe stmt;` (single line, no braces) or `_Unsafe { ... }` block.

**`_Unsafe` is contagious.** An `_Unsafe` function forces every caller to also be in `_Unsafe` context, and the cost spreads through the codebase. Keeping the interface `_Safe` lets the borrow checker help callers, even when the implementation has some escape hatches inside.

```c
// GOOD — interface is _Safe, only the one unsafe operation is wrapped
_Safe void log_value(const int *_Borrow v) {
    _Unsafe { printf("%d\n", *v); }     // printf is variadic, needs _Unsafe
}

// BAD — whole function is _Unsafe just for one line
_Unsafe void log_value(const int *_Borrow v) {
    printf("%d\n", *v);                  // forces every caller to be _Unsafe too
}
```

**A function with an `_Unsafe { ... }` block inside is STILL `_Safe`.** The
two are not in conflict; that is the whole point of `_Unsafe` blocks. Do not
demote the function to `_Unsafe` just because its body needs one escape.

**`_Unsafe` blocks must be minimal.** Wrap exactly the statements that require
the escape, and nothing else. Re-check every line you put inside `_Unsafe`:
if it would compile fine in `_Safe`, move it out.

```c
// BAD — bloated _Unsafe block, only the assignment needs the escape
_Safe void f(uint8_t *buf, size_t i, uint8_t v) {
    _Unsafe {
        if (i >= cap) return;       // _Safe-able
        validate(v);                // _Safe-able
        buf[i] = v;                 // raw subscript — needs _Unsafe
        log("wrote byte");          // _Safe-able
    }
}

// GOOD — only the raw-subscript line is _Unsafe
_Safe void f(uint8_t *buf, size_t i, uint8_t v) {
    if (i >= cap) return;
    validate(v);
    _Unsafe buf[i] = v;             // single-statement _Unsafe — no braces needed
    log("wrote byte");
}
```

**Self-check before submitting BSC code with any `_Unsafe`:**
1. Is the enclosing function annotated `_Safe`? If not, would it pass the
   borrow checker as `_Safe`? If yes, change it.
2. Is every line inside `_Unsafe { ... }` actually requiring the escape? If
   not, move the safe lines out — a one-line `_Unsafe stmt;` (no braces) is
   often the right shape.

### Step 2.5: Default Pointer Parameters to `_Borrow`, Not `_Owned`

A function that **reads or mutates** a value through a pointer but does **not
take ownership** must use `_Borrow`, never `_Owned`. `_Owned` means the
function consumes the value — the caller's variable becomes dead after the
call. Most C functions don't do that; they just need to look at or mutate
something that lives elsewhere.

**Rule of thumb when picking a parameter qualifier:**

- The function frees the pointer, transfers it onward, or stores it in a
  long-lived owner → `T *_Owned`. (Caller's variable is dead after the call.)
- The function only reads through the pointer → `const T *_Borrow`.
- The function mutates *through* the pointer but does not free it → `T *_Borrow`.
- The function does pointer arithmetic / iteration → raw `T *` (no borrow). This is the ONLY case where a bare `T *` parameter is correct; all other pointer parameters MUST be `_Owned` or `_Borrow` (see TOP RULE 2).

```c
// BAD — _Owned forces the caller to give up ownership for a function
// that just reads. Caller cannot use cfg afterwards.
_Safe int get_port(const Config *_Owned cfg) { return cfg->port; }

// GOOD — borrow; caller keeps cfg
_Safe int get_port(const Config *_Borrow cfg) { return cfg->port; }

// BAD — _Owned for a setter that doesn't consume
_Safe void set_port(Config *_Owned cfg, int p) { cfg->port = p; }

// GOOD — mutable borrow; caller keeps cfg
_Safe void set_port(Config *_Borrow cfg, int p) { cfg->port = p; }
```

**Self-check before annotating a function signature:** for each pointer
parameter, ask "does the caller's variable become dead after this call?" If
no, it is `_Borrow`, not `_Owned`. Defaulting to `_Owned` everywhere makes
every call site a one-shot move — that is wrong, and it forces the caller
into ugly re-allocation patterns that aren't in the original C.

See `/bsc-safe-zone` for the full list of `_Safe`-zone restrictions and legitimate uses of `_Unsafe` blocks.

### Step 3: Analyze Ownership (THE CRITICAL STEP)

For every pointer in the codebase, classify it into one of three categories:

#### Category A: Owned pointers → annotate `*_Owned`

A pointer OWNS data if:
- It was assigned from `malloc`/`calloc`/`realloc`/`strdup`
- The code calls `free()` on it (or is responsible for freeing)
- It is returned from a function that creates/allocates (caller must free)
- A struct field that gets freed in the struct's cleanup/delete function

#### Category B: Borrowed pointers → annotate `*_Borrow`

A pointer BORROWS data if:
- It points into someone else's allocation (e.g. back-pointers, parent pointers)
- It is a function parameter for read-only or mutate-without-freeing access
- It is used for traversal without taking ownership
- It aliases another owned pointer temporarily

#### Category C: Raw pointers → leave as plain `T *` (NO annotation)

Some **specific** pointers CANNOT or SHOULD NOT be annotated. Leave these as raw `T *`.

**WARNING**: Category C is for individual pointers with specific reasons, NOT for entire codebases. If you find yourself putting most pointers in Category C, you are doing it wrong — go back and re-analyze. Most pointers that touch `malloc`/`free` belong in Category A.

**Category C does NOT apply to function signature pointers** (parameters or returns) except for the four narrow cases listed in TOP RULE 2: output double-pointers, function pointers, opaque cast intermediaries, and arithmetic cursors. If a signature pointer doesn't fit one of those four, it MUST be `_Owned` or `_Borrow`. Local interior/cursor variables are the typical Category C — not API surface.

- **Pointer arithmetic / interior pointers**: pointers into the middle of a buffer
  ```c
  unsigned char *cursor = buffer + offset;  // points inside buffer, not an allocation
  char *end = str + len;                    // pointer arithmetic result
  ```
- **Output parameters / double pointers**: `T **out` used to return values
  ```c
  void get_result(int **out) { *out = &some_local; }
  ```
- **Iterators / scanning pointers**: temporary pointers that walk through data
  ```c
  const char *p = input;
  while (*p != '\0') { p++; }  // just scanning, no ownership
  ```
- **Conditional ownership**: when the same field is sometimes owned, sometimes not (flag-based)
  ```c
  // If ownership depends on a runtime flag, you may need to keep it raw
  // and handle it with _Unsafe blocks
  struct Item {
      int flags;
      void *data;  // owned if !(flags & IS_REFERENCE), borrowed otherwise
  };
  ```
- **Opaque / void* in generic contexts**: casting intermediaries
  ```c
  void *tmp = (void *)ptr;  // intermediate cast, not a real allocation
  ```
- **Stack pointers inside `_Unsafe` blocks**: `&local_var` kept as raw `T *` is
  only valid in `_Unsafe`. In `_Safe` zones, `&local_var` **is** a borrow and
  MUST be written as `&_Const local_var` / `&_Mut local_var`, with the receiver
  typed `const T *_Borrow` / `T *_Borrow`. See Step 4.5.
  ```c
  _Unsafe {
      int x = 42;
      int *p = &x;       // raw pointer — OK only in _Unsafe
  }
  _Safe void f(void) {
      int x = 42;
      int *_Borrow p = &_Mut x;   // NOT a raw `int *p = &x` — that's forbidden
  }
  ```
- **Function pointers**: not data pointers, leave as-is
- **Pointers inside `_Unsafe` blocks**: when you explicitly opt out of safety checks

### Step 3.5: Convert Allocations to safe_malloc / safe_free (PREFERRED)

BSC provides `safe_malloc<T>(val)` and `safe_free()` from `bishengc_safety.hbs`. **Prefer these over raw malloc/free** — they return `T *_Owned` directly with no cast needed and work in `_Safe` context.

```c
#include "bishengc_safety.hbs"
```

**Signatures (from BSC standard library):**
- `_Safe T *_Owned safe_malloc<T>(T t)` — allocates, initializes to `t`, returns `T *_Owned`
- `_Safe void safe_free(void *_Nullable _Owned p)` — frees a nullable owned pointer

| C (original) | BSC (preferred: safe_malloc) | BSC (raw malloc + __take_from_raw) |
|---|---|---|
| `malloc(sizeof(int))` | `safe_malloc<int>(0)` | `__take_from_raw((int *)malloc(sizeof(int)))` |
| `free(p)` | `safe_free((void *_Owned)p)` | `_Unsafe { free(__move_to_raw(p)); }` |
| `p = NULL; free(p);` | `safe_free((void *_Nullable _Owned)p)` | N/A — use safe_free for nullable |

### CRITICAL: `(T *_Owned)` and `(T *)` casts between raw and `_Owned` are forbidden everywhere

**The compiler rejects direct C casts between `_Owned` and raw pointers in every context — including `_Unsafe`.** You MUST use the builtins `__take_from_raw` (raw → `_Owned`) and `__move_to_raw` (`_Owned` → raw).

```c
// WRONG — rejected by compiler with "cannot cast between _Owned and raw pointer;
//         use __move_to_raw or __take_from_raw for ownership transfer"
int *_Owned p = (int *_Owned)malloc(sizeof(int));   // ERROR
int *raw = (int *)p;                                  // ERROR (p is _Owned)

// CORRECT
int *_Owned p = __take_from_raw((int *)malloc(sizeof(int)));
int *raw = __move_to_raw(p);                          // transfers ownership out; p unusable after
```

Verified against `clang/test/BSC/Negative/Ownership/RuleCheck/owned_raw_cast_disallowed/owned_raw_cast_disallowed.cbs` and `clang/test/BSC/Positive/Ownership/owned_raw_transfer_builtins/owned_raw_transfer_builtins.cbs`.

**Exception — owned-to-owned-via-void is allowed**: `(void *_Owned)p` when `p` is `T *_Owned` is fine (both sides are `_Owned`; no raw involved). This is why `safe_free((void *_Owned)p)` stays correct.

**Borrow-then-cast pattern** — when you need to pass a raw pointer to a C API without transferring ownership:
```c
int *_Owned p = safe_malloc<int>(42);
foo((int *)&_Mut *p);       // borrow through *p, then cast borrow to raw. p keeps ownership.
safe_free((void *_Owned)p);
```

### Null-check before `__take_from_raw`

When nullability-check is enabled (default under `-nullability-check=safeonly`), `__take_from_raw(raw)` errors if `raw` has not been null-checked. Either null-check first, or declare the result as `_Nullable`:

```c
// Option A: null-check first, assign to non-nullable _Owned
int *raw = (int *)malloc(sizeof(int));
if (raw != nullptr) {
    int *_Owned p = __take_from_raw(raw);
    // use p
}

// Option B: accept nullability in the result type
int *_Owned _Nullable p = __take_from_raw((int *)malloc(sizeof(int)));
```

**When to use which:**
- `safe_malloc<T>(val)` — use when allocating a single value of known type. Returns `T *_Owned`, initializes to `val`. No cast or null-check needed. Works in `_Safe` context. **Covers almost all C `malloc(sizeof(T))` cases** — don't reach for the raw `malloc` fallback unless you need variable size.
- `safe_free(p)` — **preferred for all frees**. Parameter type is `void *_Nullable _Owned`. Cast based on the SOURCE pointer's type:
  - If `p` is `T *_Owned` (non-nullable): `safe_free((void *_Owned)p)`. The `_Nullable` widening at the call is implicit.
  - If `p` is `T *_Nullable _Owned`: `safe_free((void *_Nullable _Owned)p)`. The cast must preserve `_Nullable` — casting to non-nullable `(void *_Owned)` would narrow and error.
- Raw `malloc` + `__take_from_raw` — use only when `safe_malloc<T>` doesn't fit: variable-size allocations (`malloc(n * sizeof(T))`), `calloc`, `realloc`, `strdup`, or buffer allocations. Requires `_Unsafe` context.
- Raw `free()` — takes raw `void *`. Call with `__move_to_raw(owned_ptr)` to transfer the `_Owned` pointer out. Use only in `_Unsafe` blocks. If the pointer might be NULL, use `safe_free` instead.

#### Converting malloc → safe_malloc
```c
// BEFORE (C)
Node *n = (Node*)malloc(sizeof(Node));
memset(n, 0, sizeof(Node));

// AFTER (BSC) — preferred
Node *_Owned n = safe_malloc<Node>((Node){0});

// AFTER (BSC) — raw malloc + __take_from_raw (when safe_malloc doesn't fit)
_Unsafe {
    Node *raw = (Node *)malloc(sizeof(Node));
    if (raw == nullptr) { /* handle */ }
    memset(raw, 0, sizeof(Node));
    Node *_Owned n = __take_from_raw(raw);
}
```

#### Converting free → safe_free
```c
// BEFORE (C)
free(node->name);
free(node);

// AFTER (BSC) — safe_free accepts _Nullable _Owned, safe for possibly-NULL pointers
safe_free((void *_Owned)node->name);
safe_free((void *_Owned)node);

// If the pointer might be NULL (common in cleanup paths):
safe_free((void *_Nullable _Owned)node->name);  // safe even if name is NULL
```

#### Variable-size allocations — raw malloc + `__take_from_raw`
```c
// BEFORE (C)
char *buf = (char*)malloc(length + 1);
char *copy = strdup(original);

// AFTER (BSC) — raw malloc in _Unsafe, then __take_from_raw to get _Owned
_Unsafe {
    char *raw_buf = (char *)malloc(length + 1);
    if (raw_buf == nullptr) { /* handle */ }
    char *_Owned buf = __take_from_raw(raw_buf);

    char *raw_copy = strdup(original);
    if (raw_copy == nullptr) { /* handle */ }
    char *_Owned copy = __take_from_raw(raw_copy);
}
```

#### Realloc ownership semantics
`realloc` takes a raw `void *` and returns a raw `void *`. You must `__move_to_raw` the old owned buffer out, call `realloc`, then `__take_from_raw` the result. On failure (NULL return), C's `realloc` does NOT free the old buffer, but ownership has already been moved out — so you must recover the raw and re-wrap it:

```c
_Unsafe {
    // Move out the old _Owned buffer to a raw pointer
    char *raw_old = __move_to_raw(old_buf);

    char *raw_new = (char *)realloc(raw_old, newsize);
    if (raw_new == nullptr) {
        // realloc failed: raw_old is still valid per C semantics.
        // Re-wrap it into _Owned and free.
        char *_Owned recovered = __take_from_raw(raw_old);
        safe_free((void *_Owned)recovered);
        return;
    }
    // Success: raw_new is the moved/grown buffer. Wrap into _Owned.
    char *_Owned new_buf = __take_from_raw(raw_new);
    old_buf = new_buf;   // old_buf now owns the resized buffer
}
```

### Step 4: Annotate Pointers (Categories A and B only)

Only annotate pointers that clearly fall into Category A or B. When in doubt, leave as raw and add a `/* TODO: ownership unclear */` comment.

#### Struct fields

**Rule**: a struct's cleanup/delete function is the source of truth. If the cleanup frees `field` → mark `_Owned`. If not → leave the field as plain (raw) pointer or use a value type. **Do NOT use `_Borrow` as a struct field unless you have verified the design works** (see the limits below).

```c
// BEFORE (C)
typedef struct Token {
    char *name;
    char *value;
    int kind;
} Token;

// AFTER (BSC) — cleanup frees name and value; kind is plain data
typedef struct Token {
    char *_Owned name;             // freed by ~Token / cleanup → _Owned
    char *_Owned value;            // freed by ~Token / cleanup → _Owned
    int kind;                      // plain value
} Token;
```

#### `_Borrow` as a struct field — tightly restricted, don't use for back-references

Per user manual §3.2.6, struct `_Borrow` fields are subject to rules that rule out most C patterns:

1. A struct's `_Borrow` field **cannot borrow from the containing struct or its other fields** (rule 3). So `s.p = &_Const s.m` errors.
2. A struct containing a `_Borrow` field **cannot itself be borrowed** (rule 5). This cascades: once `T` has a `_Borrow` member, `T *_Borrow` is illegal.
3. Heap-allocated structs where `_Borrow` fields cross-reference other heap nodes — classic linked list / tree with parent pointers — **do not satisfy the borrow checker's lifetime model**. The target of the borrow could be freed independently.

**For back-references in linked lists, trees, graphs, observer patterns**: use raw pointers (no annotation) with access wrapped in `_Unsafe`. See §"Circular structures" below. The stdlib `LinkedList<T>` (`libcbs/src/list/list.hbs`) uses raw pointers for exactly this reason:

```c
// From stdlib — linked list nodes use raw pointers, NOT _Owned or _Borrow
struct _BSC_ListNode<T> {
    _BSC_ListNode<T>* next;    // raw
    _BSC_ListNode<T>* prev;    // raw
    T element;
};
```

The old "doubly-linked list with `_Owned next` and `_Borrow prev`" example that used to appear here was wrong — it neither compiles cleanly nor represents a valid BSC pattern. Use Pattern A from §"Circular structures" instead.

#### Function parameters — borrowing (when not taking ownership)
```c
// BEFORE (C)
int count_items(const Node *list) { ... }  // just reads, doesn't free

// AFTER (BSC)
int count_items(const Node *_Borrow list) { ... }
```

#### Function parameters — taking ownership (caller gives up ownership)
```c
// BEFORE (C)
void container_add(Container *c, Node *item) { ... c takes ownership of item ... }

// AFTER (BSC)
void container_add(Container *_Borrow c, Node *_Owned item) { ... }
```

#### Function returns — transferring ownership
```c
// BEFORE (C)
Node* create_node(void) {
    Node *n = malloc(sizeof(Node));
    return n;  // caller must free
}

// AFTER (BSC) — preferred: use safe_malloc
Node *_Owned create_node(void) {
    Node *_Owned n = safe_malloc<Node>((Node){0});
    return n;  // ownership transfers to caller
}

// AFTER (BSC) — if you must use raw malloc, wrap via __take_from_raw
Node *_Owned create_node_raw(void) {
    _Unsafe {
        Node *raw = (Node *)malloc(sizeof(Node));
        if (raw == nullptr) { /* handle */ }
        Node *_Owned n = __take_from_raw(raw);
        return n;
    }
}
```

#### Function returns — borrowing (returning pointer into existing data)
```c
// BEFORE (C)
Node* find_node(Container *c, int id) { ... return pointer into c's data ... }

// AFTER (BSC) — caller does NOT free the result
Node *_Borrow find_node(Container *_Borrow c, int id) { ... }
```

#### Const-correctness of getters — a translation decision with whole-codebase reach

C getters `T* get(Container* c, ...)` are typically read-only. The naive
translation is `T *_Borrow get(Container *_Borrow this, ...)` — but this is
**NOT const**: `Container *_Borrow` is a mut-borrow. Any caller from a
`const Container *_Borrow` context cannot call it.

This is subtle because C has no borrow checker — a C read-only getter and a C
mutating setter have identical pointer types. BSC needs you to pick at
translation time:

| Getter behavior in C | BSC signature |
|----------------------|---------------|
| Reads fields only; does not modify the container | `const T *_Borrow get(const Container *_Borrow this, ...)` |
| Returns a mut-borrow into container so caller can modify | `T *_Borrow get_mut(Container *_Borrow this, ...)` |
| Both needed | Provide **two** member functions (`get` + `get_mut`) |

**Forgetting const on read-only getters is a whole-codebase annoyance.** Once
you have even one function that needs `const Container *_Borrow`, every
non-const getter it wants to call becomes unreachable. Callers then work around
it by reaching through `_Public` fields directly, casting, or duplicating
helpers — all of which defeat the API.

**Checklist during translation:**
- Audit every C function whose name starts `get_`, `find_`, `has_`, `count_`,
  `is_`, `peek_`, or any verb describing read-only access.
- If the function body does not modify `this` or call mutating methods on it,
  change BOTH the `this` parameter AND the returned pointer to `const`:
  `const T *_Borrow method(const Container *_Borrow this, ...)`.
- If a C-style "getter" happens to lazy-initialize, sort, or cache on first
  call, it is **not** actually read-only — leave it non-const and document why.

#### Raw pointers — leave unannotated
```c
// These stay as plain pointers — no _Owned or _Borrow
unsigned char *cursor = buffer_at_offset(buf);      // interior pointer
const unsigned char *input_pointer = input + 1;     // pointer arithmetic
unsigned char *output_pointer = output;              // alias into owned buffer
char *after_end = nullptr;                           // used by strtod
```

### Step 4.5: Convert Address-of (`&`) to `&_Const` / `&_Mut`

In BiSheng C, `_Safe` zones **forbid plain `&`** — you must use `&_Const` (immutable borrow) or `&_Mut` (mutable borrow). See `bsc-borrowing` Skill §7 (Complete Example) and `bsc-safe-zone` Skill for full semantics.

#### When to convert

| Context | C `&` | BSC conversion |
|---------|-------|----------------|
| **`_Safe` zone** | `&x` | Must convert to `&_Const x` or `&_Mut x` |
| **`_Unsafe` zone** | `&x` | Can keep as `&x` (no conversion required) |
| **Function address** | `&func` | Keep as `&func` — exception: function addresses allowed in `_Safe` |

#### How to choose `&_Const` vs `&_Mut`

| Situation | Use | Reason |
|-----------|-----|--------|
| Target is `const T *_Borrow` or code **only reads** through the pointer | `&_Const x` | Immutable borrow — read-only, multiple allowed |
| Target is `T *_Borrow` and code **writes** through the pointer | `&_Mut x` | Mutable borrow — read/write, exactly one at a time |
| Passed to `void foo(const T *_Borrow p)` | `&_Const x` | Function expects read-only borrow |
| Passed to `void bar(T *_Borrow p)` | `&_Mut x` | Function may mutate through pointer |
| `const` variable / string literal | `&_Const` only | Cannot use `&_Mut` — not modifiable |
| Global variable (in `_Safe`) | `&_Const` only | `&_Mut` of globals forbidden in `_Safe` |

#### Examples

```c
// BEFORE (C)
void init(int *out) { *out = 0; }
void read_val(const int *p) { (void)*p; }
void example(void) {
    int x = 42;
    int *p = &x;
    init(&x);
    read_val(&x);
}

// AFTER (BSC) — in _Safe zone
_Safe void init(int *_Borrow out) { *out = 0; }
_Safe void read_val(const int *_Borrow p) { (void)*p; }
_Safe void example(void) {
    int x = 42;
    init(&_Mut x);                       // writes → &_Mut (no other live borrow)
    read_val(&_Const x);                 // read-only param → &_Const
    const int *_Borrow r = &_Const x;    // read-only → &_Const
    (void)*r;                            // use r before scope end
}
```

#### Borrow from `_Owned` pointer

```c
// BEFORE (C) — pointer to heap object
Node *node = malloc(...);
process(&node->value);

// AFTER (BSC)
Node *_Owned node = safe_malloc<Node>((Node){0});
process(&_Mut node->value);    // or &_Const if process only reads
```

From owned pointer itself (not a field): use `&_Const *ptr` or `&_Mut *ptr`:

```c
Node *_Owned node = create_node();
const Node *_Borrow ref = &_Const *node;   // borrow from owned
```

#### When NOT to convert (keep plain `&`)

- **Outside `_Safe`**: In `_Unsafe` blocks or non-`_Safe` functions, plain `&` is valid.
- **Function address**: `&printf`, `&my_callback` — allowed in `_Safe`.
- **Output/double-pointer patterns**: `int **out` receiving `&local_ptr` — often kept in `_Unsafe`; if in `_Safe`, the inner `&` would need `&_Mut`/`&_Const` per target type.

#### Common mistakes

```c
// WRONG — & forbidden in _Safe
_Safe void f(void) { int x = 1; int *p = &x; }

// CORRECT
_Safe void f(void) { int x = 1; int *_Borrow p = &_Mut x; }

// WRONG — &_Mut on const / literal
const int c = 1;
int *_Borrow mp = &_Mut c;           // error: not modifiable
char *_Borrow sp = &_Mut "hello";    // error: string literal immutable

// CORRECT
const int *_Borrow r = &_Const c;
const char *_Borrow sr = &_Const "hello";

// WRONG — &_Mut of global in _Safe
int global_x;
_Safe void f(void) { int *_Borrow p = &_Mut global_x; }  // error

// CORRECT
_Safe void f(void) { const int *_Borrow p = &_Const global_x; }
```

### Step 5: Handle Special Patterns

#### Conditional ownership (flag-based)
Some C code uses flags to decide whether to free:
```c
if (!(item->type & IS_REFERENCE)) {
    free(item->child);  // only free if we own it
}
```
Options:
1. Keep the field as raw `T *` and use `_Unsafe` blocks for free
2. Restructure into two separate types (owned vs reference variant)
3. Keep as `*_Owned` and use `_Unsafe` to skip free when flag is set

#### String literals vs allocated strings
```c
const char *literal = "hello";                                         // raw pointer — string literal, never freed
char *_Owned allocated;                                                // heap copy — must free
_Unsafe { allocated = __take_from_raw(strdup("hello")); }              // strdup returns raw; wrap via __take_from_raw
```

#### Deciding `struct` vs `_Owned struct` when porting

C has only one kind of aggregate. BSC has two:

- **`struct S`** — copy semantics (like a C struct). May still contain
  `*_Owned` fields, in which case `S` is automatically a move-semantic type
  (see `is_move_semantic<T>` in the user manual §2.3.2). No auto-destructor:
  every `*_Owned` field must be released explicitly on every code path.
- **`_Owned struct S`** — move semantics, whole-value tracked. May have a
  destructor `~S(S this) { ... }` that runs automatically at scope exit.
  Nested `_Owned struct` members are auto-destructed by the compiler; only
  raw `*_Owned` pointer members need explicit `safe_free` in `~S`.

**Promote C structs to `_Owned struct` when any of these hold:**

1. The struct "owns" heap-allocated resources (fields assigned from
   `malloc`/`calloc`/`strdup`, or `_Owned`-annotated fields).
2. The C code has a corresponding `free_S(S* s)` / `S_destroy(S* s)` / similar
   cleanup function that walks the struct's fields.
3. Instances are conceptually unique and shouldn't be bit-copied.

```c
// BEFORE (C)
typedef struct Buffer {
    char *data;
    size_t len;
} Buffer;
void buffer_free(Buffer *b) { free(b->data); }

// AFTER (BSC) — promoted to _Owned struct
_Owned struct Buffer {
_Public:
    char *_Owned data;
    size_t len;
    ~Buffer(Buffer this) {
        safe_free((void *_Owned)this.data);
    }
};
// The caller no longer calls buffer_free — the destructor fires automatically.
```

**Keep as plain `struct` (don't promote) when:**

1. The struct is POD-like — only value fields, no heap pointers, no cleanup.
2. The C code passes instances by value and expects cheap copies.
3. You genuinely need copy semantics (e.g. `Point`, `Rect`, `Color`).

```c
// Stays a plain struct — no ownership, just a pair of numbers
struct Point { int x; int y; };
```

**A plain `struct` with a `*_Owned` field is legal but dangerous.** The
compiler flags it as move-semantic but generates NO auto-destructor — you
must release the owned fields manually on every drop path. If you catch
yourself doing that, promote to `_Owned struct`.

##### Destructor ordering and "partial move" rule

From the user manual §3.4.1.2: at scope end, an `_Owned struct` (recursively
including members) must be in one of exactly two states:

- **`_Owned`**: nothing moved, destructor will fire automatically.
- **`moved`**: the whole struct was explicitly moved out as a unit.

Any in-between state (e.g. one field moved, the rest live) is rejected with:
`partially moved _Owned struct: X at scope end, Y moved`. **When porting C
code that frees fields one at a time and then forgets the parent**, this rule
will catch you. Fix by either moving the whole struct out or leaving it
untouched and letting the destructor run.

#### Bit-copying structs with `memcpy` / `memmove` / `memset`

C routinely uses `memcpy(dst, src, sizeof(T))` to duplicate a struct. This is
a **type-erased bit copy**: the compiler's ownership tracking does not see
it. If `T` contains `*_Owned` or `_Owned struct` members, a raw `memcpy`
creates two owners of the same heap memory — leading to double-free when
both destructors fire.

```c
// UNSAFE — the _Owned pointer inside r is now aliased by val.
// When r goes out of scope, ~Resource(r) frees r.s.
// When the caller uses val and drops it, the same buffer is freed again.
Resource r = { .s = safe_malloc<char>(100) };
memcpy(val, (const void *)&r, sizeof(Resource));
// BUG: double free pending.
```

**Sanctioned pattern (user manual §6.1.4):** pair the `memcpy` with
`forget<T>(r)`. `forget` takes ownership of `r` and drops it on the floor
without calling the destructor. Ownership has transferred to `val` via the
bit copy; `r`'s destructor is correctly suppressed.

```c
#include "bishengc_safety.hbs"

_Unsafe void ffi_out(char *val) {
    Resource r = { .s = safe_malloc<char>(100) };
    memcpy(val, (const void *)&r, sizeof(Resource));
    forget<Resource>(r);   // suppress ~Resource; val now owns r.s
}
```

**Rules for porting `memcpy`/`memmove` over owning types:**

- If both source and destination will be live after the copy → you've
  duplicated ownership. Use `.clone()` on the source (if available) instead
  of `memcpy`.
- If the source is "moved out" by the copy (FFI boundary, manual move) →
  wrap the raw copy in `_Unsafe` and call `forget<T>(src)` immediately
  after, so the compiler doesn't fire the source's destructor.
- For `memset(s, 0, sizeof(T))` on a struct containing `*_Owned` fields:
  same problem inverted — it overwrites the owned pointer with NULL without
  freeing. Use a value-replace via assignment, or wrap in `_Unsafe` after
  first freeing the fields by hand.

Treat `mem*` on owning types as a **boundary operation**: `_Unsafe`, paired
with an explicit ownership statement (`forget`, prior `safe_free`, or a
matching copy).

#### Circular structures: back-references, parents, graphs, observers

BSC's single-owner `_Owned` model cannot directly express cycles. There are
**two idiomatic patterns** — pick based on whether nodes are truly shared:

| Situation | Pattern |
|-----------|---------|
| **Container uniquely owns its nodes** (typical list, tree, queue) | Raw `Node*` fields + `_Unsafe` manipulation + custom `~Container` destructor that walks and frees. No `Rc`/`Weak`. |
| **Nodes are shared between multiple owners**, OR **back-references need to survive drop of some owners**, OR **you want automatic destruction without writing a walk-and-free destructor** | `Rc<T>` for owning refs, `Weak<T>` for back-refs, `RefCell<T>` for mutation through shared refs. Pattern from user manual §6.3. |

The stdlib's own `LinkedList<T>` (libcbs `list.hbs`) uses the **first
pattern** — raw pointers in `_Unsafe` — precisely because a list uniquely
owns its chain. Don't reach for `Rc`/`Weak` if a custom destructor would do.

##### Pattern A — raw pointers + `_Unsafe` + custom destructor

Use when the container is the sole owner. `_Owned struct` wraps the root
pointers; the destructor walks and frees nodes. Public API stays `_Safe`.

```c
typedef struct SNode SNode;
struct SNode {
    int           value;
    struct SNode *next;   // raw; owned transitively by the list
};

_Owned struct SList {
_Public:
    struct SNode *head;
    size_t        count;

    ~SList(SList this) {
        _Unsafe {
            struct SNode *n = this.head;
            while (n != NULL) {
                struct SNode *nxt = n->next;
                free((void *)n);
                n = nxt;
            }
        }
    }
};
// push/pop/etc: _Safe bodies with tiny _Unsafe blocks around the malloc/free.
```

Forward-declare with `typedef struct X X;` — BSC requires the `struct`
tag on raw references unless a typedef exists.

##### Pattern B — `Rc<T>` / `Weak<T>` / `RefCell<T>`

Use when nodes are shared, or when back-refs must remain usable after some
owners drop. The manual's §6.3.6.4 tree example is the canonical case.

```c
// CORRECT: RefCell<Option<Weak<Node>>> — the Option::None state is how
// you construct a node BEFORE any parent exists. Weak<T>::new requires an
// existing Rc<T> to borrow, so there is no "default Weak" — Option fills
// that gap.
_Owned struct Node {
_Public:
    int value;
    RefCell<Option<Weak<Node>>>    parent;    // back-ref; None until wired
    RefCell<Vec<Rc<Node>>>         children;  // owning forward refs
};

_Safe Node Node::new(int v) {
    Node n = {
        .value    = v,
        .parent   = RefCell<Option<Weak<Node>>>::new(Option<Weak<Node>>::None()),
        .children = RefCell<Vec<Rc<Node>>>::new(Vec<Rc<Node>>::new()),
    };
    return n;
}

// Wiring: parent owns children via Rc::clone, children back-ref via Weak.
Rc<Node> root = Rc<Node>::new(Node::new(10));
Rc<Node> leaf = Rc<Node>::new(Node::new(5));
root.deref()->children.borrow_mut().deref()->push(leaf.clone());
*leaf.deref()->parent.borrow_mut().deref() =
    Option<Weak<Node>>::Some(Weak<Node>::new(&_Const root));
```

**Why `Option<Weak<T>>` and not just `Weak<T>`:** `Weak<T>::new(const Rc<T>*)`
is the ONLY constructor — there is no `Weak::none()` / default. To create a
`Node` before its parent exists (required by the leaf-first construction
order), the `parent` field must start in a legitimately-empty state. `Option`
provides that. The user manual's example elides the `parent` initializer
entirely, which would leave it in an indeterminate state — don't copy that.

**Why both `Rc` and `Weak`:** `Rc<T>` alone creates an immortal cycle
(every node's refcount stays ≥1 because its neighbor refs it). `Weak<T>`
doesn't contribute to the refcount, so when the owning chain drops its
last `Rc`, destructors fire normally. User manual §6.3.6 shows the leak
without `Weak` and its resolution with it.

**Why `RefCell`:** BSC's borrow checker forbids mutating through a
`const T *_Borrow` (which is what `Rc::deref` returns). If your cyclic
structure needs mutation (adding children, rewiring edges), wrap the
mutable-from-shared fields in `RefCell`. `borrow_mut()` gives a `RefMut<T>`
whose `deref()` is `T *_Borrow`. Violations abort at **runtime**, not compile
time.

##### Translation table

| C pattern | Recommended BSC translation |
|-----------|-----------------------------|
| Singly- or doubly-linked list whose list struct is the sole owner | Pattern A (raw + `_Unsafe` + custom destructor). Model on stdlib `LinkedList<T>`. |
| Tree walked from a single root, no back-references | Plain `_Owned struct` with `Vec<Node>` children. Recursion handles traversal; auto-destructors handle cleanup. No `Rc` needed. |
| Tree with parent back-references (caller walks leaf→root sometimes) | Pattern B with `RefCell<Option<Weak<Node>>>` parent, `RefCell<Vec<Rc<Node>>>` children. |
| Graph where a node may be reachable from multiple predecessors | Pattern B: edges own targets via `Rc<Node>`; if edges should be non-owning views, use `Weak<Node>` and upgrade on access. |
| Observer / listener pattern | Subject → observers via `Vec<Weak<Observer>>`; observer → subject via `RefCell<Option<Weak<Subject>>>`. |

**When NOT to reach for `Rc`/`Weak`:** if the container uniquely owns its
nodes and nobody else references them, use Pattern A. It's shorter, cheaper,
and proven — the stdlib follows it. Rc+Weak overhead only pays off when
sharing or leaf-up traversal is genuinely required.

See `bsc-stdlib-advanced` Skill for the full `Rc`/`Weak`/`RefCell`/`Cell` API.

#### C Object-Oriented patterns (vtable / METHOD macro)

Many C codebases implement OO-style polymorphism through structs of function pointers
("vtables"). A typical pattern (used by strongSwan's `METHOD` macro and similar helpers):

```c
/* C original */
typedef struct public_t public_t;
struct public_t { void (*write)(public_t *this, uint8_t v); };

typedef struct { public_t public; uint8_t *buf; size_t used; } private_t;

METHOD(public_t, write, void, private_t *this, uint8_t v)
{
    this->buf[this->used++] = v;
}
// Expands to: a body taking private_t*, plus a raw fn-ptr alias _write for the vtable.
```

**Why `METHOD` (and any `transparent_union` approach) cannot be ported to BSC:**

1. BSC mixed-mode declarations (`_Safe` overload of an `_Unsafe` function) require the
   parameter list to be the same types, only adding `_Owned`/`_Borrow` qualifiers to raw
   pointer params. `transparent_union` gives a *union* as the first parameter — not a raw
   pointer — so mixed-mode cannot apply.

2. **Function pointer casts across different struct pointer param types are forbidden in
   heterogeneous `_Safe`/`_Unsafe` declarations (the mixed-mode mechanism).** Use the
   body+trampoline pattern regardless — the trampoline gives you correctly-typed parameters
   without any fn-ptr cast.

**The hand-written body + trampoline pattern:**

Write two `_Safe` functions per vtable slot:

- **Body** — takes `private_t *_Borrow _Nonnull this`, holds all the logic.
- **Trampoline** — takes `public_t *_Borrow _Nonnull self` (matches the vtable slot type),
  casts to `private_t *_Borrow` via the void-borrow two-step, then calls the body.

```c
/* Body — _Safe, operates on private_t */
_Safe static void write_uint8(private_t *_Borrow _Nonnull this, uint8_t value)
{
    _Unsafe this->buf[this->used] = value;   // raw field subscript
    this->used += 1;
}

/* Trampoline — _Safe, parameter type matches vtable slot */
_Safe static void _write_uint8(public_t *_Borrow _Nonnull self, uint8_t value)
{
    private_t *_Borrow _Nonnull this = _Unsafe(
        (private_t *_Borrow _Nonnull)(void *_Borrow _Nonnull)self);
    write_uint8(this, value);
}
```

The cast chain `(private_t *_Borrow _Nonnull)(void *_Borrow _Nonnull)self`:
- `public_t *_Borrow → void *_Borrow` — implicit, allowed in `_Safe`.
- `void *_Borrow → private_t *_Borrow` — explicit cast, wrapped in `_Unsafe(expr)`.
- Direct `public_t *_Borrow → private_t *_Borrow` is **forbidden** (different pointed-to types).

**Vtable initialisation** assigns trampolines (whose param types match the vtable slot),
never bodies:

```c
public_t *_Owned _Nullable create(void)
{
    private_t *this;
    INIT(this,
        .public = {
            .write_uint8 = _write_uint8,   // trampoline — NOT write_uint8
            .destroy     = _destroy,
        },
    );
    return __take_from_raw(&this->public);
}
```

**Destroy body caveat.** `this` is a `_Borrow`, so BSC does not model freeing it — wrap
all frees in `_Unsafe`:

```c
_Safe static void destroy(private_t *_Borrow _Nonnull this)
{
    _Unsafe {
        free(this->buf);
        free((void *)(void *_Borrow _Nonnull)this);   // two-step to strip _Borrow
    }
}
```

**WARNING — this is a silent use-after-free hazard.** The compiler's ownership model does NOT see the `free(this)` through a `_Borrow`. Any caller that continues to hold or use an `_Owned` or `_Borrow` of the same object after `destroy` returns will get a runtime use-after-free that the checker did not catch. This escape is only acceptable because vtable protocols demand the trampoline parameter be `public_t *_Borrow` — the C interface is fixed and the `_Owned` alternative would break the vtable contract. In ordinary (non-vtable) code, always take `_Owned this` in a destroy function so the compiler models the free.

**Checklist for porting a C vtable struct:**
- [ ] Do NOT use `METHOD` macro or `transparent_union` tricks
- [ ] Write one `_Safe` body per slot — takes `private_t *_Borrow _Nonnull`
- [ ] Write one `_Safe` trampoline per slot — takes `public_t *_Borrow _Nonnull`
- [ ] Cast inside trampoline: `_Unsafe((private_t *_Borrow _Nonnull)(void *_Borrow _Nonnull)self)`
- [ ] Assign trampolines (not bodies) in the vtable initializer
- [ ] Wrap all raw-field indexing in body functions with `_Unsafe` statement/block/expression
- [ ] In destroy body, free all raw buffer fields first, then the struct pointer, all in `_Unsafe`

#### Function pointer hooks (custom allocators)
If the C code uses function pointer hooks for memory allocation (e.g. a configurable `allocate`/`deallocate` struct), you MUST replace them with direct `malloc`/`free`/`realloc` calls. This is required because:
- BSC ownership annotations need to know the actual allocator — function pointer indirection hides this
- Keeping hooks and leaving all pointers raw defeats the purpose of translating to BSC
- The goal is to express ownership at the type level, not preserve runtime allocator configurability

```c
// BEFORE (C) — custom allocator hooks
typedef struct {
    void *(*allocate)(size_t);
    void (*deallocate)(void *);
} hooks;
static hooks global_hooks = { malloc, free };
// ... global_hooks.allocate(size) everywhere ...

// AFTER (BSC) — remove hooks, use direct calls with _Owned
// Delete the hooks struct entirely
// Replace global_hooks.allocate(size) → malloc(size) in _Unsafe, then __take_from_raw
// Replace global_hooks.deallocate(p) → safe_free((void *_Owned)p)
```

Do NOT use "the code has a hook-based allocator" as justification for skipping ownership annotations. Remove the hooks and annotate.

### Step 6: Handle goto + Ownership Cleanup

C code often uses `goto fail` for error handling. In BSC, `_Owned` variables must be properly consumed on all paths:

```c
// BEFORE (C)
Node *item = create_item();
if (!item) goto fail;
// ... more work ...
return item;
fail:
    free(item);
    return NULL;

// AFTER (BSC) — use _Nullable since item may be NULL at cleanup
Node *_Owned _Nullable item = create_item();
if (item == nullptr) goto fail;
// ... more work ...
return item;  // ownership transfers to caller
fail:
    safe_free((void *_Nullable _Owned)item);
    return nullptr;
```

### Step 6.5: Subtle Expression Translations

BSC is a *superset* of C, so expression-level C still compiles. But when you restructure
code — especially when splitting lines to fit the ownership model — a few C idioms
silently change meaning. Check these explicitly.

#### Post-decrement / post-increment in loops

C's `while (length--)` decrements **after** the condition check. If you split this across
lines in BSC (common when you need to insert a bounds check or a borrow), the order
flips.

```c
// C original — length decrements AFTER the test
while (length--) {
    if (strchr("xX", string[length])) { ... }
}

// WRONG BSC translation — accesses the null terminator on first iteration
while (length) {
    if (strchr("xX", string->at(cur_index + length))) { ... }
    length -= 1;
}

// CORRECT — decrement BEFORE access
while (length) {
    length -= 1;
    if (strchr("xX", string->at(cur_index + length))) { ... }
}
```

Real bug seen in the wild: a numeric-parser predicate silently rejected every
input because on the first iteration it accessed position `length` (the null
terminator). `strchr("xX", '\0')` then returned non-NULL — `strchr` finds the
trailing `\0` of the *needle* string — so the guard fired on every call.

#### Pointer-based offsets vs. index-based offsets

C uses `string + start` to point into a buffer. In BSC with `String`, you've got an index
`start` into a container — the base pointer moves, but your index arithmetic is relative
to the container's start.

```c
// C: pointer p moves with the caller's position
strncmp(p, "-0", 2)

// WRONG BSC: compares from start of whole string, not from cur_index
strncmp((const char*)string->as_str(), "-0", 2)

// CORRECT: explicit offset into the container
strncmp((const char*)string->get(cur_index), "-0", 2)
```

Real bug seen in the wild: a numeric-parser predicate compared `"-0"` against
the beginning of the whole input buffer instead of against the number's start
offset. The C code had the offset built into `p`; the BSC port lost it when
`p` became a `String` reference.

#### `sizeof(expr) - 1` for string literals

C macros like `SIZEOF_TOKEN(s)` (`sizeof(s) - 1`) work identically in BSC, but only for
**array** literals. If you accidentally pass a `String` or `const char *`, `sizeof` gives
the pointer size, not the string length. Audit every `sizeof`-on-a-string.

#### Pointer arithmetic on `String` content

`string[i]` in C becomes `string->at(i)` in BSC — which does a **bounds check**. If
your C code relied on reading past the known length (common in fast lexers), you now
get an OOB panic. Use `string->at(i)` inside an explicit bound or use the raw buffer
via `as_str()` + `_Unsafe`.

#### Don't drop the default-emit branch when porting a write-through-cursor loop

Many C routines — serializers, encoders, lexers, formatters — use a moving
cursor to emit bytes:

```c
// C pattern
for (i = 0; i < len; i++) {
    c = input[i];
    switch (c) {
        case '\n': *buf++ = '\\'; *buf++ = 'n'; break;
        case '\t': *buf++ = '\\'; *buf++ = 't'; break;
        /* ...other escape cases... */
        default:   *buf++ = c;          /* <-- THE IMPORTANT BRANCH */
    }
}
```

When you refactor `buf` away and switch to an output builder (e.g.
`out->push(c)`), the instinct is to port each `case` and move on. Easy to
forget the `default` branch — the code still compiles, still runs, but
silently drops every byte that didn't match a case:

```c
// WRONG port — default branch lost
for (size_t i = 0; i < len; i++) {
    char c = input->at(i);
    switch (c) {
        case '\n': out->push('\\'); out->push('n'); break;
        case '\t': out->push('\\'); out->push('t'); break;
        default:   break;                /* BUG: emits nothing */
    }
}

// CORRECT port — default mirrors the C cursor write
for (size_t i = 0; i < len; i++) {
    char c = input->at(i);
    switch (c) {
        case '\n': out->push('\\'); out->push('n'); break;
        case '\t': out->push('\\'); out->push('t'); break;
        default:   out->push(c);         /* emit the byte as-is */
    }
}
```

**Rule:** for every C `case`/`default` that writes to the output via the old
cursor idiom (`*buf++ = ...`), write an equivalent `out->push(...)` (or the
BSC equivalent in your output type) in the port. Diff the two side-by-side
before concluding the port is done.

**How to notice this bug:** output length is much shorter than expected, or
tests that compare against a known-good output fail with "prefix matches,
middle is missing".

#### Porting C iterator loops that advance a `const char *`

C iterates C strings via pointer advance:

```c
// C pattern
const char *p = input;
while (*p != '\0') {
    emit(*p);
    p++;
}
```

You cannot port this directly to a `const char *_Borrow` in `_Safe`:
`_Borrow` pointers forbid indexing `p[i]` and arithmetic `p + n` / `p++`
(see `bsc-borrowing` §5 and §8). Three legitimate ports:

1. **Index + base pointer borrow** — keep the raw base, loop on an index,
   one tight `_Unsafe` block for the deref:
   ```c
   const char *base = input;   // raw, not _Borrow
   size_t i = 0;
   _Unsafe {
       while (base[i] != '\0') { emit(base[i]); i++; }
   }
   ```

2. **Convert to a `String`-typed input** — the idiomatic rewrite, using
   bounds-checked indexing:
   ```c
   for (size_t i = 0; i < input->length(); i += 1) {
       char c = input->at(i);
       if (c == '\0') break;
       emit(c);
   }
   ```

3. **Fixed-literal helpers** — if the "string" being iterated is a small
   set of compile-time constants (e.g. "null"/"true"/"false" tokens), don't
   iterate at all. Write one helper per literal that push-emits the bytes
   straight to the output. Zero `_Unsafe`, zero allocation, no loop.

Pick based on where the bytes come from: user input → #2; known literal → #3;
raw interop buffer → #1.

### Step 7: Compile and Fix

```bash
clang file.cbs -o output
```

BSC compiler will report errors for ownership violations. Fix them iteratively.

## Common Translation Errors (From Real Projects)

These are the most frequent errors when translating C to BSC. Check your translation against each one.

### Error 1: `_Owned` pointer initialized to NULL without `_Nullable`
```c
// WRONG — _Owned is non-nullable by default
cJSON *_Owned root = NULL;

// FIX
cJSON *_Owned _Nullable root = nullptr;
```
**When this happens**: Any `_Owned` local variable that starts as NULL, or any error path that sets an `_Owned` to NULL.

### Error 2: Passing `_Owned` pointer to function expecting raw pointer
```c
// WRONG — internal_func expects raw T*, but item is _Owned
void internal_func(cJSON *item);
cJSON *_Owned item = create_item();
internal_func(item);  // ERROR: ownership mismatch

// FIX Option A: Make the function accept _Owned if it consumes
void internal_func(cJSON *_Owned item);

// FIX Option B: Borrow-then-cast (function borrows temporarily; item keeps ownership)
//   `(cJSON *)item` is a direct owned→raw cast — forbidden, even in _Unsafe.
//   The correct pattern is: take a borrow first, then cast the borrow to raw.
_Unsafe { internal_func((cJSON *)&_Mut *item); }

// FIX Option C: Make function accept _Borrow (best when function doesn't free)
void internal_func(cJSON *_Borrow item);
```

### Error 3: Assigning `_Owned` return value to raw variable
```c
// WRONG — create returns _Owned, but result is raw
cJSON *result = cJSON_CreateObject();  // ownership leaked

// FIX
cJSON *_Owned result = cJSON_CreateObject();
```

### Error 4: Returning raw pointer from function declared `_Owned` return
```c
// WRONG — raw `item` cannot be returned as _Owned
cJSON *_Owned detach_item(cJSON *parent) {
    cJSON *item = parent->child;   // raw pointer from struct field
    return item;                    // ERROR: raw → _Owned conversion
}
```
```c
// FIX — use __take_from_raw to transfer ownership from a raw pointer.
//   (cJSON *_Owned)item is a raw→owned C cast — forbidden, even in _Unsafe.
cJSON *_Owned detach_item(cJSON *parent) {
    cJSON *item = parent->child;
    // If the caller must be sure item is non-null, null-check first.
    _Unsafe { return __take_from_raw(item); }
}
```
**Note**: `__take_from_raw` preserves nullability of its argument. If `item` could be null and nullability-check is on, either null-check first or declare the return type `cJSON *_Owned _Nullable`.

### Error 5: Using `NULL` instead of `nullptr` for `_Owned` pointers
```c
// WRONG — NULL is C macro, not BSC-aware
cJSON *_Owned _Nullable item = NULL;

// FIX — use nullptr in BSC
cJSON *_Owned _Nullable item = nullptr;
```

### Error 6: Ownership transfer into container then continued use
```c
// WRONG — after AddItemToObject, item is moved, caller cannot use it
cJSON *_Owned item = cJSON_CreateObject();
cJSON_AddItemToObject(root, "key", item);     // item ownership moved
cJSON_AddStringToObject(item, "name", "val"); // ERROR: use after move
```

**FIX Option A — populate BEFORE transferring ownership** (preferred: no aliasing needed):
```c
cJSON *_Owned item = cJSON_CreateObject();
cJSON_AddStringToObject(&_Mut *item, "name", "val"); // mutate while still owned
cJSON_AddItemToObject(root, "key", item);             // transfer last
```

**FIX Option B — re-borrow from the container after transfer** (when mutation
must happen after `root` sees the item):
```c
cJSON *_Owned item = cJSON_CreateObject();
cJSON_AddItemToObject(root, "key", item);             // item moved
cJSON *_Borrow ref = cJSON_GetObjectItem(root, "key"); // look it up from root
cJSON_AddStringToObject(ref, "name", "val");           // mutate via borrow
```

**DO NOT** save a `cJSON *` raw pointer "alias" of `item` and use it after the
move. In BSC, raw-from-`_Owned` aliasing is a type error in `_Safe` and a
use-after-move hazard in `_Unsafe`. Pick Option A or B.

## Verification Workflow — STOP CONDITION FOR REPORTING SUCCESS

A C-to-BSC translation is **not done** until both the project compile command
runs clean AND the existing test suite passes. This is non-negotiable; the
borrow-checker errors are exactly what makes the translation correct, and a
"finished" translation that doesn't compile is a translation that hides bugs.

### Step A — Find the project's compile command

Look in `CLAUDE.md` (or `AGENTS.md`) for a section labelled "BSC Project
Compile Command" or "Compile command for this project". This is set up by
`install.sh -a claude-plugin` and contains the exact compiler path,
include flags, and verify command for **this project**.

If that section still says `[FILL IN: ...]` or is missing entirely:

> **STOP. Do not guess.** Do not fall back to `clang file.cbs -o output` or
> `clang -x bsc file.c -fsyntax-only` — those will fail for almost every real
> project (wrong compiler, missing includes). Ask the user:
>
> *"I don't see a compile command in `CLAUDE.md`. What's the exact command
> to compile and verify a BSC file in this project? (compiler path, include
> flags, and how to run the test suite)"*
>
> Wait for the user's answer before writing or modifying any BSC code. A
> translation that compiles in your head but not under the project's
> toolchain is worse than no translation.

### Step B — Compile after every meaningful change

After translating a file (or a coherent chunk of one), run the project's
verify command on it. Fix every diagnostic before moving to the next file.
Do not batch a dozen translated files and hope they all compile at the end —
ownership errors are easier to fix one at a time, and the model's mental
state after one file's worth of `_Owned`/`_Borrow` is clearest while the
file is fresh.

### Step C — Run the existing test suite

The translated code must pass **the project's existing tests**. Tests are
the contract. A translation that compiles but breaks tests has changed the
program's behavior, which means the ownership annotations are wrong (you
moved something that shouldn't have been moved, dropped something that
needed to live, or introduced an unintended copy/mutation).

If the project has no tests, say so explicitly in the report:

> *"The translation compiles, but the project has no test suite I could
> run to verify behavior. The translation is syntactically valid BSC but
> not behaviorally verified."*

Do not invent tests as part of the translation work — that's a separate
task. Do report the gap.

### Step D — Reporting

Only after Steps A–C pass do you report the translation as complete. The
report must state, in this order:

1. The exact compile command you ran and that it returned 0.
2. The exact test command you ran and that it returned 0 (or that no
   test suite exists).
3. A summary of the ownership decisions (counts of `_Owned`, `_Borrow`,
   `_Unsafe` blocks; any cases where you escalated to `_Unsafe` rather
   than annotating).

Anything less than this — "I think it should compile", "the borrow checker
might complain about X", "tests probably still pass" — is **not done**. Go
back to Step B.

## Checklist — Verify Before Done

- [ ] File extensions KEPT as `.c`/`.h` — compile with `clang -x bsc file.c` (do NOT rename)
- [ ] `#include` paths unchanged — headers still reference `.h`, not `.hbs`
- [ ] `#include "bishengc_safety.hbs"` added where safe_malloc/safe_free are used (this is the one new include)
- [ ] Platform-specific code LEFT IN PLACE — `#ifdef _WIN32`, `__declspec`, `__cdecl`, `extern "C"` guards all stay; BSC clang handles them
- [ ] **Every pointer analyzed** — classified as owned, borrowed, or raw
- [ ] **Owned struct fields** annotated `*_Owned`
- [ ] **Borrowed struct fields** annotated `*_Borrow`
- [ ] **Interior/arithmetic/iterator pointers** left as raw `T *`
- [ ] **Address-of in _Safe**: `&x` converted to `&_Const x` (read-only) or `&_Mut x` (mutable); `&_Mut` not used on const, literals, or globals
- [ ] **Single-value allocations** converted to `safe_malloc<T>(val)` where possible
- [ ] **Variable-size allocations** (malloc/calloc/strdup/realloc) go through `__take_from_raw(raw_ptr)` in `_Unsafe` — direct `(T *_Owned)` C casts are forbidden everywhere, not just in `_Safe`
- [ ] **Returning raw as `_Owned`**: use `__take_from_raw(raw_ptr)`, not a C cast
- [ ] **Passing `_Owned` where raw is expected**: use borrow-then-cast `(T *)&_Mut *p` (keeps ownership) or `__move_to_raw(p)` (transfers ownership out)
- [ ] **Every free()** converted to `safe_free((void *_Owned)p)` or `safe_free((void *_Nullable _Owned)p)`
- [ ] **Every pointer in every function signature annotated** — no bare `T *` in any parameter or return type unless it is one of the four explicit raw cases (output double-pointer, function pointer, opaque cast intermediary, arithmetic cursor). Decisions match between `.h`/`.hbs` declaration and `.c`/`.cbs` definition.
- [ ] **Function parameters** annotated `_Owned` / `_Borrow` / `const _Borrow` per the decision tree
- [ ] **Function returns** annotated `_Owned` (caller frees) or `_Borrow` (caller does not free)
- [ ] **Read-only getters are `const`**: functions named `get_`/`find_`/`has_`/`count_`/`is_`/`peek_` that don't mutate take `const T *_Borrow this` AND return `const ...*_Borrow` — otherwise callers from `const` contexts can't reach them
- [ ] **Default-emit branch preserved**: every C `*buf++ = c` in a switch/default was ported to the equivalent `out->push(c)` in BSC (silently-dropped bytes are a classic port bug)
- [ ] **Pointer-advance loops over `const char *`**: ported as index + base-pointer in one `_Unsafe`, OR rewritten to `String::at(i)` with a length bound, OR replaced with per-literal helpers when the source is a compile-time constant
- [ ] **Struct promotion decided**: every C struct with heap-owning fields or a matching `*_free` function was promoted to `_Owned struct` with a destructor; pure-value structs stay plain `struct`
- [ ] **No raw `mem*` on owning types**: every `memcpy`/`memmove`/`memset` over a struct containing `*_Owned` or `_Owned struct` members is wrapped in `_Unsafe` and paired with `forget<T>(src)` (ownership transferred) or prior `safe_free` of the fields (pre-overwrite)
- [ ] **Cycles and shared ownership decided deliberately**: single-owner lists/trees use Pattern A (raw pointers + `_Unsafe` + custom destructor, like stdlib `LinkedList<T>`). Only reach for Pattern B (`Rc`+`Weak`+`RefCell`) when nodes are genuinely shared or leaf-up traversal is needed. `Weak<T>` fields are typed `Option<Weak<T>>` because `Weak::new` requires an existing `Rc`.
- [ ] **Conditional ownership** patterns handled with `_Unsafe` or restructuring
- [ ] **`_Owned` + NULL**: Every `_Owned` pointer that can be NULL uses `_Nullable` and `nullptr`
- [ ] **No use-after-move**: After passing `_Owned` to a consuming function, only use raw aliases
- [ ] **`goto` cleanup paths**: `_Owned _Nullable` used for variables freed in error handlers
- [ ] **Qualifier placement**: `T *_Owned`, never `_Owned T*`
- [ ] **Non-zero annotations**: Translation contains `_Owned` on allocating functions, freeing functions, and owned struct fields. Zero `_Owned` = incomplete translation.
- [ ] **No hook/allocator indirection**: Custom allocator structs removed, replaced with direct malloc/free
- [ ] **Nullability**: Pointers that can be null use `_Nullable`; `nullptr` used instead of `NULL` in BSC
- [ ] **Initialization**: Variables initialized before use in `_Safe` zones; arrays use init lists (not element-by-element)
- [ ] **Project compile command** (from `CLAUDE.md`) runs clean on the changed files. NOT a generic `clang file.cbs` — the project's actual command. If `CLAUDE.md` has no compile command, the model has STOPPED and asked the user (see "Verification Workflow")
- [ ] **Project test suite** runs clean on the changed files (or the absence of a test suite is reported explicitly)
- [ ] **Every `_Unsafe` block reviewed**: enclosing function is `_Safe` if its interface allows; the block contains only the lines that actually need the escape
- [ ] **Compiler-driven `_Unsafe`**: every `_Unsafe` in the diff corresponds to a specific compiler diagnostic that could not be fixed by adjusting annotations or rewriting in `_Safe`-zone form. No `_Unsafe` was added pre-emptively. If you can delete an `_Unsafe` and the file still compiles, delete it.
