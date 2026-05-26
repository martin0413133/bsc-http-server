---
name: bsc-safe-zone
description: "BiSheng C 安全区域。当你需要理解 _Safe 函数、_Safe 块、_Unsafe 逃逸块、安全区域限制（初始化、指针、类型转换、枚举/浮点转换、++/-- 语义）、trait/泛型安全、成员函数安全或混合安全/不安全模式时，使用此技能。"
---

# BiSheng C 安全区域技能

## 内部有 `_Unsafe { ... }` 的函数仍然算 `_Safe`

`_Safe` 和 `_Unsafe { ... }` 并不冲突；`_Unsafe` 块的全部意义就是给函数一个局部逃逸。不要因为函数体需要一两行逃逸代码就将其从 `_Safe` 降级为 `_Unsafe`。`_Unsafe` 是传染性的——将函数设为 `_Unsafe` 会强制每个调用者也变成 `_Unsafe`。

## `_Unsafe` 块必须最小化

只包装真正需要逃逸的语句，不要多包。检查你放在 `_Unsafe { ... }` 内部的每一行：如果它在 `_Safe` 中可以正常编译，就把它移出去。

```c
// 不好——臃肿的 _Unsafe 块，只有赋值需要逃逸
_Safe void f(uint8_t *buf, size_t i, uint8_t v) {
    _Unsafe {
        if (i >= cap) return;
        validate(v);
        buf[i] = v;
        log("wrote byte");
    }
}

// 好——只有原始下标是 _Unsafe
_Safe void f(uint8_t *buf, size_t i, uint8_t v) {
    if (i >= cap) return;
    validate(v);
    _Unsafe buf[i] = v;
    log("wrote byte");
}
```

`_Unsafe stmt;`（单语句，无大括号）通常是正确的形式。

## `_Unsafe` 块必须附带理由注释

每个 `_Unsafe` 块（或语句）都需要一个内联注释，说明为什么 `_Safe` 不够。这使得 `_Unsafe` 接缝可审计——审阅者可以验证每次逃逸是合法的，未来的维护者可以判断如果编译器改进，某次逃逸是否可以被消除。

```c
// _Unsafe：变参 C 函数——无法声明为 _Safe
_Unsafe { fprintf(stderr, "error: %s\n", msg); }

// _Unsafe：预处理宏——无法声明为 _Safe
_Unsafe { SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY); }

// _Unsafe：跨结构体指针转换（sockaddr_in -> sockaddr）
_Unsafe { if (bind(fd, (struct sockaddr*)(void*)&_Mut addr, sizeof(addr)) < 0) ... }

// _Unsafe：__move_to_raw / __take_from_raw（所有权绕过内置函数）
_Unsafe { ctx.config = (const Config* _Nonnull)__move_to_raw(cfg); }

// _Unsafe：跨线程边界的 void* 类型擦除（非平凡指向类型）
_Unsafe { struct ServerCtx* ctx = (struct ServerCtx*)ctx_raw; }

// _Unsafe：原始 malloc/realloc/free——safe_malloc_array 无法处理有 _Owned 字段的类型
_Unsafe { r->routes_data = (Route*)malloc(cap * sizeof(Route)); }
```

不要写不带注释的 `_Unsafe { ... }`。如果你说不清为什么该块需要 `_Unsafe`，那它很可能不需要。

## 1. 概述

指定编译器强制内存安全的代码区域。默认上下文是 `_Unsafe`（标准 C 兼容性）。使用 `_Safe` 来选择严格的检查。

## 2. 语法

`_Safe` / `_Unsafe` 可以修饰：函数声明、函数定义、函数签名、函数指针、语句和带括号的表达式。

```c
_Safe int add(int a, int b) { return a + b; }  // 安全函数

void example(void) {
    _Safe { int x = 10; }         // 安全块
    _Safe int y = 1;              // 安全语句
}

_Safe void process(const int *_Borrow v) {
    _Unsafe { printf("%d\n", *v); }  // 不安全逃逸（printf 是变参）
    _Unsafe int c = 1;               // 不安全语句
    char d = _Unsafe((char)c);       // 不安全表达式
}
```

