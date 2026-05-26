---
name: bsc-overview
description: "BiSheng C 语言概述。当你需要了解 BiSheng C 是什么、其文件类型（.cbs/.hbs）、编译器用法或获取所有 BSC 特性和关键字的概览时，使用此技能。"
---

# BiSheng C 概述技能

## 1. 什么是 BiSheng C

BiSheng C（BSC）是 **C 的超集**，具有 Rust 启发的内存安全、traits、泛型和 async/await。所有有效的 C 代码都是有效的 BSC 代码。

- 源文件：`.cbs`（或使用 `-x bsc` 编译的 `.c`）
- 头文件：`.hbs`（或使用 `-x bsc` 编译的 `.h`）
- 编译器：clang 分支——`clang file.cbs -o output`，或 `clang -x bsc file.c -o output` 将 `.c` 文件视为 BSC

## 关键语法规则

`_Owned` 和 `_Borrow` 是**指针限定词**——它们放在 `*` **之后**，绝不在类型之前：
- 正确：`char *_Owned p = (char *_Owned)malloc(n);`
- 正确：`const int *_Borrow r = &_Const x;`
- 错误：`_Owned char* p = malloc(n);` ——无法编译
- 错误：`_Borrow int* r = ...;` ——无法编译

`malloc()` 返回原始 `void*`，必须进行转换：`(T *_Owned)malloc(...)`。

## 2. 特性概览

| 特性 | 关键字 | 描述 |
|---------|----------|-------------|
| 成员函数 | `TypeName::method`、`this`、`This` | 为任何类型（结构体、基本类型等）附加方法 |
| 泛型 | `<T>`、`<T, int N>` | 编译时单态化、常量泛型、类型别名 |
| Traits | `_Trait`、`_Impl`、`This*` | 带有虚表动态分发的接口抽象 |
| 操作符重载 | `__attribute__((operator OP))` | 为用户定义类型重载操作符 |
| 所有权 | `_Owned`、`_ArrayElem`、`_Nullable`、`_Nonnull`、`_Public` | 移动语义、RAII 析构函数、编译时内存安全 |
| 借用 | `_Borrow`、`_ArrayElem`、`&_Const`、`&_Mut` | 带有生命周期强制的非拥有性引用 |
| 可空性 | `_Nullable`、`_Nonnull`、`nullptr` | 编译时空安全检查 |
| 安全区域 | `_Safe`、`_Unsafe` | 编译器强制内存安全区域 |
| 初始化分析 | `__attribute__((ensure_init))`、`__assume_initialized` | 字段级未初始化变量检测 |
| 协程 | `_Async`、`_Await`、`Future` | 基于轮询的无栈协程 |
| 调度器 | `Scheduler::init/spawn/run/destroy` | 线程池并发任务执行 |
| 常量表达式 | `constexpr`、`_Static_assert`、`if constexpr` | 编译时求值和类型特化分支 |
| 类型 traits | `is_integral<T>()`、`is_pointer<T>()` 等 | 编译时类型分类 |
| 标准库 | `Vec<T>`、`String`、`Option<T>`、`Result<T>` | 带有 RAII 的安全容器 |

## 3. 快速语法汇总

| 特性 | 语法 |
|---------|--------|
| 源文件/头文件 | `.cbs` / `.hbs` |
| 成员函数 | `RetType TypeName::method(TypeName* this, ...)` |
| 静态成员 | `RetType TypeName::method(...)`（无 this） |
| 泛型函数 | `T func<T>(T arg)` |
| 泛型结构体 | `struct S<T> { T field; };` |
| 常量泛型 | `struct A<T, int N> { T data[N]; };` |
| 类型别名（泛型） | `typedef Alias<T> = OldType<T>;` |
| 条件类型别名 | `conditional<C, T, F>`（来自 `bsc_conditional.hbs`） |
| Trait 定义 | `_Trait Name { RetType method(This* this); };` |
| Trait 实现 | `_Impl _Trait Name for TypeName;` |
| Trait 指针 | `_Trait Name* ptr = &value;` |
| 操作符重载 | `__attribute__((operator+)) RetType func(...)` |
| 拥有指针 | `T *_Owned`（**不是** `_Owned T*`——限定词放在 `*` 之后） |
| 拥有的结构体 | `_Owned struct S { _Public: ...; ~S(S this) {...} };`（必须在文件作用域） |
| 拥有的数组指针 | `T *_Owned _ArrayElem`——支持 `[]` 的 `_Owned` 指针（参见 `bsc-ownership`） |
| 不可变借用 | `const T *_Borrow ref = &_Const value;`（**不是** `_Borrow T*`） |
| 可变借用 | `T *_Borrow ref = &_Mut value;` |
| 借用到数组 | `T *_Borrow _ArrayElem p = &_Mut arr[i];`——支持 `[]`、`+`、`-`、`++`、`--` |
| 可空指针 | `T *_Owned _Nullable p = nullptr;` |
| 非空指针 | `T *_Nonnull p = &value;` |
| 安全函数 | `_Safe RetType func(ParamType param) { ... }` |
| 不安全块 | `_Unsafe { ... }` |
| 异步函数 | `_Async RetType func(...) { ... }` |
| Await | `T result = _Await asyncFunc();` |
| constexpr if | `if constexpr (expr) { ... }` |
| 析构函数 | `~TypeName(TypeName this) { ... }`（在 `_Owned struct` 内部） |
| ensure_init | `void f(int *__attribute__((ensure_init)) out)` |
| assume init | `_Unsafe { __assume_initialized(&x); }` |

## 4. 技能索引

> 关于成员函数，请参见 `bsc-member-function` 技能
> 关于泛型，请参见 `bsc-generic` 技能
> 关于 traits，请参见 `bsc-trait` 技能
> 关于操作符重载，请参见 `bsc-operator-overloading` 技能
> 关于所有权，请参见 `bsc-ownership` 技能
> 关于借用，请参见 `bsc-borrowing` 技能
> 关于可空性，请参见 `bsc-nullability` 技能
> 关于安全区域，请参见 `bsc-safe-zone` 技能
> 关于初始化分析，请参见 `bsc-initialization` 技能
> 关于协程，请参见 `bsc-coroutine` 技能
> 关于 constexpr，请参见 `bsc-constexpr` 技能
> 关于标准库，请参见 `bsc-stdlib` 技能
> 关于编译，请参见 `bsc-compile` 技能
> 关于常见陷阱，请参见 `bsc-common-mistakes` 技能
> 关于错误码和诊断，请参见 `bsc-errors` 技能
