---
name: bsc-operator-overloading
description: "BiSheng C operator overloading. When you need to understand how to overload operators (+, -, *, /, ==, !=, <, >, [], ->, etc.) using __attribute__((operator OP)) syntax, parameter/return requirements, or generic operator functions, use this Skill."
---

# BiSheng C Operator Overloading Skill

## 1. Overview

Define operator behavior for user-defined types by marking a global function with `__attribute__((operator OP))`. The compiler redirects matching operator expressions to the overload function.

## 2. Syntax

```c
__attribute__((operator OP))
RetType functionName(Params...) {
    // implementation
}
```

Where `OP` is one of the supported operators listed below.

## 3. Supported Operators

| Category | Operators |
|----------|-----------|
| Binary arithmetic | `+`, `-`, `*`, `/`, `%` |
| Relational | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| Bitwise | `\|`, `&`, `~`, `^`, `<<`, `>>` |
| Unary | `+` (positive), `-` (negate), `*` (dereference) |
| Member access | `->` |
| Indexing | `[]` |

## 4. Parameter and Return Requirements

| Operator | Params | Return |
|----------|--------|--------|
| Relational (`==`, `!=`, `<`, `>`, `<=`, `>=`) | Exactly 2 params; at least one must be a user-defined type (struct, enum) | Must return `_Bool` |
| `*` (deref), `->` (member access) | Exactly 1 param; must be a pointer to user-defined type (raw, `*_Borrow`, or `*_Borrow const`) | Must return a pointer type (raw, `*_Borrow`, `*_Borrow const`, or `Rc` pointer) |
| `[]` (indexing) | Exactly 2 params; first must be a pointer to user-defined type (raw, `*_Borrow`, or `*_Borrow const`) | Must return a pointer type (raw, `*_Borrow`, `*_Borrow const`, or `Rc` pointer) |
| Other unary (`+`, `-`, `~`) | Exactly 1 param; must be a user-defined type | No restriction |
| Other binary (`+`, `-`, `*`, `/`, `%`, `\|`, `&`, `^`, `<<`, `>>`) | Exactly 2 params; at least one must be a user-defined type | No restriction |

## 5. Rules

- Overload functions must be **global functions** — cannot be member functions or `_Trait` functions
- At least one parameter must be a user-defined type (struct, enum) — overloading for purely built-in types is forbidden
- Overload function names must not conflict with existing non-overload function names
- Multiple overloads for the same operator are allowed if function names differ AND parameter types differ
- Overload functions can be **generic functions**
- For `*` (deref) and `->`: the compiler auto-takes `&_Mut` of the operand and passes it to the function. For `*`, the return value is also auto-dereferenced
- For `[]`: the compiler auto-takes `&_Mut` of the operand and passes it with the index. The return value is auto-dereferenced

## 6. Examples

### Binary arithmetic

```c
struct Square { int width; int height; };

__attribute__((operator+))
struct Square squareAdd(struct Square s1, struct Square s2) {
    struct Square s = {s1.width + s2.width, s1.height + s2.height};
    return s;
}

int main() {
    struct Square s1 = {100, 50};
    struct Square s2 = {60, 110};
    struct Square s3 = s1 + s2;  // calls squareAdd
    return 0;
}
```

### Relational

```c
__attribute__((operator>))
_Bool FooCompare(struct Foo A, struct Foo B) {
    return A.y > B.y;
}
```

### Dereference (`*`)

```c
struct MyPoint<T> { T *data; };

__attribute__((operator*))
T * derefMyPoint<T>(struct MyPoint<T> *_Borrow p) {
    return p->data;
}

int main() {
    int data = 100;
    struct MyPoint<int> p = { &data };
    // *p == 100  (equivalent to: *derefMyPoint(&_Mut p))
    *p = 10;
    return 0;
}
```

### Member access (`->`)

```c
struct MyData<T> { T a; };
struct MyPoint<T> { MyData<T> *data; };

__attribute__((operator->))
MyData<T> * mDerefMyPoint<T>(struct MyPoint<T> *_Borrow p) {
    return p->data;
}

int main() {
    struct MyData<int> d = { 100 };
    struct MyPoint<int> p = { &d };
    // p->a == 100  (equivalent to: mDerefMyPoint(&_Mut p)->a)
    p->a = 10;
    return 0;
}
```

### Indexing (`[]`)

```c
#define ARRLEN 10
struct MyArray<T> { T data[ARRLEN]; };

__attribute__((operator[]))
T *GetMyArrayData<T>(struct MyArray<T> *p, int index) {
    return &p->data[index];
}

int main() {
    struct MyArray<int> array;
    for (int i = 0; i < ARRLEN; i++) {
        array[i] = i;  // equivalent to: *GetMyArrayData(&_Mut array, i) = i
    }
    return 0;
}
```

### Generic operator overloading

```c
struct Point<T> { T x; T y; };

__attribute__((operator+))
struct Point<T> Add<T>(struct Point<T> lhs, struct Point<T> rhs) {
    T x1 = lhs.x + rhs.x;
    T y1 = lhs.y + rhs.y;
    struct Point<T> p = { .x = x1, .y = y1 };
    return p;
}

int main() {
    struct Point<int> p1 = {.x = 1, .y = 2};
    struct Point<int> p2 = {.x = 3, .y = 4};
    struct Point<int> p3 = p1 + p2;  // {.x = 4, .y = 6}
    return 0;
}
```

> For member function basics, see `bsc-member-function` Skill
> For generic functions, see `bsc-generic` Skill
> For operator overload errors (BSC-E12xx), see `bsc-errors` Skill
