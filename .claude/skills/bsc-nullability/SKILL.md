---
name: bsc-nullability
description: "BiSheng C non-null pointers and nullability. When you need to understand _Nullable, _Nonnull, nullptr, null-safety checking, pointer nullability tracking, null check patterns, or -nullability-check compiler option, use this Skill."
---

# BiSheng C Nullability Skill

## 1. Overview

BSC tracks pointer nullability at compile time. The compiler prevents dereferencing or accessing members through pointers that may be null.

- `_Nonnull` — pointer guaranteed non-null (default for `_Owned` and `_Borrow`)
- `_Nullable` — pointer may be null (default for raw pointers)
- `nullptr` — null pointer literal (replaces `NULL` in safe zones)

## 2. Default Nullability

| Pointer kind | Default | Override |
|-------------|---------|----------|
| Raw pointer (`int *`) | Nullable | `int *_Nonnull` |
| `_Owned` pointer (`int *_Owned`) | Nonnull | `int *_Owned _Nullable` |
| `_Borrow` pointer (`int *_Borrow`) | Nonnull | `int *_Borrow _Nullable` |

```c
// Nullable pointers:
int *_Nullable p1 = nullptr;
int *_Borrow _Nullable p2 = nullptr;
int *_Owned _Nullable p3 = nullptr;
int *p4 = nullptr;                    // raw pointer is Nullable by default

// Nonnull pointers:
int *_Nonnull p5 = &a;
int *_Borrow p6 = &_Mut a;           // _Borrow is Nonnull by default
int *_Owned p7 = safe_malloc<int>(5); // _Owned is Nonnull by default
```

## 3. Trackable Pointers

The compiler can track nullability only for pointers that:
1. Are **lvalues** (have a memory address)
2. Are **not** `volatile`
3. Are **not** obtained through array subscript (`[]`)

```c
_Safe void test(int *_Borrow _Nullable p, int *_Borrow _Nullable volatile vp) {
    int *_Borrow _Nullable q = nullptr;
    int *_Borrow _Nullable arr[2] = {nullptr, &_Mut local};

    if ((q = p) != nullptr) {
        *q = 1;  // ok: q is trackable lvalue
    }
    if (identity(p) != nullptr) {
        *identity(p) = 3;  // error: function return is not lvalue
    }
    if (vp != nullptr) {
        *vp = 4;  // error: volatile pointer not tracked
    }
    if (arr[1] != nullptr) {
        *arr[1] = 5;  // error: array subscript not tracked
    }
}
```

### Workaround: copy through a trackable local

When you need to dereference an untrackable nullable pointer, first bind it to
a **fresh local variable** (which is trackable by virtue of being a plain lvalue),
then null-check and dereference the local:

```c
_Safe void use_untrackable(int *_Borrow _Nullable p, int *_Borrow _Nullable arr[]) {
    int *_Borrow _Nullable t1 = identity(p);  // trackable temp
    if (t1 != nullptr) { *t1 = 3; }           // ok

    int *_Borrow _Nullable t3 = arr[1];       // trackable temp
    if (t3 != nullptr) { *t3 = 5; }           // ok
}
```

The temp must be a local variable — not a struct member, not another array
element. This is the recommended pattern whenever you face an untrackable
nullable pointer.

## 4. Nullability State Changes

For **Nonnull-marked** pointers: state is always Nonnull. If null-checked, the null branch treats it as null (useful for `_Owned` leak prevention):

```c
_Safe int test(void) {
    int *_Owned a = safe_malloc<int>(1);  // Nonnull
    if (a != nullptr) {
        safe_free((void *_Owned)a);
        return 1;
    }
    return 0;  // ok: no leak error (a treated as null in this branch)
}
```

For **Nullable-marked** pointers, state changes via:
1. **Assignment with non-null expression** → becomes Nonnull
2. **Null check in control flow** → Nonnull in the non-null branch