不能在以下内容上使用 `_Safe`/`_Unsafe`：全局变量、函数外的类型声明或 `typedef`（函数指针 typedef 除外）。

## 3. 限制（编译器强制）

在 `_Safe` 区域中：

### 指针操作
- **没有 `&` 取地址**——使用 `&_Const` 或 `&_Mut` 进行借用。例外：取函数地址是允许的。
- **没有原始指针解引用**（`*rawptr`、`rawptr->field`）——`_Owned` 和 `_Borrow` 指针解引用可以
- **允许 `T *_Owned _ArrayElem` / `T *_Borrow _ArrayElem` 下标**（`p[i]` 读取和写入）——不需要 `_Unsafe`。这是指针算术在 `_Safe` 中被禁止的一般规则的例外：编译器允许对 `_ArrayElem` 指针使用 `[]`，即使 `p + i` 和 `p++` 仍然是错误。
- **没有指针类别转换**——不允许在 `_Owned`/`_Borrow`/原始指针之间转换，没有指针到整数或整数到指针的转换。例外：`T *_Owned` 可以显式转换为 `void *_Owned`。
- **不允许指向不同类型之间的指针转换**
- **需要 `nullptr`**——`NULL` 在安全区域中被禁止；使用 `nullptr` 初始化或比较指针

#### 指针转换矩阵

| 转换 | 在 `_Safe` 中 | 在 `_Unsafe` 中 |
|---|---|---|
| `T *_Borrow` → `void *_Borrow`（T 是平凡数据：无指针，不是 `_Owned struct`） | **可以**（隐式） | 可以 |
| `T *_Borrow` → `void *_Borrow`（T 有指针字段 / 是 `_Owned struct`） | **禁止**（即使有显式转换） | 禁止 |
| `void *_Borrow` → `T *_Borrow` | **禁止**——需要显式转换，必须在 `_Unsafe` 中 | 可以（显式转换） |
| `T *_Borrow _ArrayElem` → `T *_Borrow` | 可以（隐式） | 可以 |
| `T *_Borrow` → `T *_Borrow _ArrayElem` | 禁止 | 禁止 |
| `T *_Borrow` → 原始 `T *` | **到处禁止** | 到处禁止 |
| `T *_Owned` → `void *_Owned` | 可以（显式转换） | 可以 |
| `void *_Owned` → `T *_Owned` | 在 `_Safe` 中禁止；在 `_Unsafe` 中允许，但结果结构体的内部 `_Owned` 成员**不**拥有任何东西 | 可以（显式转换，相同警告） |
| `T *_Owned` ↔ `T *_Owned _ArrayElem`（C 转换） | 禁止——这些是不同的类别；使用 `safe_malloc_array`/`__take_array_from_raw` | 禁止 |
| `T *_Owned` → 原始 `T *` | 禁止——使用 `__move_to_raw` | 禁止——使用 `__move_to_raw` |
| `T *_Owned _ArrayElem` → 原始 `T *` | 禁止——使用 `__move_array_to_raw` | 禁止——使用 `__move_array_to_raw` |
| `fn(A *_Borrow)` → `fn(B *_Borrow)`（fn-ptr 转换） | 在异构 `_Safe`/`_Unsafe` 声明中禁止——使用 trampoline 模式 | 在异构声明中禁止——使用 trampoline 模式 |

**void-borrow 两步转换**是在 `_Safe` 函数内部在不同结构体指针类型之间转换的规范模式（例如，vtable trampolines——参见 `c-to-bsc` §5 "C OO patterns"）：

```c
/* 目标：public_t *_Borrow → private_t *_Borrow */
private_t *_Borrow _Nonnull this = _Unsafe(
    (private_t *_Borrow _Nonnull)(void *_Borrow _Nonnull)self);
```

- 第 1 步：`public_t *_Borrow → void *_Borrow`——隐式，在 `_Safe` 中允许。
- 第 2 步：`void *_Borrow → private_t *_Borrow`——显式转换，包装在 `_Unsafe(expr)` 中。
- 直接的 `public_t *_Borrow → private_t *_Borrow` 被**禁止**（不同的指向类型）。

