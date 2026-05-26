---
name: bsc-borrowing
description: "BiSheng C borrowing. When you need to understand _Borrow pointers, &_Const (immutable borrow), &_Mut (mutable borrow), borrow lifetime rules, freezing semantics, NLL (Non-Lexical Lifetimes), borrow type conversions, dereference/member access semantics, or borrow function signatures, use this Skill."
---

# BiSheng C Borrowing Skill

## Pick `_Borrow`, Not `_Owned`, By Default for Pointer Parameters

A function that **reads or mutates** a value through a pointer but does **not
take ownership** must use `_Borrow`, never `_Owned`.

- The function frees the pointer, transfers it onward, or stores it long-term → `T *_Owned`.
- The function only reads through the pointer → `const T *_Borrow`.
- The function mutates *through* the pointer but does not free it → `T *_Borrow`.
- The function does pointer arithmetic / iteration → raw `T *`.

`_Owned` parameters consume the caller's variable. Most C functions don't do
that. Defaulting to `_Owned` everywhere makes every call site a one-shot move
and forces ugly re-allocation patterns at the call sites — that's wrong.

For a translation context, see `/c-to-bsc` Step 2.5.

## CRITICAL: `_Borrow` Syntax

**`_Borrow` goes AFTER the `*`, not before the type.** It is a pointer qualifier.

```c
// CORRECT
const int *_Borrow r = &_Const x;
int *_Borrow mr = &_Mut x;

// WRONG (does not compile)
_Borrow int* r = &_Const x;
```

## 1. Overview

Temporary, non-owning references. The compiler enforces borrow rules at compile time — no dangling references, no aliased mutation.

## 2. Creating Borrows

```c
int x = 42;
const int *_Borrow cr = &_Const x;   // immutable borrow — read only
int *_Borrow mr = &_Mut x;           // mutable borrow — read/write
```

From `_Owned` pointer: `&_Const *owned_ptr` or `&_Mut *owned_ptr`.

## 3. Semantics

| Type | Access | Aliasing |
|------|--------|----------|
| `const T *_Borrow` | Read only | Multiple allowed simultaneously |
| `T *_Borrow` | Read + Write | Exactly one at a time |

Mutable borrows implicitly convert to immutable: `T *_Borrow` -> `const T *_Borrow` (compiler inserts `&_Const *`). This works in declarations, assignments, function args, and returns.

## 4. Non-Lexical Lifetimes (NLL)

Borrow lifetimes use **NLL**: a borrow is active from its creation (or re-assignment) to its **last use**, NOT to the end of the lexical scope. NLL can be segmented — if a borrow variable is reassigned, it creates disjoint active ranges.

```c
void use(int *_Borrow p) {}

void foo() {
    int local1 = 1, local2 = 2;
    int *_Borrow p = &_Mut local1;  // NLL segment 1 starts
    use(p);                          // NLL segment 1 ends (last use)
    // local1 is unfrozen here — p's borrow has ended
    local1 = 10;                     // OK: p is no longer active
    p = &_Mut local2;                // NLL segment 2 starts (and ends — no further use)
}
```

Uses that extend NLL: function calls `use(p)`, `return p`, dereference `*p`, member access `p->field`.

## 5. Rules

- **`_Borrow` goes after the `*`**: write `int *_Borrow`, NOT `_Borrow int*`
- While **immutable borrow** active: original is read-only, no mutable borrows allowed
- While **mutable borrow** active: original is **frozen** — no reads, writes, moves, or borrows
- **Struct field borrows**: `&_Mut e.field` freezes only **that field** (blocks whole-struct modification); other fields remain accessible. `&_Const e.field` puts the field into read-only state
- Borrow lifetime <= borrowed value's lifetime (compiler enforced)
- Cannot create borrow of a borrow: `int *_Borrow *_Borrow` is illegal
- Borrow variables must be initialized at declaration
- Cannot be global variables or union members
- `_Owned` and `_Borrow` cannot coexist on same pointer: `int *_Owned _Borrow` is illegal
- No indexing (`p[i]`) and no pointer arithmetic (`p + n`, `p++`) on **plain** borrow pointers. For an array-element borrow, use `_Borrow _ArrayElem` (see §11) — that variant supports `[]`, `+`, `-`, `+=`, `-=`, `++`, `--`
- Cannot use `_Borrow` pointer type as a generic type argument
- No `_Trait` impl for borrow types; no member functions for borrow types
- Cannot take borrow of a struct that itself contains borrow members
- Globals: in safe zones, only immutable borrows (`&_Const`) allowed; no mutable borrows of globals
- String literals: `&_Mut "hello"` is forbidden; `&_Mut * "hello"` is forbidden

