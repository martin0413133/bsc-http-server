---
name: bsc-stdlib-advanced
description: "BiSheng C advanced standard library types (libcbs). When you need to understand Rc<T>, Weak<T>, Cell<T>, RefCell<T>, Slice<T>, SliceMut<T>, HashMap<K,V,S>, or interior mutability patterns, use this Skill."
---

# BiSheng C Standard Library — Advanced Types

## 1. Overview

These types build on the core libcbs containers. All are `_Owned struct` types with automatic destructors. For core types (`Vec`, `String`, `Option`, `Result`, etc.), see `bsc-stdlib` Skill.

## 2. Rc\<T\> and Weak\<T\> — Reference Counting

```c
#include "rc.hbs"
```

### Rc\<T\> (strong reference)

| Method | Signature | Description |
|--------|-----------|-------------|
| `new` | `Rc<T>::new(T)` → `Rc<T>` | Create new Rc with value |
| `clone` | `rc.clone()` → `Rc<T>` | Increment ref count, return new Rc |
| `deref` | `rc.deref()` → `const T *_Borrow` | Get immutable borrow to inner value |
| `strong_ref_count` | `rc.strong_ref_count()` → `unsigned` | Current strong reference count |
| `weak_ref_count` | `rc.weak_ref_count()` → `unsigned` | Current weak reference count |

### Weak\<T\> (weak reference)

| Method | Signature | Description |
|--------|-----------|-------------|
| `new` | `Weak<T>::new(&rc)` → `Weak<T>` | Create weak ref from Rc |
| `clone` | `w.clone()` → `Weak<T>` | Clone weak reference |
| `upgrade` | `w.upgrade()` → `Option<Rc<T>>` | Try to upgrade to Rc (None if dropped) |
| `strong_ref_count` | `w.strong_ref_count()` → `unsigned` | Strong count (0 if dropped) |
| `weak_ref_count` | `w.weak_ref_count()` → `unsigned` | Weak count |

```c
_Safe void rc_example(void) {
    Rc<int> a = Rc<int>::new(42);
    Rc<int> b = a.clone();                          // strong_count = 2
    const int *_Borrow val = a.deref();             // 42

    Weak<int> w = Weak<int>::new(&_Const a);        // weak_count = 1
    Option<Rc<int>> upgraded = w.upgrade();         // Some(Rc)
    // ~Rc, ~Weak auto-destruct; memory freed when strong_count reaches 0
}
```

## 3. Cell\<T\> and RefCell\<T\> — Interior Mutability

```c
#include "cell.hbs"
```

### Cell\<T\> (copyable types only)

| Method | Signature | Description |
|--------|-----------|-------------|
| `new` | `Cell<T>::new(T)` → `Cell<T>` | Create new Cell |
| `get` | `c.get()` → `T` | Return copy of contained value |
| `set` | `c.set(T)` | Replace contained value |

### RefCell\<T\> (runtime borrow checking)

| Method | Signature | Description |
|--------|-----------|-------------|
| `new` | `RefCell<T>::new(T)` → `RefCell<T>` | Create new RefCell |
| `borrow_immut` | `rc.borrow_immut()` → `RefImmut<T>` | Immutable borrow (aborts if mut-borrowed) |
| `borrow_mut` | `rc.borrow_mut()` → `RefMut<T>` | Mutable borrow (aborts if any borrow active) |
| `try_borrow_immut` | `rc.try_borrow_immut()` → `Option<RefImmut<T>>` | Try immutable borrow (None on failure) |
| `try_borrow_mut` | `rc.try_borrow_mut()` → `Option<RefMut<T>>` | Try mutable borrow (None on failure) |

### RefImmut\<T\> / RefMut\<T\> (RAII borrow guards)

| Method | Signature | Description |
|--------|-----------|-------------|
| `deref` (RefImmut) | `ri.deref()` → `const T *_Borrow` | Get immutable reference |
| `deref` (RefMut) | `rm.deref()` → `T *_Borrow` | Get mutable reference |