### 原始指针参数/返回值在签名中是允许的
`_Safe` 函数**可以**有原始指针参数/返回值，并且可以有带指针成员的结构体或联合体类型作为参数/返回值。限制适用于安全区域内对指针的**操作**，而不是它们在签名中的存在。

### 初始化规则
- **指针类型**（原始、`_Owned`、`_Borrow`、函数指针）**必须**初始化（如果需要，使用 `nullptr`）
- **包含指针字段的结构体/联合体**必须使用**完整**的初始化器列表进行初始化（不能部分初始化）
- **基本类型**（`int`、`float`、`char`、`_Bool`）和**没有**指针字段的结构体/联合体**可以**保持未初始化或部分初始化

```c
_Safe {
    int a;                            // 可以：基本类型
    int *p;                           // 错误：指针必须初始化
    int *p1 = nullptr;                // 可以
    struct HasPtr hp = {nullptr, 0};  // 可以：完整初始化
    struct HasPtr hp2 = {0};          // 错误：部分初始化（有指针字段）
}
```

#### `_Owned struct` 必须使用聚合设计初始化——绝不能使用裸声明符

一个 `_Owned struct` 总是包含 `_Owned` 字段（直接或通过 `String`/`Vec` 成员）。裸声明符在安全区域中总是被拒绝：

```c
// 错误——"uninitialized declarator is forbidden in the safe zone"
Request r;

// 正确——通过聚合设计初始化提供所有字段
Request r = {
    .method  = String::new(),
    .path    = String::new(),
    .headers = Vec<Header>::new(),
    .body    = String::new(),
    .ok      = 0
};
```

所有 `_Owned` 子字段必须在大括号列表内进行移动初始化。你不能先构造结构体再赋值字段——见下面的直接规则。

#### 构造后不能重新赋值 `_Owned` 结构体字段；改用 `safe_swap`

在 `_Owned struct` 初始化后，对 `_Owned` 字段进行赋值会被拒绝：

```c
Config c = { .name = String::new(), ... };
String newname = build_name();
c.name = newname;   // 错误——"assign to part of _Owned value"
```

两种合法的替代方案：

**A — 在构造时移动**（当值在结构体构建之前已知时首选）：

```c
String name = build_name();
Config c = { .name = name, ... };  // `name` 被移入；之后 `name` 已失效
```

**B — 构造后替换使用 `safe_swap`**（来自 `bishengc_safety.hbs`）：

```c
String newname = build_name();
safe_swap(&_Mut c.name, &_Mut newname);
// 原地交换；旧值落入 `newname`，在作用域结束时析构
```

`safe_swap<T>(T* _Borrow left, T* _Borrow right)` 交换两个拥有的值而不进行直接的赋值语句，安全区域规则允许这样做。

### 自增/自减（`++`/`--`）
`++` 和 `--` 是**允许的**，但它们的返回类型是 `void`。你可以将它们用作独立语句，但不能使用表达式的值。

```c
_Safe void foo(void) {
    int a = 0;
    a++;                // 可以：仅副作用
    int x = a++;        // 错误：结果是 void
    for (int i = 0; i < 10; i++) {}  // 可以：迭代子句
}
```

### 类型转换规则
- **没有隐式窄化转换**（`long` 到 `int`、`double` 到 `float`、`int` 到 `float`）——需要显式转换
- 适合目标类型的**编译时常量**不受窄化限制
- **没有浮点到整数的转换**（即使是显式的）
- **整数到浮点**的转换在安全区域中显式允许
- **枚举转换**：隐式枚举到枚举被禁止；只有目标枚举包含源枚举的所有值时才允许显式转换。隐式枚举到底层整数是允许的
- **比较/逻辑操作符**（`==`、`!=`、`>=`、`<=`、`>`、`<`、`&&`、`||`、`!`）：结果是 `int` 类型（0 或 1）并且可以隐式转换为其他整数类型
- **if/while 条件**：允许任何算术类型（遵循 C 规则）

### 联合体规则
- **没有联合体成员访问**（`.` 读取或写入）——但联合体可以被声明、初始化和作为参数传递