### Reassignment rules
- Reassignment of a borrow variable requires the same type and the new source must have a lifetime >= the borrow variable's remaining lifetime

## 6. Dereference and Member Access

### Dereference (`*p`)
| Operation | Immutable borrow | Mutable borrow (T is Copy) | Mutable borrow (T is Move) |
|-----------|-----------------|---------------------------|---------------------------|
| Read `*p` | OK | OK | OK |
| Assign to `*p` | Error | OK | Error (cannot move-assign) |

### Member access (`p->field`)
- Immutable borrow: can read fields, cannot modify
- Mutable borrow: can read and modify fields (for Copy types)
- Immutable borrow cannot call mutable methods (`T *_Borrow this` methods)

## 7. Type Conversions

- **Trait upcasting**: explicit cast required; no implicit downcasting from trait to concrete
- **`T *_Borrow` → `void *_Borrow`**: **implicit** when `T` is a trivial data type (no pointer fields, not an `_Owned struct`); otherwise the conversion is **forbidden**, even with an explicit cast
  ```c
  struct S { int *ptr; };
  int a = 0; struct S s = {.ptr = nullptr};
  _Safe {
      int *_Borrow p1 = &_Mut a;
      void *_Borrow p2 = p1;                       // ok: int is trivial
      struct S *_Borrow p3 = &_Mut s;
      void *_Borrow p4 = (void *_Borrow)p3;        // error: S has pointer field
  }
  ```
- **`void *_Borrow` → `T *_Borrow`**: implicit conversion **forbidden**; explicit cast must be in `_Unsafe`
- **Between `_Borrow` and raw pointer**: forbidden in `_Safe` zones; outside `_Safe`, casts go through the raw `T *` intermediate, never directly
- **Between `_Owned` and `_Borrow`**: C-style casts in either direction are **forbidden**
- **Between mutable and immutable borrows**: explicit cast is forbidden. Mutable→immutable is implicit (compiler inserts `&_Const *`); the reverse is never allowed
- **`T *_Borrow _ArrayElem` → `T *_Borrow`**: allowed as an implicit conversion (equivalent to "re-borrow at the current element, dropping array-iteration semantics"). The reverse `T *_Borrow` → `T *_Borrow _ArrayElem` is forbidden
- **Implicit `_Bool` conversion**: `_Borrow _Nullable` pointers can be used in conditions

## 8. Additional Rules

- Borrow pointers can be used in `if`/`while`/`do-while`/`for`/ternary conditions, but **NOT** in `switch`
- Forbidden operators on **plain** borrow pointers: `-`, `~`, `[]`, `++`, `--`, `*`(arithmetic), `/`, `%`, `&`(bitwise), `|`, `<<`, `>>`, binary `+`, binary `-`. `_Borrow _ArrayElem` re-enables `[]`, binary `+`/`-`, `+=`/`-=`, `++`/`--` (see §11). The rest stay forbidden
- Comparison (`==`, `!=`, `<`, `<=`, `>`, `>=`) is allowed between same-typed borrows. Top-level `const`/`volatile`/`restrict` on the pointee are ignored when comparing — `int *_Borrow` and `const int *_Borrow` are comparable
- `sizeof(T *_Borrow) == sizeof(T *)` and the same for `_Alignof`. `_Borrow _ArrayElem` has the same size/alignment as `T *`
- String literals auto-borrow to `const char *_Borrow` when passed to a `const char *_Borrow` parameter (compiler inserts `&_Const *`)