```c
_Safe void test(void) {
    int *_Borrow _Nullable p1 = nullptr;    // Nullable
    *p1 = 10;                                // error!

    int local = 10;
    p1 = &_Mut local;                        // → Nonnull (non-null assignment)
    *p1 = 20;                                // ok

    p1 = foo(&_Mut local);                   // → Nullable (foo returns _Nullable)
    *p1 = 20;                                // error!

    p1 = bar(&_Mut local);                   // → Nonnull (bar returns Nonnull)
    *p1 = 20;                                // ok

    int *_Borrow _Nullable p2 = foo(&_Mut local);
    if (p2 != nullptr)
        *p2 = 10;  // ok: Nonnull in true branch
    else
        *p2 = 20;  // error: Nullable in else branch
}
```

## 5. Null Check Patterns

In conditions of `if`/`while`/ternary, these patterns constitute null checks:

1. **Direct pointer**: `if (p)` / `while (s.p)`
2. **Logical operators**: `if (!p)` / `if (p && q)` / `if (p || q)`
3. **Explicit comparison**: `if (p != nullptr)` / `if (nullptr == p)`
4. **With assignment**: `if ((q = p) != nullptr)` — only checks `q`
5. **In comma expression**: `if ((x, p != nullptr))` — only checks last
6. **Parentheses**: any of above can be nested in parens

State updates:
- `if (e)` / `if (e != nullptr)`: `e` is Nonnull in true branch
- `if (!e)` / `if (e == nullptr)`: `e` is Nonnull in false/else branch
- `if (p && q)`: both Nonnull in true branch
- `if (!p || !q)`: both Nonnull in else branch

## 6. Assignment, Passing, and Return Rules

```c
// Cannot assign nullable value to Nonnull pointer:
int *_Borrow p1 = nullptr;          // error in safe zone
int *_Borrow p2 = foo(&_Mut local); // error if foo returns _Nullable

// Cannot pass nullable argument to Nonnull parameter:
_Safe void bar(int *_Borrow p) {}
bar(nullable_ptr);  // error

// Cannot return nullable from Nonnull return type:
_Safe int *_Borrow return_nonnull(int *_Borrow p) {
    int *_Borrow _Nullable q = nullptr;
    return q;  // error
}
```

## 7. Type Casting

With `-nullability-check=all`, casting Nullable to Nonnull in non-safe zones is also checked:

```c
void foo() {
    int *p1 = nullptr;
    int *p2 = (int *_Nonnull)p1;   // error: nullable to nonnull cast
    int *_Owned p3 = (int *_Owned)p1; // error
}
```

After a null check, casting is allowed:

```c
void foo() {
    int *p1 = nullptr;
    if (p1 != nullptr) {
        int *_Nonnull p2 = (int *_Nonnull)p1;  // ok
        int *_Owned p3 = (int *_Owned)p1;       // ok
    }
}
```

## 8. Struct Members

```c
struct Data { int *_Borrow _Nullable value; };

_Safe void test(void) {
    int local = 10;
    // Init list: nullability inferred from initializer
    struct Data data1 = {.value = bar(&_Mut local)};  // Nonnull
    *data1.value = 10;  // ok

    // Non-init-list: defaults to Nullable
    struct Data data2 = init_data(&_Mut local);  // Nullable
    *data2.value = 10;  // error

    // Fix: reassign or null-check
    data2.value = bar(&_Mut local);  // → Nonnull
    *data2.value = 10;               // ok

    if (data3.value != nullptr)
        *data3.value = 10;           // ok
}
```

## 9. Compiler Options

`-nullability-check=<mode>`:

| Mode | Behavior |
|------|----------|
| `safeonly` (default) | Only check in `_Safe` zones |
| `all` | Check in all code (safe and non-safe) |

Without the option, behavior is equivalent to `-nullability-check=safeonly`.

> For ownership and `_Owned _Nullable`, see `bsc-ownership` Skill
> For borrowing and `_Borrow _Nullable`, see `bsc-borrowing` Skill
> For safe zones, see `bsc-safe-zone` Skill
> For nullability errors (BSC-E04xx), see `bsc-errors` Skill