### 其他限制
- **没有内联汇编**
- **不能调用不安全函数**——必须包装在 `_Unsafe {}` 中
- **空参数需要 `void`**：`_Safe void f(void)`——不是 `_Safe void f()`
- **没有变参**——除非函数有 `__attribute__((format(...)))`：
  ```c
  _Safe int foo(int a, ...);  // 错误
  __attribute__((format(printf, 1, 2)))
  _Safe int bar(const char *fmt, ...);  // 可以（但 va_start/va_arg/va_end 在函数体内仍被禁止）
  ```
- **Switch**：`case`/`default` 只能在 `switch` 后的第一级块中；该第一级块中不能有变量声明

## 4. Trait 和泛型安全规则

- 声明为 `_Safe` 的 `_Trait` 函数要求实现函数也是 `_Safe`；如果 trait 函数不是 `_Safe`，实现可以是 `_Safe`（编译器会警告）
- `_Safe` 泛型函数：所有实例化都进行安全检查
- 成员函数也可以用 `_Safe`/`_Unsafe` 修饰，规则与全局函数相同

## 5. 混合模式声明（_Safe/_Unsafe 重载）

同一个函数可以同时有 `_Safe` 和 `_Unsafe` 声明：

```c
_Unsafe int* foo(int* p);              // 不安全版本
_Safe int* _Owned foo(int* _Owned p);  // 安全版本：添加 _Owned
```

- `_Safe` 声明可以**添加** `_Owned`、`_Borrow`、`_Owned _ArrayElem` 或 `_Borrow _ArrayElem` 到原始指针参数/返回值。`_Owned _ArrayElem` 和 `_Borrow _ArrayElem` 作为**完整单元**添加——你不能跨声明将普通 `_Owned` 升级为 `_Owned _ArrayElem`。
- 必须**不删除** `_Unsafe` 声明中存在的限定词，也不能将 `_Owned` 与 `_Borrow` 互换（反之亦然）。**返回类型**上的标准 C 限定词（`const`、`volatile` 等）也必须保留；**参数类型**上的为了兼容性会被剥离。
- 在安全上下文中，只有 `_Safe` 重载可以调用。在不安全上下文中，当类型匹配时，优先选择 `_Safe` 版本。
- **泛型函数不支持混合模式**
- 如果函数有多个相同安全级别的声明，它们必须一致

## 6. 函数指针规则

- `_Safe` 函数指针只能从有 `_Safe` 声明的函数赋值
- `_Unsafe` 函数指针可以从 `_Safe` 或 `_Unsafe` 函数赋值（如果类型兼容）

```c
_Safe void safe_fn(void);
_Unsafe void unsafe_fn(void);

_Safe void (*sp)(void) = nullptr;
sp = safe_fn;    // 可以
sp = unsafe_fn;  // 错误：没有可用的 _Safe 声明
```

## 7. 完整示例

```c
#include <stdio.h>
#include "bishengc_safety.hbs"

_Safe int add(int a, int b) { return a + b; }

_Safe int readBorrow(const int *_Borrow ref) {
    return *ref;  // 可以：_Borrow 解引用是安全的
}

void mixedFunction(void) {
    int *raw = (int *)malloc(sizeof(int));
    *raw = 100;
    _Safe {
        int x = 42;
        int y;  // 可以：基本类型，不需要初始化
        const int *_Borrow ref = &_Const x;
        y = readBorrow(ref);
        _Unsafe {
            printf("from safe zone: %d\n", y);
            free(raw);
        }
    }
}

_Safe int main(void) {
    int sum = add(10, 20);
    const int *_Borrow r = &_Const sum;
    _Unsafe { printf("sum = %d\n", *r); }
    return 0;
}
```

> 关于初始化分析（字段级追踪），请参见 `bsc-initialization` 技能
> 关于借用（安全区域中需要），请参见 `bsc-borrowing` 技能
> 关于安全上下文中的所有权，请参见 `bsc-ownership` 技能
> 关于可空性检查，请参见 `bsc-nullability` 技能
> 关于安全区域错误（BSC-E03xx），请参见 `bsc-errors` 技能
> 关于常见安全区域错误，请参见 `bsc-common-mistakes` 技能
