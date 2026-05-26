---
name: bsc-nullability
description: "BiSheng C 非空指针和可空性。当你需要理解 _Nullable、_Nonnull、nullptr、空安全检查、指针可空性追踪、空检查模式或 -nullability-check 编译器选项时，使用此技能。"
---

# BiSheng C 可空性技能

## 1. 概述

BSC 在编译时追踪指针可空性。编译器阻止通过可能为空的指针进行解引用或成员访问。

- `_Nonnull` — 保证非空的指针（`_Owned` 和 `_Borrow` 的默认值）
- `_Nullable` — 可能为空的指针（原始指针的默认值）
- `nullptr` — 空指针字面量（在安全区域中替代 `NULL`）

## 2. 默认可空性

| 指针类型 | 默认值 | 覆盖 |
|-------------|---------|----------|
| 原始指针（`int *`） | 可空 | `int *_Nonnull` |
| `_Owned` 指针（`int *_Owned`） | 非空 | `int *_Owned _Nullable` |
| `_Borrow` 指针（`int *_Borrow`） | 非空 | `int *_Borrow _Nullable` |

```c
// 可空指针：
int *_Nullable p1 = nullptr;
int *_Borrow _Nullable p2 = nullptr;
int *_Owned _Nullable p3 = nullptr;
int *p4 = nullptr;                    // 原始指针默认可空

// 非空指针：
int *_Nonnull p5 = &a;
int *_Borrow p6 = &_Mut a;           // _Borrow 默认非空
int *_Owned p7 = safe_malloc<int>(5); // _Owned 默认非空
```

## 3. 可追踪的指针

编译器只能追踪满足以下条件的指针的可空性：
1. 是**左值**（有内存地址）
2. 不是 `volatile`
3. 不是通过数组下标（`[]`）获得的

```c
_Safe void test(int *_Borrow _Nullable p, int *_Borrow _Nullable volatile vp) {
    int *_Borrow _Nullable q = nullptr;
    int *_Borrow _Nullable arr[2] = {nullptr, &_Mut local};

    if ((q = p) != nullptr) {
        *q = 1;  // 可以：q 是可追踪的左值
    }
    if (identity(p) != nullptr) {
        *identity(p) = 3;  // 错误：函数返回值不是左值
    }
    if (vp != nullptr) {
        *vp = 4;  // 错误：不追踪 volatile 指针
    }
    if (arr[1] != nullptr) {
        *arr[1] = 5;  // 错误：不追踪数组下标
    }
}
```

### 解决方法：通过可追踪的局部变量复制

当你需要解引用一个不可追踪的可空指针时，首先将其绑定到**新的局部变量**（由于其是普通左值而是可追踪的），然后对局部变量进行空检查和解引用：

```c
_Safe void use_untrackable(int *_Borrow _Nullable p, int *_Borrow _Nullable arr[]) {
    int *_Borrow _Nullable t1 = identity(p);  // 可追踪的临时变量
    if (t1 != nullptr) { *t1 = 3; }           // 可以

    int *_Borrow _Nullable t3 = arr[1];       // 可追踪的临时变量
    if (t3 != nullptr) { *t3 = 5; }           // 可以
}
```

临时变量必须是局部变量——不是结构体成员，也不是另一个数组元素。当你面对不可追踪的可空指针时，这是推荐的模式。

## 4. 可空性状态变化

对于**标记为非空**的指针：状态始终为非空。如果进行了空检查，空分支将其视为空（对 `_Owned` 泄漏预防有用）：

```c
_Safe int test(void) {
    int *_Owned a = safe_malloc<int>(1);  // 非空
    if (a != nullptr) {
        safe_free((void *_Owned)a);
        return 1;
    }
    return 0;  // 可以：此分支中将 a 视为空（无泄漏错误）
}
```

对于**标记为可空**的指针，状态通过以下方式变化：
1. **赋值为非空表达式** → 变为非空
2. **控制流中的空检查** → 在非空分支中变为非空

```c
_Safe void test(void) {
    int *_Borrow _Nullable p1 = nullptr;    // 可空
    *p1 = 10;                                // 错误！

    int local = 10;
    p1 = &_Mut local;                        // → 非空（非空赋值）
    *p1 = 20;                                // 可以

    p1 = foo(&_Mut local);                   // → 可空（foo 返回 _Nullable）
    *p1 = 20;                                // 错误！

    p1 = bar(&_Mut local);                   // → 非空（bar 返回 Nonnull）
    *p1 = 20;                                // 可以

    int *_Borrow _Nullable p2 = foo(&_Mut local);
    if (p2 != nullptr)
        *p2 = 10;  // 可以：真分支中为非空
    else
        *p2 = 20;  // 错误：else 分支中为可空
}
```

## 5. 空检查模式

在 `if`/`while`/三目运算符的条件中，以下模式构成空检查：

