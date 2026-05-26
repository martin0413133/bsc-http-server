---
name: bsc-initialization
description: "BiSheng C 初始化分析。当你需要理解未初始化变量检测、字段级初始化追踪、__attribute__((ensure_init))、__assume_initialized、数组初始化规则或 -uninit-check 编译器选项时，使用此技能。"
---

# BiSheng C 初始化分析技能

## 1. 概述

BSC 执行编译时数据流分析，确保变量在使用前已初始化。分析在**结构体字段级别**追踪初始化。默认情况下在 `_Safe` 区域中激活；可通过 `-uninit-check` 配置。

## 2. 规则

### 规则 1：所有局部变量必须在使用前初始化

```c
_Safe void example(void) {
    int x;
    int y = x;  // 错误：使用未初始化的值：`x`
}
```

### 规则 2：结构体的字段级追踪

检测到部分字段初始化。当所有字段都初始化时，结构体自动提升为完全初始化。

```c
struct Pair { int a; int b; };

_Safe void partial(void) {
    struct Pair p;
    p.a = 1;
    struct Pair q = p;  // 错误：p.b 未初始化
}

_Safe void full(void) {
    struct Pair p;
    p.a = 1;
    p.b = 2;           // 所有字段完成 → p 自动提升
    struct Pair q = p;  // 可以
}
```

### 规则 3：所有控制流路径都必须初始化

```c
_Safe void example(int cond) {
    int x;
    if (cond) { x = 1; }
    // else 路径：x 未初始化
    int y = x;  // 错误：使用可能未初始化的值：`x`
}
```

### 规则 4：取地址也是一种使用

对未初始化变量进行 `&`、`&_Mut` 或 `&_Const` 操作是错误。例外：`ensure_init` 参数和 `__assume_initialized` 参数。

```c
_Safe void example(void) {
    int x;
    int *_Borrow p = &_Mut x;  // 错误：使用未初始化的值：`x`
}
```

### 规则 5：函数必须在所有路径上初始化返回值

```c
_Safe int example(int cond) {
    if (cond) { return 1; }
}  // 错误：返回值可能未在所有路径上初始化
```

### 规则 6：数组元素逐个赋值不会初始化

逐个元素的赋值不会将数组标记为已初始化。数组必须使用初始化列表或 `__assume_initialized`。

```c
_Safe void bad(void) {
    int arr[3];
    arr[0] = 1; arr[1] = 2; arr[2] = 3;
    int x = arr[0];  // 错误：arr 仍未初始化
}

_Safe void good(void) {
    int arr[3] = {1, 2, 3};  // 可以：初始化列表
    int x = arr[0];
}

_Safe void assume(void) {
    int arr[3];
    arr[0] = 1; arr[1] = 2; arr[2] = 3;
    _Unsafe { __assume_initialized(&arr); }
    int x = arr[0];  // 可以
}
```

这也适用于结构体内部的数组字段：

```c
typedef struct { int a[2]; int b; } ArrStruct;

_Safe void bad(void) {
    ArrStruct s;
    s.a[0] = 1; s.a[1] = 2; s.b = 3;
    ArrStruct t = s;  // 错误：s.a 未初始化
}

_Safe void good(void) {
    ArrStruct s = {{1, 2}, 3};
    ArrStruct t = s;  // 可以
}
```

### 规则 7：联合体——写入任何成员即可初始化整个联合体

```c
// 使用 -uninit-check=all 编译（安全区域中禁止联合体访问）
union U { int a; float f; };

void example(void) {
    union U u;
    u.a = 42;
    float f = u.f;  // 可以：通过 u.a 初始化了整个联合体
}
```

在联合体变体内写入结构体成员也会初始化整个联合体。已知限制：跨变体读取未写入的字节不会报错。

### 规则 8：嵌套结构体——任意深度的追踪

```c
struct Inner { int x; int y; };
struct Outer { struct Inner inner; int z; };

_Safe void example(void) {
    struct Outer o;
    o.inner.x = 1;
    o.inner.y = 2;  // inner 自动提升
    o.z = 3;        // o 自动提升
    struct Outer p = o;  // 可以
}
```

### 规则 9：全局和静态变量隐式初始化

```c
static int global_count;

_Safe void example(void) {
    int x = global_count;  // 可以：C 规范保证零初始化
}
```

## 3. `__attribute__((ensure_init))`

一个参数属性，建立初始化契约：
- **调用方**：调用后，`*param` 被标记为已初始化
- **被调用方**：编译器验证 `*param` 在所有返回路径上均已初始化

```c
void init_int(int *__attribute__((ensure_init)) out);

_Safe void caller(void) {
    int x;
    _Unsafe { init_int(&x); }  // 调用后 x 标记为已初始化
    int y = x;                  // 可以
}

// 编译器验证契约：
void good_init(int *__attribute__((ensure_init)) out) {
    *out = 42;  // 可以
}

void bad_init(int *__attribute__((ensure_init)) out) {
}  // 错误：ensure_init 参数 'out' 在返回时未初始化
```

### 字段级部分初始化

```c
struct Pair { int a; int b; };
void init_field(int *__attribute__((ensure_init)) p);

_Safe void example(void) {
    struct Pair p;
    _Unsafe {
        init_field(&p.a);  // 只标记 p.a
        init_field(&p.b);  // p.b 已标记 → p 自动提升
    }
    struct Pair q = p;  // 可以
}
```

### 在安全区域中的使用