Borrow guards automatically release the borrow when they go out of scope.

```c
_Safe void refcell_example(void) {
    RefCell<int> cell = RefCell<int>::new(42);

    // Immutable borrow
    RefImmut<int> ri = cell.borrow_immut();
    const int *_Borrow val = ri.deref();     // 42
    // ~RefImmut releases borrow when ri goes out of scope

    // Mutable borrow (after immutable borrow is released)
    RefMut<int> rm = cell.borrow_mut();
    int *_Borrow mval = rm.deref();
    *mval = 100;
    // ~RefMut releases borrow
}
```

## 4. Slice\<T\> and SliceMut\<T\>

```c
#include "slice.hbs"
```

### Slice\<T\> (immutable view)

| Method | Signature | Description |
|--------|-----------|-------------|
| `new` | `Slice<T>::new(ptr, len)` → `Slice<T>` | **_Unsafe** — create from pointer + length |
| `get` | `s.get(size_t)` → `const T *_Borrow` | Get element (bounds-checked) |
| `sub` | `s.sub(start, end)` → `Slice<T>` | Sub-slice [start, end) |
| `length` | `s.length()` → `size_t` | Number of elements |
| `is_empty` | `s.is_empty()` → `_Bool` | Whether slice is empty |
| `as_ptr` | `s.as_ptr()` → `const T *_Nonnull` | **_Unsafe** — raw nonnull pointer |

### SliceMut\<T\> (mutable view)

| Method | Signature | Description |
|--------|-----------|-------------|
| `new` | `SliceMut<T>::new(ptr, len)` → `SliceMut<T>` | **_Unsafe** — create from pointer + length |
| `get` | `s.get(size_t)` → `const T *_Borrow` | Get immutable element |
| `get_mut` | `s.get_mut(size_t)` → `T *_Borrow` | Get mutable element |
| `sub` | `s.sub(start, end)` → `Slice<T>` | Immutable sub-slice |
| `sub_mut` | `s.sub_mut(start, end)` → `SliceMut<T>` | Mutable sub-slice |
| `as_mut_ptr` | `s.as_mut_ptr()` → `T *_Nonnull` | **_Unsafe** — raw mutable nonnull pointer |
| `length` | `s.length()` → `size_t` | Number of elements |
| `is_empty` | `s.is_empty()` → `_Bool` | Whether slice is empty |

## 5. HashMap\<K, V, S\>

```c
#include "hash_map.hbs"
```

| Method | Signature | Description |
|--------|-----------|-------------|
| `with_hasher` | `HashMap<K,V,S>::with_hasher(S)` → `HashMap<K,V,S>` | Create empty with hasher |
| `with_capacity_and_hasher` | `HashMap<K,V,S>::with_capacity_and_hasher(size_t, S)` → `HashMap<K,V,S>` | Create with capacity and hasher |
| `insert` | `m.insert(K, V)` → `Option<V>` | Insert key-value (returns old value if key existed) |
| `get_mut` | `m.get_mut(&k)` → `Option<V *_Borrow>` | Get mutable ref to value |
| `remove` | `m.remove(&k)` → `Option<V>` | Remove key, return value |
| `contains_key` | `m.contains_key(&k)` → `_Bool` | Check if key exists |
| `reserve` | `m.reserve(size_t)` | Reserve additional capacity |
| `length` | `m.length()` → `size_t` | Number of entries |
| `capacity` | `m.capacity()` → `size_t` | Current capacity |
| `is_empty` | `m.is_empty()` → `_Bool` | Whether map is empty |
| `clear` | `m.clear()` | Remove all entries |

**Note**: Keys must implement `hash` and `equals` functions. Built-in types have these pre-implemented.

> For core types (Vec, String, Option, Result, etc.), see `bsc-stdlib` Skill
> For ownership/RAII patterns, see `bsc-ownership` Skill
> For borrowing in API design, see `bsc-borrowing` Skill
