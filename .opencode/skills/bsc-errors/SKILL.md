---
name: bsc-errors
description: "BiSheng C error codes and diagnostics. When you encounter a BSC compiler error (BSC-Exxxx), need to understand what an error means, or want to look up fix strategies for specific diagnostics, use this Skill."
---

# BiSheng C Error Codes Skill

## 1. Error Code Scheme

BSC errors use the format `BSC-Exxxx` (errors) and `BSC-Wxxxx` (warnings), grouped by category:

| Range | Category | Description |
|-------|----------|-------------|
| BSC-E01xx | Ownership | Use-after-move, uninitialized, assign, cast, memory leak |
| BSC-E02xx | Borrow Checking | Lifetime, multiple borrows, assign-while-borrowed |
| BSC-E03xx | Safe Zone | Forbidden operations in `_Safe` functions/blocks |
| BSC-E04xx | Nullability | Deref/pass/return/cast nullable pointers |
| BSC-E05xx | Traits | Undefined traits, unimplemented functions, type conflicts |
| BSC-E06xx | Owned Struct / Destructor | Struct tags, destructors, member issues |
| BSC-E07xx | Type System | `_Owned`/`_Borrow` qualifier conflicts, incompatible casts |
| BSC-E08xx | Async | `_Async`/`_Await` usage |
| BSC-E09xx | Generics / Constexpr | Generic function issues, constexpr limitations |
| BSC-E10xx | Parse-level | BSC-specific syntax errors |
| BSC-E11xx | Member Functions | Instance members, attributes |
| BSC-E12xx | Operator Overload | Overload restrictions |

## 2. Diagnostic Suppression Flags

Suppress with `-Eno-<identifier>` on command line or `#pragma GCC diagnostic ignored "-E<identifier>"` in source:

```
bsc-safety-check              # All BSC safety checks
├── bsc-ownership             # All ownership checks
│   ├── use-moved-owned       #   Use after move
│   ├── use-uninit-owned      #   Use uninitialized
│   ├── assign-owned          #   Assign to owned
│   ├── cast-owned            #   Cast owned
│   └── check-memory-leak     #   Memory leaks
├── bsc-borrow                # All borrow checks
│   ├── assign-borrowed       #   Assign while borrowed
│   ├── move-borrowed         #   Move while borrowed
│   ├── use-mutably-borrowed  #   Use while mutably borrowed
│   ├── repeated-borrow       #   Multiple mutable borrows
│   ├── return-local-borrow   #   Return local reference
│   └── short-life-borrow     #   Lifetime too short
└── bsc-nullability           # All nullability checks
    ├── deref-nullable        #   Deref nullable
    ├── pass-nullable         #   Pass nullable
    └── return-nullable       #   Return nullable
```

> For full hierarchy and usage examples, see `bsc-compile` Skill.

## 3. AI-Friendly Database

For LLM/IDE integration, the `errors/ai/` directory contains:

- `bsc-errors.json` — Structured JSON database of all errors with cause, fix strategies, code examples, and keywords
- `bsc-errors.schema.json` — JSON schema
- `error-code-mapping.json` — Maps diagnostic names to error codes

## 4. Common Errors Quick Reference

### Ownership (BSC-E01xx)

| Code | Message | Fix |
|------|---------|-----|
| E0101 | Use of moved value | Use variable before transferring ownership |
| E0104 | Use of uninitialized value | Initialize before use |
| E0106 | Assign to _Owned value | Release old value first, or use a new variable |
| E0115 | Invalid cast of _Owned value | Don't cast an _Owned pointer that still holds ownership |
| E0118 | Memory leak (owned not freed) | Free, return, or transfer before scope ends |
| E0119 | Field memory leak | Free _Owned struct fields before scope ends |
| E0126 | Temporary variable memory leak | Capture return value of _Owned-returning function |

### Borrow (BSC-E02xx)

| Code | Message | Fix |
|------|---------|-----|
| E0201 | Cannot assign to borrowed variable | End borrow before modifying |
| E0202 | Cannot move out of borrowed variable | End borrow before moving |
| E0203 | Cannot use mutably borrowed variable | Read through the borrow pointer instead |
| E0204 | Cannot borrow as mutable more than once | Only one `&_Mut` at a time |
| E0207 | Cannot return reference to local variable | Return by value or use `_Owned` heap allocation |
| E0208 | Value does not live long enough | Ensure borrowed value outlives the borrow |

### Safe Zone (BSC-E03xx)

| Code | Message | Fix |
|------|---------|-----|
| E0301 | Unsafe action forbidden in safe zone | Wrap in `_Unsafe { }` block or use safe alternative |
| E0302 | Invalid _Safe/_Unsafe declaration position | Place qualifier on function decl or block statement |
| E0303 | Union member access in safe zone | Wrap union access in `_Unsafe { }` block |
| E0304 | Forbidden cast in safe zone | Wrap cast in `_Unsafe { }` or pass correct type |
| E0307 | Forbidden operation in _Safe function | Move operation to `_Unsafe` block |
| E0308 | Mutable global variable in safe zone | Make global `const` or define outside safe zone |

### Nullability (BSC-E04xx)

| Code | Message | Fix |
|------|---------|-----|
| E0401 | Nullable pointer cannot be dereferenced | Add null check before dereference |
| E0402 | Cannot pass nullable pointer argument | Guard with null check before call |
| E0403 | Cannot return nullable pointer type | Add null check or change return type to `_Nullable` |
| E0406 | Nonnull assigned by nullable | Guard with null check before assignment |
| E0407 | _Nonnull pointer must be initialized | Initialize at declaration |

> For detailed per-category documentation, see the `errors/` directory.
> For common fixes, see `bsc-common-mistakes` Skill.