```c
_Safe void init_safe(int *_Borrow __attribute__((ensure_init)) out);

_Safe void caller(void) {
    int x;
    init_safe(&_Mut x);  // 在安全区域中使用 &_Mut 可以
    int y = x;            // 可以
}
```

### `*param` 初始化前的限制

在履行契约之前，不能重新赋值或别名 `ensure_init` 指针：

```c
void bad(int *__attribute__((ensure_init)) out) {
    int local;
    out = &local;   // 错误：在 *out 初始化前不能重新赋值
}

void bad2(int *__attribute__((ensure_init)) out) {
    int *p = out;   // 错误：在 *out 初始化前不能别名
}
```

初始化后，可以自由使用：

```c
void ok(int *__attribute__((ensure_init)) out) {
    *out = 42;       // 契约已履行
    int *p = out;    // 可以：*out 已初始化
    out = &local;    // 可以
}
```

### 委托

`ensure_init` 可以委托给另一个 `ensure_init` 函数：

```c
void init_val(int *__attribute__((ensure_init)) out);

void init_delegated(int *__attribute__((ensure_init)) out) {
    init_val(out);  // 可以：委托给另一个 ensure_init
}
```

### 重新声明规则

相同安全级别的重新声明必须一致（两者都有 `ensure_init` 或都没有）。不同的安全级别（`_Safe` vs 非安全）是独立的重载，允许存在差异。

### 函数指针兼容性

`ensure_init` 是函数类型的一部分。不能将非 `ensure_init` 函数赋值给 `ensure_init` 函数指针：

```c
typedef _Safe void (*InitFn)(int *__attribute__((ensure_init)) _Borrow out);

_Safe void has_attr(int *__attribute__((ensure_init)) _Borrow out) { *out = 1; }
_Safe void no_attr(int *_Borrow out) { *out = 1; }

_Safe void test(void) {
    InitFn fn = has_attr;  // 可以
    // InitFn fn2 = no_attr;  // 错误：缺少 ensure_init
}
```

通过函数指针的间接调用支持 `ensure_init` 追踪：

```c
_Safe void indirect_call(InitFn fn) {
    int x;
    fn(&_Mut x);   // 通过 ensure_init 将 x 标记为已初始化
    int y = x;     // 可以
}
```

## 4. `__assume_initialized`

内置函数，用于在程序点将变量标记为已初始化。没有契约验证——用户保证正确性。

```c
_Safe void example(void) {
    int x;
    _Unsafe { __assume_initialized(&x); }
    int y = x;  // 可以
}
```

- 必须使用 `&` 前缀：`__assume_initialized(&x)`（不是 `__assume_initialized(x)`）
- 仅在 `_Unsafe` 块中
- 路径敏感：仅在其执行的 CFG 路径上有效
- 每次调用一个变量
- 对于数组：`__assume_initialized(&arr)`（由于数组退化而需要）
- 对于结构体：将所有字段标记为已初始化

### 支持的参数形式

取地址操作数必须是以下之一：

| 形式 | 含义 |
|------|---------|
| `&x` | 局部变量；如果 `x` 是 `ensure_init` 指针参数，也标记 `*x` 已初始化 |
| `&x.f.g...` | 局部结构体字段，任意嵌套（纯字段路径） |
| `&*p` | `ensure_init` 参数 `p` 指向的对象 |
| `&p->f.g...` | `ensure_init` 参数指向的对象的字段（纯字段路径） |

当 `*p` 的所有字段通过 `&p->...` 标记后，`*p` 会自动提升为完全初始化。

```c
struct Pair { int a; int b; };
void assume_through_ptr(struct Pair *__attribute__((ensure_init)) out) {
    out->a = 1;
    _Unsafe { __assume_initialized(&out->b); }   // 所有字段覆盖 → *out 已初始化
}
```

### **路径中不允许使用数组下标（`[i]`）**

初始化分析将数组视为**单一单元**——不追踪单个元素，因此你只能假设整个数组，不能假设单个元素。

```c
struct WithArr { int arr[3]; };
struct Outer   { struct WithArr s[2]; };

_Safe void example(void) {
    struct WithArr w;
    _Unsafe { __assume_initialized(&w.arr); }       // 可以：整个数组字段

    // _Unsafe { __assume_initialized(&w.arr[0]); }     // 错误：路径中有下标
    // _Unsafe { __assume_initialized(&o.s[0].arr); }   // 错误：中间有下标
    // _Unsafe { __assume_initialized(&o.s[0].arr[0]); }// 错误：末尾有下标
}
```

```c
// 路径敏感：
_Safe void example(int cond) {
    int x;
    if (cond) { _Unsafe { __assume_initialized(&x); } }
    int y = x;  // 错误：可能未初始化（并非所有路径）
}
```

## 5. 编译器选项

`-uninit-check=<mode>`：

| 模式 | 行为 |
|------|----------|
| `none` | 禁用初始化分析 |
| `safeonly`（默认） | 仅在 `_Safe` 区域中检查；`ensure_init` 契约验证始终激活 |
| `all` | 在所有代码中检查（包括非安全） |

注意：只要模式不是 `none`，`ensure_init` 契约验证即使在安全区域外也是激活的。

> 关于安全区域规则，请参见 `bsc-safe-zone` 技能
> 关于所有权初始化模式，请参见 `bsc-ownership` 技能
> 关于常见初始化错误，请参见 `bsc-common-mistakes` 技能
