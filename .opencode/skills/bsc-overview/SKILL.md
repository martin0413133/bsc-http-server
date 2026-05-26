---
name: bsc-overview
description: "BiSheng C language overview. When you need to understand what BiSheng C is, its file types (.cbs/.hbs), compiler usage, or get an overview of all BSC features and keywords, use this Skill."
---

# BiSheng C Overview Skill

## 1. What is BiSheng C

BiSheng C (BSC) is a **superset of C** with Rust-inspired memory safety, traits, generics, and async/await. All valid C code is valid BSC code.

- Source files: `.cbs` (or `.c` compiled with `-x bsc`)
- Header files: `.hbs` (or `.h` compiled with `-x bsc`)
- Compiler: clang fork — `clang file.cbs -o output`, or `clang -x bsc file.c -o output` to treat a `.c` file as BSC

## CRITICAL Syntax Rule

`_Owned` and `_Borrow` are **pointer qualifiers** — they go **after the `*`**, never before the type:
- CORRECT: `char *_Owned p = (char *_Owned)malloc(n);`
- CORRECT: `const int *_Borrow r = &_Const x;`
- WRONG: `_Owned char* p = malloc(n);` — does not compile
- WRONG: `_Borrow int* r = ...;` — does not compile

`malloc()` returns raw `void*` and MUST be cast: `(T *_Owned)malloc(...)`.

## 2. Feature Overview

| Feature | Keywords | Description |
|---------|----------|-------------|
| Member functions | `TypeName::method`, `this`, `This` | Attach methods to any type (struct, primitive, etc.) |
| Generics | `<T>`, `<T, int N>` | Compile-time monomorphization, constant generics, type aliases |
| Traits | `_Trait`, `_Impl`, `This*` | Interface abstraction with vtable dynamic dispatch |
| Operator overloading | `__attribute__((operator OP))` | Overload operators for user-defined types |
| Ownership | `_Owned`, `_ArrayElem`, `_Nullable`, `_Nonnull`, `_Public` | Move semantics, RAII destructors, compile-time memory safety |
| Borrowing | `_Borrow`, `_ArrayElem`, `&_Const`, `&_Mut` | Non-owning references with lifetime enforcement |
| Nullability | `_Nullable`, `_Nonnull`, `nullptr` | Compile-time null-safety checking |
| Safe zones | `_Safe`, `_Unsafe` | Compiler-enforced memory safety regions |
| Initialization analysis | `__attribute__((ensure_init))`, `__assume_initialized` | Field-level uninitialized variable detection |
| Coroutines | `_Async`, `_Await`, `Future` | Stackless coroutines with poll-based execution |
| Scheduler | `Scheduler::init/spawn/run/destroy` | Thread-pool concurrent task execution |
| constexpr | `constexpr`, `_Static_assert`, `if constexpr` | Compile-time evaluation and type-specialized branches |
| Type traits | `is_integral<T>()`, `is_pointer<T>()`, etc. | Compile-time type classification |
| Standard library | `Vec<T>`, `String`, `Option<T>`, `Result<T>` | Safe containers with RAII |

## 3. Quick Syntax Summary

| Feature | Syntax |
|---------|--------|
| Source/header | `.cbs` / `.hbs` |
| Member function | `RetType TypeName::method(TypeName* this, ...)` |
| Static member | `RetType TypeName::method(...)` (no this) |
| Generic function | `T func<T>(T arg)` |
| Generic struct | `struct S<T> { T field; };` |
| Constant generic | `struct A<T, int N> { T data[N]; };` |
| Type alias (generic) | `typedef Alias<T> = OldType<T>;` |
| Conditional type alias | `conditional<C, T, F>` (from `bsc_conditional.hbs`) |
| Trait definition | `_Trait Name { RetType method(This* this); };` |
| Trait impl | `_Impl _Trait Name for TypeName;` |
| Trait pointer | `_Trait Name* ptr = &value;` |
| Operator overload | `__attribute__((operator+)) RetType func(...)` |
| Owned pointer | `T *_Owned` (**not** `_Owned T*` — qualifier goes after `*`) |
| Owned struct | `_Owned struct S { _Public: ...; ~S(S this) {...} };` (must be at file scope) |
| Owned array pointer | `T *_Owned _ArrayElem` — `_Owned` pointer that supports `[]` (see `bsc-ownership`) |
| Immutable borrow | `const T *_Borrow ref = &_Const value;` (**not** `_Borrow T*`) |
| Mutable borrow | `T *_Borrow ref = &_Mut value;` |
| Borrow into array | `T *_Borrow _ArrayElem p = &_Mut arr[i];` — supports `[]`, `+`, `-`, `++`, `--` |
| Nullable pointer | `T *_Owned _Nullable p = nullptr;` |
| Nonnull pointer | `T *_Nonnull p = &value;` |
| Safe function | `_Safe RetType func(ParamType param) { ... }` |
| Unsafe block | `_Unsafe { ... }` |
| Async function | `_Async RetType func(...) { ... }` |
| Await | `T result = _Await asyncFunc();` |
| constexpr if | `if constexpr (expr) { ... }` |
| Destructor | `~TypeName(TypeName this) { ... }` (inside `_Owned struct`) |
| ensure_init | `void f(int *__attribute__((ensure_init)) out)` |
| assume init | `_Unsafe { __assume_initialized(&x); }` |

## 4. Skill Index

> For member functions, see `bsc-member-function` Skill
> For generics, see `bsc-generic` Skill
> For traits, see `bsc-trait` Skill
> For operator overloading, see `bsc-operator-overloading` Skill
> For ownership, see `bsc-ownership` Skill
> For borrowing, see `bsc-borrowing` Skill
> For nullability, see `bsc-nullability` Skill
> For safe zones, see `bsc-safe-zone` Skill
> For initialization analysis, see `bsc-initialization` Skill
> For coroutines, see `bsc-coroutine` Skill
> For constexpr, see `bsc-constexpr` Skill
> For standard library, see `bsc-stdlib` Skill
> For compilation, see `bsc-compile` Skill
> For common pitfalls, see `bsc-common-mistakes` Skill
> For error codes and diagnostics, see `bsc-errors` Skill