## 9. Function Signatures

```c
// Read-only access
_Safe int len(const Vec<int> *_Borrow this) {
    return this->len;
}

// Mutable access
_Safe void push(Vec<int> *_Borrow this, int value) { ... }

// Return borrow — lifetime tied to input borrow
_Safe int *_Borrow first(Vec<int> *_Borrow this) { ... }
```

- If return is `_Borrow` and one param is `_Borrow`: return lifetime = param lifetime
- If multiple `_Borrow` params: return lifetime = union of all param lifetimes
- If no `_Borrow` params but return is `_Borrow`: **compile error** — borrow return requires at least one borrow parameter

## 10. Design Patterns

These idioms come up repeatedly when the borrow checker rejects "obvious" C-style code.

### 10.1 Iterative traversal → convert to recursion

**Problem:** You want to walk a linked structure via a while-loop, reassigning a cursor
at each step. The borrow checker forbids this because step N+1's borrow is derived from
step N's borrow, and reassignment invalidates the chain.

```c
// WRONG — cannot reassign `current` while borrows live through it
_Safe T* _Borrow walk(Root* _Borrow root, const Path* _Borrow p) {
    T* _Borrow current = root;
    for (size_t i = 0; i < p->length(); i++) {
        current = current->child(i);   // reassignment conflicts with derived borrow
    }
    return current;
}
```

**Fix:** convert to tail-recursion. Each recursive call has its own fresh stack frame,
so the borrow chain stays linear.

```c
_Safe static T* _Borrow walk_rec(T* _Borrow current, const Path* _Borrow p, size_t i) {
    if (i >= p->length()) { return current; }
    return walk_rec(current->child(i), p, i + 1);
}
_Safe T* _Borrow walk(Root* _Borrow root, const Path* _Borrow p) {
    return walk_rec(root, p, 0);
}
```

The compiler typically optimizes tail-recursion back into a loop at `-O1` and above, so
there's no runtime cost. This pattern was necessary for BSC's `path_get_value` in parson.

### 10.2 Chain calls, don't name intermediate borrows

**Problem:** Extracting an intermediate value into a named variable can extend its
borrow past where you want.

```c
// Potentially awkward — `obj` borrows from `val` through the whole block
JSON_Object* _Borrow obj = val.get_object();
obj->set_string(name, value);
// ... lots of code ...
// any other borrow of `val` conflicts with `obj`
```

**Fix:** when a borrow is used in only one call, chain it:

```c
val.get_object()->set_string(name, value);
// no named borrow — no lifetime extension
```

Name a borrow only when you use it multiple times; otherwise let it be a transient.

### 10.3 Scope the borrow tightly

If you must name a borrow but don't need it for the whole function, wrap it in a
block so its lifetime ends explicitly:

```c
{
    JSON_Object* _Borrow obj = val.get_object();
    obj->set_string(name, value);
}   // <-- `obj` released here
val.validate(&_Mut schema);   // now free to mut-borrow `val` again
```

### 10.4 Extract the work, not the borrow

**Problem:** you need data from the borrow but also need the borrowee freely afterwards.

```c
// WRONG — `key` transitively extends `parent`'s borrow
const String* _Borrow key = parent->get_name(0);
parent->set_value(key, ...);   // mut-borrow conflicts with key
```

**Fix:** clone the data you need, drop the borrow, then do the mutation.

```c
String key_owned;
{
    const String* _Borrow key = parent->get_name(0);
    key_owned = clone_string(key);
}
parent->set_value(&_Const key_owned, ...);   // parent is free again
```

## 11. Complete Example