1. **直接指针**：`if (p)` / `while (s.p)`
2. **逻辑操作符**：`if (!p)` / `if (p && q)` / `if (p || q)`
3. **显式比较**：`if (p != nullptr)` / `if (nullptr == p)`
4. **带赋值**：`if ((q = p) != nullptr)` — 只检查 `q`
5. **逗号表达式中**：`if ((x, p != nullptr))` — 只检查最后一个
6. **括号**：以上任何模式都可以嵌套在括号中

状态更新：
- `if (e)` / `if (e != nullptr)`：`e` 在真分支中为非空
- `if (!e)` / `if (e == nullptr)`：`e` 在假/else 分支中为非空
- `if (p && q)`：两者在真分支中均为非空
- `if (!p || !q)`：两者在 else 分支中均为非空

## 6. 赋值、传递和返回规则

```c
// 不能将可空值赋值给非空指针：
int *_Borrow p1 = nullptr;          // 安全区域中的错误
int *_Borrow p2 = foo(&_Mut local); // 如果 foo 返回 _Nullable 则错误

// 不能将可空参数传递给非空参数：
_Safe void bar(int *_Borrow p) {}
bar(nullable_ptr);  // 错误

// 不能从非空返回类型返回可空值：
_Safe int *_Borrow return_nonnull(int *_Borrow p) {
    int *_Borrow _Nullable q = nullptr;
    return q;  // 错误
}
```

### 原始 `const char*` 参数和 C 字符串返回类型

原始指针参数默认为 `_Nullable`。在 `_Safe` 函数中对可空指针进行下标或解引用是编译错误：

```c
// 错误——s 默认是 _Nullable；s[i] 是不安全的空解引用
_Safe size_t my_strlen(const char* s) {
    size_t n = 0;
    while (s[n]) { n++; }  // 错误：不能解引用可空指针
    return n;
}

// 正确——声明为 _Nonnull；调用者在调用点保证非空
_Safe size_t my_strlen(const char* _Nonnull s) {
    size_t n = 0;
    while (s[n]) { n++; }  // 可以
    return n;
}
```

同样的问题出现在**返回位置**，当可空返回值提供给 `_Nonnull` 参数时：

```c
// 默认返回可空
const char* mime_for_ext(const char* _Nonnull ext);

// 参数是 _Nonnull
_Safe Response make_response(String body, const char* _Nonnull ct);

// 错误——"不能将可空指针参数传递给非空参数"
_Safe Response f(const char* _Nonnull path) {
    return make_response(body, mime_for_ext(path));
}

// 修复——当函数始终返回有效指针时，将返回声明为 _Nonnull
const char* _Nonnull mime_for_ext(const char* _Nonnull ext);
```

**实用规则**：任何对 `const char*` 参数进行下标的 `_Safe` 函数必须将其声明为 `const char* _Nonnull`。任何返回值无条件传递给 `_Nonnull` 参数的函数应将其返回值声明为 `const char* _Nonnull`——前提是它确实从不返回空。

## 7. 类型转换

使用 `-nullability-check=all` 时，在非安全区域中将可空转换为非空也会被检查：

```c
void foo() {
    int *p1 = nullptr;
    int *p2 = (int *_Nonnull)p1;   // 错误：可空到非空转换
    int *_Owned p3 = (int *_Owned)p1; // 错误
}
```

空检查后，允许转换：

```c
void foo() {
    int *p1 = nullptr;
    if (p1 != nullptr) {
        int *_Nonnull p2 = (int *_Nonnull)p1;  // 可以
        int *_Owned p3 = (int *_Owned)p1;       // 可以
    }
}
```

## 8. 结构体成员

```c
struct Data { int *_Borrow _Nullable value; };

_Safe void test(void) {
    int local = 10;
    // 初始化列表：从初始化器推断可空性
    struct Data data1 = {.value = bar(&_Mut local)};  // 非空
    *data1.value = 10;  // 可以

    // 非初始化列表：默认可空
    struct Data data2 = init_data(&_Mut local);  // 可空
    *data2.value = 10;  // 错误

    // 修复：重新赋值或空检查
    data2.value = bar(&_Mut local);  // → 非空
    *data2.value = 10;               // 可以

    if (data3.value != nullptr)
        *data3.value = 10;           // 可以
}
```

## 9. 编译器选项

`-nullability-check=<mode>`：

| 模式 | 行为 |
|------|----------|
| `safeonly`（默认） | 仅在 `_Safe` 区域中检查 |
| `all` | 在所有代码中检查（安全和非安全） |

没有该选项时，行为等同于 `-nullability-check=safeonly`。

> 关于所有权和 `_Owned _Nullable`，请参见 `bsc-ownership` 技能
> 关于借用和 `_Borrow _Nullable`，请参见 `bsc-borrowing` 技能
> 关于安全区域，请参见 `bsc-safe-zone` 技能
> 关于可空性错误（BSC-E04xx），请参见 `bsc-errors` 技能
