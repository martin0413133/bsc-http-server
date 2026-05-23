---
name: bsc-stdlib
description: "BiSheng C standard library core types (libcbs). When you need to understand safe_malloc, safe_free, safe_swap, forget, Vec<T>, String, LinkedList, Option, Result, or the libcbs design pattern, use this Skill."
---

# BiSheng C Standard Library — Core Types

## 1. Overview

libcbs provides safe container types with RAII semantics. All containers are `_Owned struct` types with automatic destructors. Include the appropriate `.hbs` header.

**Key rule**: Generic containers can only be instantiated with `_Owned struct` or copyable types. If `T` has move semantics but is not an `_Owned struct`, wrap it in one.

## 2. Safety Utilities — `bishengc_safety.hbs`

Core safety primitives used by all other libcbs types.

```c
#include "bishengc_safety.hbs"

// Safe malloc — allocates and initializes with value, returns _Owned pointer
T *_Owned safe_malloc<T>(T t);

// Safe free — frees an owned pointer
void safe_free(void *_Owned);

// Safe array malloc — allocates n contiguous T initialized to `initial`,
// returns _Owned _ArrayElem (supports p[i]). See bsc-ownership §8.
_Safe T *_Owned _ArrayElem safe_malloc_array<T>(size_t n, T initial);

// Safe array free — frees an _Owned _ArrayElem (NOT interchangeable with safe_free)
_Safe void safe_free_array(void *_Owned _ArrayElem);

// Safe swap — swaps two borrowed values
void safe_swap<T>(T* _Borrow left, T* _Borrow right);

// Forget — takes ownership and drops without running destructor
void forget<T>(T t);
```

**Important**: `safe_free` is NOT `_Safe` — calls to `safe_free` inside a `_Safe` function must be wrapped in an `_Unsafe {}` block. `safe_malloc_array` and `safe_free_array` ARE `_Safe`, so they can be called directly from a `_Safe` function.

**Match alloc/free**: `safe_malloc` ↔ `safe_free`, `safe_malloc_array` ↔ `safe_free_array`. Crossing the two pairs is a compile error — `T *_Owned` and `T *_Owned _ArrayElem` are distinct pointer categories. Likewise for raw conversion: `__move_to_raw` / `__take_from_raw` for `_Owned`; `__move_array_to_raw` / `__take_array_from_raw` for `_Owned _ArrayElem`.

### Usage Example

```c
#include "bishengc_safety.hbs"

_Safe void example(void) {
    int *_Owned p = safe_malloc(42);

    int a = 1;
    int b = 2;
    safe_swap(&_Mut a, &_Mut b);  // a=2, b=1

    _Unsafe {
        safe_free((void *_Owned)p);
    }
}
```

## 3. Vec\<T\> — Dynamic Array

```c
#include "vec.hbs"
```

| Method | Signature | Description |
|--------|-----------|-------------|
| `new` | `Vec<T>::new()` → `Vec<T>` | Create empty vec |
| `with_capacity` | `Vec<T>::with_capacity(size_t)` → `Vec<T>` | Create with pre-allocated capacity |
| `push` | `v.push(T)` | Append element (grows if needed) |
| `pop` | `v.pop()` → `T` | Remove and return last element (aborts if empty) |
| `get` | `v.get(size_t)` → `const T *_Borrow` | Immutable access (aborts if out of bounds) |
| `get_mut` | `v.get_mut(size_t)` → `T *_Borrow` | Mutable access (aborts if out of bounds) |
| `set` | `v.set(size_t, T)` | Set element at index (pushes if index == len) |
| `remove` | `v.remove(size_t)` → `T` | Remove element at index, shift remaining |
| `length` | `v.length()` → `size_t` | Number of elements |
| `capacity` | `v.capacity()` → `size_t` | Allocated capacity |
| `is_empty` | `v.is_empty()` → `_Bool` | Whether vec is empty |
| `clear` | `v.clear()` | Remove all elements (calls destructors) |
| `shrink_to_fit` | `v.shrink_to_fit()` | Reduce capacity to length |

```c
_Safe void vec_example(void) {
    Vec<int> v = Vec<int>::new();
    v.push(42);
    v.push(17);
    const int *_Borrow ref = v.get(0);       // 42
    int val = v.pop();                        // 17
    int removed = v.remove(0);               // 42
    // ~Vec auto-frees when v goes out of scope
}
```

## 4. String

```c
#include "string.hbs"
```

