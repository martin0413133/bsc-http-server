---
name: bsc-member-function
description: "BiSheng C member functions. When you need to understand how to attach methods to structs, primitives, or other types using TypeName::method syntax, static methods, this pointer, This keyword, or method call syntax, use this Skill."
---

# BiSheng C Member Functions Skill

## 1. Overview

Attach methods to any type (struct, union, enum, int, float, etc.) without changing its memory layout (size and alignment are unaffected). Three forms: instance methods, value receivers, and static methods. This enables non-intrusive extension — add methods to existing types without modifying their source.

## 2. Syntax

```c
// Instance method — first param is `this` pointer
RetType TypeName::method(TypeName* this, OtherParams...) { body }

// Value receiver — first param is `this` by value
RetType TypeName::method(TypeName this) { body }

// Static method — no `this` param
RetType TypeName::staticMethod(Params...) { body }

// `This` is a shorthand for the enclosing type name
RetType TypeName::method(This* this) { body }
RetType TypeName::method(This this) { body }   // value receiver with This
```

### Declaration vs Definition separation

```c
void int::print(int* this);       // declaration

void int::print(int* this) {      // definition
    printf("%d", *this);
}
```

## 3. Rules

- `this` must be the first parameter if present
- No method overloading or redefinition
- Method names must not collide with member variable names (applies to struct, union, enum)
- Must use `struct` keyword unless typedef-ed: `void struct S::f(struct S* this) {}`
- typedef-ed names work: `typedef struct { int x; } Vec2; void Vec2::f(This* this) {}`
- `this` pointer can be `const`/`volatile` qualified: `void int::read(const int* this) {}`

### Type restrictions — cannot add methods to:
- `void`
- Pointer types
- Array types
- Function types
- cv-qualified types (e.g. `const int`, `volatile float`)
- **Incomplete types** (declared but not fully defined)

### Other restrictions
- If two header files define the same-named member function for the same type, including both in one compilation unit causes a compile error
- Calling member functions directly on **literals** is currently prohibited: `42.print()` is an error (integer, float, and compound literals)
- Static methods must be called as `TypeName::method()` — calling via dot notation on an instance is an error

## 4. Call Syntax

- Dot syntax: `obj.method()` for instances
- Arrow syntax: `ptr->method()` for pointers
- Explicit: `TypeName::method(&obj)` — equivalent to dot syntax

## 5. Complete Example

```c
#include <stdio.h>

struct Person {
    char name[32];
    int age;
};

// Instance methods
char* struct Person::getName(struct Person* this) {
    return this->name;
}

int struct Person::getAge(struct Person* this) {
    return this->age;
}

// Method on primitive type
void int::print(int* this) {
    printf("%d", *this);
}

// Value receiver
int int::doubled(int this) {
    return this * 2;
}

// Static method (no this)
struct Person struct Person::create(const char* name, int age) {
    struct Person p;
    int i = 0;
    while (name[i] != '\0' && i < 31) {
        p.name[i] = name[i];
        i++;
    }
    p.name[i] = '\0';
    p.age = age;
    return p;
}

int main() {
    struct Person p = struct Person::create("Alice", 30);
    printf("Name: %s\n", p.getName());
    printf("Age: %d\n", p.getAge());

    struct Person* pp = &p;
    printf("Via pointer: %s\n", pp->getName());

    int x = 21;
    x.print();
    printf("\n");
    int d = x.doubled();
    printf("Doubled: %d\n", d);

    printf("Explicit: %d\n", struct Person::getAge(&p));
    return 0;
}
```

> For generic member functions, see `bsc-generic` Skill
> For trait methods using This*, see `bsc-trait` Skill
> For operator overloading on types, see `bsc-operator-overloading` Skill
> For member function errors (BSC-E11xx), see `bsc-errors` Skill
