---
name: bsc-generic
description: "BiSheng C generics. When you need to understand generic functions, generic structs, generic unions, constant generics, generic type aliases, conditional type aliases, generic member functions, or type deduction, use this Skill."
---

# BiSheng C Generics Skill

## 1. Overview

Compile-time monomorphization — no runtime overhead. The compiler generates specialized code for each type used.

## 2. Generic Functions

```c
T max<T>(T a, T b) {
    return a > b ? a : b;
}

void swap<T>(T* a, T* b) {
    T tmp = *a;
    *a = *b;
    *b = tmp;
}

int main() {
    int m1 = max<int>(3, 5);       // explicit instantiation
    int m2 = max(3, 5);            // implicit type deduction
    float m3 = max<float>(1.2, 2.5);
    return 0;
}
```

## 3. Generic Structs and Unions

Generic unions follow the same syntax as generic structs.

```c
struct Pair<T1, T2> {
    T1 first;
    T2 second;
};

union Value<T1, T2> {
    T1 as_first;
    T2 as_second;
};

T1 struct Pair<T1, T2>::getFirst(This* this) {
    return this->first;
}

T1 union Value<T1, T2>::getFirst(This* this) {
    return this->as_first;
}

int main() {
    struct Pair<int, float> p = {.first = 1, .second = 2.0};
    int f = p.getFirst();
    Pair<int, float> p2 = p;  // `struct` can be omitted when using generic struct types

    union Value<int, float> v;
    v.as_first = 42;
    return 0;
}
```

- Generic struct/union instantiation is **explicit-only** — no implicit type deduction (unlike generic functions)

## 4. Constant Generics

```c
struct Array<T, int N> {
    T data[N];
};

int sumN<int N>(int arr[N]) {
    int s = 0;
    for (int i = 0; i < N; i++) s += arr[i];
    return s;
}

int main() {
    struct Array<int, 5> a;
    constexpr int sz = 10;
    struct Array<float, sz> b;  // constexpr OK as arg
    struct Array<int, (sz + 1)> c;  // non-trivial expr needs parens
    return 0;
}
```

### Constant generic parameter types
- Declaration: `int` and its modifiers (`long`, `short`, `unsigned`, `signed`) and their combinations
- Also supports typedef aliases of the above: `typedef unsigned long ulong;` then `struct S<ulong N> {...}`

### Constant generic argument types
- Integer literals (`1`, `2`, etc.) — no parens needed
- `constexpr` variables — no parens needed
- `constexpr` function calls and other constant expressions — **must be parenthesized**

**Parenthesization rule**: plain integer literals and constexpr constants need no parens; all other constant expressions must be parenthesized — e.g. `Array<int, 5>` and `Array<int, sz>` are fine, but `Array<int, (sz + 1)>` requires parens.

## 5. Generic Type Aliases

```c
// New-style typedef required for generic aliases:
typedef Alias<T> = T*;
typedef IntArray<int N> = int[N];
typedef MyPair<T> = struct Pair<T, T>;

// Non-generic new-style also works:
typedef Int64 = long int;
```

**Restrictions:**
- Cannot define generic type aliases inside function bodies
- Cannot define type aliases inside struct bodies
- Cannot extend member functions on a generic type alias

## 6. Conditional Type Alias

Requires `#include <bsc_conditional.hbs>`.

```c
#include <bsc_conditional.hbs>

// conditional<C, T, F>: when C is non-zero → type T; when C is zero → type F
typedef conditional<int C, T, F> = __conditional(int C, T, F);

conditional<1, int, float> x = 42;     // x is int
conditional<0, int, float> y = 3.14;   // y is float
```

Use case — selecting types based on generic parameters:

```c
typedef RetType<int C> = conditional<C, int*, float*>;

RetType<1> foo<int C>() {
    // When C is non-zero, RetType<C> is int*; otherwise float*
}
```

## 7. Generic Member Functions

```c
struct MyStruct<T> { T val; };
union MyUnion<T> { T val; };

T struct MyStruct<T>::get(This* this) { return this->val; }

T union MyUnion<T>::get(This* this) { return this->val; }

// Static generic member function
T static struct MyStruct<T>::zero() { T z = 0; return z; }

// Return type can be the generic struct/union type itself
struct MyStruct<T> struct MyStruct<T>::make(T x) {
    struct MyStruct<T> result = { .val = x };
    return result;
}

int main() {
    struct MyStruct<int> s = {.val = 42};
    int v = s.get();
    int z = MyStruct<int>::zero();
    struct MyStruct<int> s2 = MyStruct<int>::make(7);

    union MyUnion<int> u = {.val = 7};
    int w = u.get();
    return 0;
}
```

- No overloading of generic member functions (same rule as non-generic member functions)
- Generic member functions can be static (first parameter not necessarily `this`)
- Return type can be the generic struct/union type

## 8. Rules

- **No spaces** between the function/struct/union name and the angle brackets: `max<int>(a, b)` is correct; `max <int>(a, b)` is not
- Generic functions support both explicit (`max<int>(a, b)`) and implicit (`max(a, b)`) type deduction
- Generic structs and unions support **explicit instantiation only** — no implicit deduction
- Generic unions use identical syntax to generic structs: `union MyUnion<T> { ... };`
- `struct`/`union` keyword can be omitted when *using* a generic struct/union type, but not when *declaring* it
- No overloading of generic member functions
- Constant generic expressions (other than plain literals / constexpr constants) must be parenthesized

## 9. Complete Example

```c
#include <stdio.h>

T max<T>(T a, T b) { return a > b ? a : b; }

void swap<T>(T* a, T* b) { T tmp = *a; *a = *b; *b = tmp; }

int main() {
    printf("max<int> = %d\n", max<int>(3, 7));
    printf("max deduced = %d\n", max(10, 20));

    int a = 1, b = 2;
    swap<int>(&a, &b);
    printf("After swap: a=%d, b=%d\n", a, b);
    return 0;
}
```

> For member function basics, see `bsc-member-function` Skill
> For generic traits, see `bsc-trait` Skill
> For constexpr generic functions, see `bsc-constexpr` Skill
> For conditional type alias, see `bsc-constexpr` Skill
> For generics/constexpr errors (BSC-E09xx), see `bsc-errors` Skill