| Method | Signature | Description |
|--------|-----------|-------------|
| `new` | `String::new()` → `String` | Create empty string |
| `with_capacity` | `String::with_capacity(size_t)` → `String` | Create with capacity |
| `from` | `String::from(const char*)` → `String` | **_Unsafe** — create from C string |
| `as_str` | `s.as_str()` → `const char *_Borrow` | Immutable C string view |
| `as_mut_str` | `s.as_mut_str()` → `char *_Borrow` | Mutable C string view |
| `get` | `s.get(size_t)` → `const char *_Borrow` | Immutable char access (bounds-checked) |
| `get_mut` | `s.get_mut(size_t)` → `char *_Borrow` | Mutable char access (bounds-checked) |
| `at` | `s.at(size_t)` → `char` | Character at index |
| `push` | `s.push(char)` | Append character |
| `set` | `s.set(size_t, char)` | Set character at index |
| `find` | `s.find(char)` → `size_t` | Find first occurrence (`bsc_string_no_pos` if not found) |
| `slice` | `s.slice(size_t start, size_t len)` → `String` | Extract substring |
| `equals` | `s.equals(&other)` → `_Bool` | Compare two strings |
| `length` | `s.length()` → `size_t` | String length |
| `capacity` | `s.capacity()` → `size_t` | Allocated capacity |
| `shrink_to_fit` | `s.shrink_to_fit()` | Reduce capacity to length |
| `is_empty` | `s.is_empty()` → `_Bool` | Whether string is empty |

**Note**: `String::from()` is `_Unsafe` — wrap in `_Unsafe {}` block when used in `_Safe` code.

## 5. LinkedList\<T\>

```c
#include "list.hbs"
```

| Method | Signature | Description |
|--------|-----------|-------------|
| `new` | `LinkedList<T>::new()` → `LinkedList<T>` | Create empty list |
| `push_back` | `l.push_back(T)` | Append to end |
| `push_front` | `l.push_front(T)` | Prepend to front |
| `pop_back` | `l.pop_back()` → `T` | Remove and return last (aborts if empty) |
| `pop_front` | `l.pop_front()` → `T` | Remove and return first (aborts if empty) |
| `front` | `l.front()` → `T` | Return copy of first element |
| `back` | `l.back()` → `T` | Return copy of last element |
| `remove` | `l.remove(T)` | Remove all elements equal to value |
| `split_off` | `l.split_off(size_t)` → `LinkedList<T>` | Split list at index |
| `contains` | `l.contains(T)` → `_Bool` | Check if element exists |
| `length` | `l.length()` → `size_t` | Number of elements |
| `is_empty` | `l.is_empty()` → `_Bool` | Whether list is empty |
| `clear` | `l.clear()` | Remove all elements |

## 6. Option\<T\>

```c
#include "option.hbs"
```

| Method / Function | Signature | Description |
|--------|-----------|-------------|
| `Some` | `Option<T>::Some(T)` → `Option<T>` | Create Some value |
| `None` | `Option<T>::None()` → `Option<T>` | Create None value |
| `is_some` | `o.is_some()` → `_Bool` | Check if Some |
| `is_none` | `o.is_none()` → `_Bool` | Check if None |
| `option_unwrap` | `option_unwrap<T>(Option<T>)` → `T` | Extract value (aborts if None) |

```c
_Safe void option_example(void) {
    Option<int> some = Option<int>::Some(42);
    if (some.is_some()) {
        int val = option_unwrap<int>(some);  // 42
    }
}
```

**Note**: `option_unwrap` is a free function, not a method. It consumes the Option (takes by value).

## 7. Result\<T, E\>

```c
#include "result.hbs"
```

| Method / Function | Signature | Description |
|--------|-----------|-------------|
| `Ok` | `Result<T, E>::Ok(T)` → `Result<T, E>` | Create Ok value |
| `Err` | `Result<T, E>::Err(E)` → `Result<T, E>` | Create Err value |
| `is_ok` | `r.is_ok()` → `_Bool` | Check if Ok |
| `is_err` | `r.is_err()` → `_Bool` | Check if Err |
| `result_unwrap` | `result_unwrap<T, E>(Result<T, E>)` → `T` | Extract Ok (aborts if Err) |
| `result_unwrap_err` | `result_unwrap_err<T, E>(Result<T, E>)` → `E` | Extract Err (aborts if Ok) |

**Note**: `result_unwrap` and `result_unwrap_err` are free functions that consume the Result.

## 8. Design Pattern: Safe API with Unsafe Internals

All libcbs types follow a consistent pattern:

```c
_Owned struct MyType<T> {
_Public:
    // ... fields ...
    ~MyType(MyType<T> this) { /* cleanup */ }
};

_Safe void MyType<T>::method(MyType<T> *_Borrow this, ...) {
    _Unsafe { /* implementation using raw operations */ }
}
```

## 9. Include Summary

| Header | Types Provided |
|--------|---------------|
| `bishengc_safety.hbs` | `safe_malloc`, `safe_free`, `safe_swap`, `forget` |
| `vec.hbs` | `Vec<T>` |
| `string.hbs` | `String` |
| `list.hbs` | `LinkedList<T>` |
| `option.hbs` | `Option<T>` |
| `result.hbs` | `Result<T, E>` |
| `rc.hbs` | `Rc<T>`, `Weak<T>` |
| `cell.hbs` | `Cell<T>`, `RefCell<T>`, `RefImmut<T>`, `RefMut<T>` |
| `slice.hbs` | `Slice<T>`, `SliceMut<T>` |
| `hash_map.hbs` | `HashMap<K, V, S>` |

> For smart pointers, interior mutability, slices, and HashMap, see `bsc-stdlib-advanced` Skill
> For ownership/RAII patterns, see `bsc-ownership` Skill
> For borrowing in API design, see `bsc-borrowing` Skill
