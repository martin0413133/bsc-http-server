---
name: bsc-trait
description: "BiSheng C traits. When you need to understand _Trait definition, _Impl registration, trait pointers, dynamic dispatch (vtable), generic traits, trait pointer variables, trait type casting, or trait-based polymorphism, use this Skill."
---

# BiSheng C Traits Skill

## 1. Overview

Interface abstraction with vtable-based dynamic dispatch. Define a set of methods, implement them for concrete types, and call through trait pointers for polymorphism.

## 2. Defining Traits

```c
_Trait Printable {
    void print(This* this);          // First param MUST be `This* this`
    int toInt(This* this);
};
```

- Only function declarations inside `_Trait` body (no implementations, no variables)
- First param must be `This* this` — named exactly `this`
- `_Trait` definitions only at top-level scope (not inside functions or structs)
- Empty traits are allowed: `_Trait T {};`
- Cannot extend member functions on a trait type: `void _Trait T::method(_Trait T* this)` is an error
- Can typedef: `typedef _Trait Printable { void print(This* this); } P;`

## 3. Implementing Traits

```c
struct Circle { float r; };

void struct Circle::print(struct Circle* this) {
    printf("Circle(r=%f)\n", this->r);
}
int struct Circle::toInt(struct Circle* this) {
    return (int)this->r;
}

_Impl _Trait Printable for struct Circle;  // all methods must exist before this line
```

- Can implement for primitives: `_Impl _Trait Printable for int;`
- Can implement for typedef-ed types
- All trait methods must be defined **before** the `_Impl` statement
- Cannot implement trait for a trait type: `_Impl _Trait T for _Trait T;` is an error
- Cannot implement trait for an instantiated generic struct type:
  ```c
  struct G<T> {};
  _Impl _Trait Printable for struct G<int>;  // ERROR
  ```

## 4. Trait Pointers (Dynamic Dispatch)

```c
void printAnything(_Trait Printable* obj) {
    obj->print();  // vtable dispatch
}

int main() {
    struct Circle c = {.r = 3.14};
    _Trait Printable* p = &c;      // implicit conversion
    p->print();
    printAnything(&c);

    int x = 42;
    p = &x;                        // OK if int implements Printable
    p->print();
    return 0;
}
```

### Trait pointer variable rules

- Only pointer form: `_Trait T* p;` — **NOT** `_Trait T p;`
- Trait pointers **cannot be dereferenced** with `*`
- Multi-level pointers allowed: `_Trait T** q = &p;` — access via `(*q)->method()`
- `const`/`volatile` qualifiers apply: `const _Trait T* p;` or `_Trait T* const p;`
- Can be function parameters and return values
- Conversion: concrete type pointer implicitly converts to trait pointer if `_Impl`'d; reverse requires explicit cast
- **Comparison** limited to `==` and `!=` only:
  - `p == NULL` / `p != NULL` — OK
  - `p1 == p2` — OK if same trait type; **warning** if different trait types
  - `p == concretePtr` — OK if concrete type implements the trait; warning if not

### Type casting rules

- Cannot cast trait pointer to a non-pointer type
- Cast to concrete type must be **explicit**: `(int*)p` — no implicit downcasting
- Cast to `void*` is allowed: `(void*)p`
- `void*` **CANNOT** be cast back to trait pointer

## 5. Generic Traits

```c
_Trait Converter<T> {
    T convert(This* this);
};

int int::convert(int* this) { return *this; }
_Impl _Trait Converter<int> for int;

float float::convert(float* this) { return *this; }
_Impl _Trait Converter<float> for float;
```

### Generic trait rules

- Generic trait definitions follow the same scope rules (top-level only)
- Only **instantiated** generic traits can be `_Impl`'d: `_Impl _Trait Converter<int> for int;`
- `_Impl _Trait Converter<T> for int;` is ERROR — `T` is undefined
- The implementing struct/union type cannot itself be generic:
  ```c
  struct G<T> {};
  _Impl _Trait Converter<int> for struct G<int>;  // ERROR: cannot _Impl for instantiated type
  ```

### Generic trait pointer variables

- Use instantiated form: `_Trait Converter<int>* p;`
- Multi-level pointers: `_Trait Converter<int>** q;`
- `const`/`volatile` on generic trait pointers works the same as non-generic
- Comparing different instantiations produces a **warning**: `_Trait Converter<int>* t1` vs `_Trait Converter<float>* t3` → warning on `t1 == t3`
- Casting rules are the same as non-generic trait pointers

## 6. Complete Example

```c
#include <stdio.h>

// Shape trait with area and side-length methods
_Trait Shape {
    float getArea(This* this);
    float getSideLen(This* this);
};

struct Square { float side; };
struct Rectangle { float width; float height; };

float struct Square::getArea(struct Square* this) {
    return this->side * this->side;
}
float struct Square::getSideLen(struct Square* this) {
    return this->side * 4;
}
_Impl _Trait Shape for struct Square;

float struct Rectangle::getArea(struct Rectangle* this) {
    return this->width * this->height;
}
float struct Rectangle::getSideLen(struct Rectangle* this) {
    return (this->width + this->height) * 2;
}
_Impl _Trait Shape for struct Rectangle;

// Dynamic dispatch through trait pointer
void showShape(_Trait Shape* s) {
    printf("  area = %f\n", s->getArea());
    printf("  perimeter = %f\n", s->getSideLen());
}

int main() {
    struct Square sq = {.side = 5.0};
    struct Rectangle rc = {.width = 3.0, .height = 4.0};

    _Trait Shape* p = &sq;
    showShape(p);       // Square: area=25, perimeter=20

    p = &rc;
    showShape(p);       // Rectangle: area=12, perimeter=14

    // Compare trait pointers
    _Trait Shape* p2 = &sq;
    if (p != p2) {
        printf("Different shapes\n");
    }

    // Explicit cast back to concrete type
    struct Rectangle* rp = (struct Rectangle*)p;
    return 0;
}
```

> For member function basics, see `bsc-member-function` Skill
> For generic functions/structs, see `bsc-generic` Skill
> For trait errors (BSC-E05xx), see `bsc-errors` Skill