```c
#include <stdio.h>

void printValue(const int *_Borrow ref) {
    printf("value = %d\n", *ref);
}

void doubleValue(int *_Borrow ref) {
    *ref = *ref * 2;
}

const int *_Borrow getFirst(const int *_Borrow arr, int len) {
    return arr;
}

int main() {
    int x = 21;

    // Immutable borrow
    const int *_Borrow cr = &_Const x;
    printValue(cr);
    printValue(&_Const x);   // inline borrow

    // Mutable borrow
    int *_Borrow mr = &_Mut x;
    doubleValue(mr);
    // NLL: mr's lifetime ended at last use (doubleValue call)
    printf("x = %d\n", x);  // OK: x is no longer frozen

    // Multiple immutable borrows are OK
    const int *_Borrow r1 = &_Const x;
    const int *_Borrow r2 = &_Const x;
    printf("r1=%d, r2=%d\n", *r1, *r2);

    // Struct field borrows — only the borrowed field is frozen
    struct Point { int x; int y; };
    struct Point p = {.x = 10, .y = 20};
    int *_Borrow px = &_Mut p.x;  // only p.x is frozen
    p.y = 30;                      // OK: p.y is a different field
    *px = 50;
    // px NLL ends here (last use above)

    // Mutable-to-immutable implicit conversion
    int val = 5;
    int *_Borrow mp = &_Mut val;
    const int *_Borrow ip = mp;  // OK: implicit conversion

    // String literal auto-borrow
    void print_str(const char *_Borrow s);
    print_str("hello");  // compiler auto-inserts &_Const *

    return 0;
}
```

## 11. `_Borrow _ArrayElem`: Borrowing Into Arrays

Taking the address of an array element with `&_Mut arr[i]` or `&_Const arr[i]`
yields a `_Borrow _ArrayElem` pointer. This is a borrow that **remembers it
points into an array**, so subscript and arithmetic are allowed on it (unlike
plain `_Borrow`).

```c
_Safe int foo(void) {
    int arr[4] = {1, 2, 3, 4};
    int *_Borrow _ArrayElem p = &_Mut arr[0];
    p = p + 1;            // ok: _Borrow _ArrayElem supports +
    p += 1;               // ok: +=
    ++p;                  // ok: ++
    int x = p[0];         // ok: subscript

    int *_Borrow q = p;   // ok: implicit downgrade to plain borrow
    // q + 1;             // error: plain borrow forbids arithmetic
    // q[0];              // error: plain borrow forbids subscript
    return x;
}
```

Rules specific to `_Borrow _ArrayElem`:

- Allowed operators: `[]`, binary `+`, `-`, `+=`, `-=`, `++`, `--`. All other
  plain-`_Borrow` restrictions still apply (no `*` arithmetic, no bitwise, etc.).
- **Implicit downgrade** `T *_Borrow _ArrayElem` → `T *_Borrow` is allowed
  (equivalent to "take a fresh plain borrow at the current element"). The
  reverse explicit cast is **forbidden**.
- For mixed `_Safe`/`_Unsafe` declarations, `_Borrow _ArrayElem` is a single
  qualifier unit — a `_Safe` redeclaration may add `_Borrow _ArrayElem` to a
  bare-pointer `_Unsafe` parameter, but cannot swap between `_Borrow` and
  `_Borrow _ArrayElem` once one is declared.
- `sizeof(T *_Borrow _ArrayElem) == sizeof(T *)`.

Except for the operators above and these conversion rules, **everything else
that applies to plain `_Borrow` (lifetime, freezing, NLL, nullability,
type-compatibility, no-borrow-of-borrow, etc.) applies identically to
`_Borrow _ArrayElem`**. Most of the time you only see this type implicitly via
`&_Mut arr[i]` and immediately bind it to a plain `_Borrow`; name it explicitly
only when you need to subscript or step.

> For ownership (owning pointers), see `bsc-ownership` Skill
> For safe zones that require borrows, see `bsc-safe-zone` Skill
> For nullability with borrows, see `bsc-nullability` Skill
> For borrow errors (BSC-E02xx), see `bsc-errors` Skill
