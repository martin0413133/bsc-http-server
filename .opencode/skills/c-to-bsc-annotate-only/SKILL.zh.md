---
name: c-to-bsc-annotate-only
description: "在强化现有/遗留 C 代码库时使用，同时它必须保持作为普通 C 编译（双构建），或者当你想要通过所有权注解获得编译时泄漏/释放后使用/双重释放检查，而无需重写数据结构、采用成员函数/泛型/traits/析构函数/libcbs 类型或失去 C 构建时使用。c-to-bsc 的保守子集，保持双构建。"
---

# C → BSC：仅注解（保持双构建）强化

## 概述

通过**仅向现有函数和结构体字段添加所有权注解**来强化遗留 C 代码。保持在 BSC 的**宏可擦除子集**内，使*同一源代码*仍然可以作为普通 C 编译（双构建）。你获得编译时泄漏/释放后使用/双重释放/借用检查，而**无需重写任何东西**。

**核心见解（已验证）：** 安全检查来自**注解 + 借用检查器**，而不是来自析构函数、成员函数、泛型或 libcbs。因此你可以跳过所有这些，仍然在编译时捕获 C 堆错误类别——同时保持 C 构建。

**所需的背景知识：** `c-to-bsc`（每个指针的注解机制、`__take_from_raw`/`__move_to_raw`）、`bsc-ownership`。本技能是 `c-to-bsc` 的*保守子集*：当完整翻译（libcbs、成员函数、`_Owned struct` + 析构函数）过于破坏性或者会导致失去 C 构建时，使用本技能。

## 何时使用

- 你必须在迁移期间（或之后）保持项目以**普通 C** 形式发布/构建。
- 你想要堆错误安全网，但**不能提交到 `.cbs`** 或重写。
- 代码库很大；你想要**最小差异、最大安全投资回报率**，增量进行。

**何时不使用：** 绿地代码（直接使用 `bsc-design` + libcbs）；当你想要自动 RAII / 高层容器并且不需要 C 构建时（使用完整 `c-to-bsc`）。

## 双构建子集——你可以使用什么 vs 什么会破坏 C 构建

| ✅ 允许（宏可擦除 → 双构建保持） | ❌ 禁止（BSC 专属语法 → C 构建破坏，每个文件） |
|---|---|
| 函数上的 `_Safe` / `_Unsafe`；`_Unsafe { }` 块 | 成员函数 `Type::method`，方法调用语法糖 `obj->m()` |
| 参数、返回值、**结构体字段**上的 `_Owned` / `_Borrow` / `const _Borrow` | 泛型：`Vec<T>`、`safe_malloc<T>`、任何 `<T>` |
| `_Nonnull` / `_Nullable`；`nullptr` | traits、`_Impl` |
| `&_Const` / `&_Mut` 取地址 | `~T()` 析构函数 / **带有析构函数体的** `_Owned struct` |
| 原始 `malloc`/`free`；`__move_to_raw`/`__take_from_raw` | libcbs 类型：`String`、`Vec`、`Rc` 等 |
| **重新声明**带有所有权的 extern/libc 函数（见下文） | |

本会话已验证：成员函数（`S::get`）和泛型调用（`safe_malloc<int>`）在 `-xc` 下都**失败**；裸 `_Owned` 在 `-xc` 下也失败（它不是 C）——这就是为什么下面的**垫片**对于 C 构建是必需的。

## 一个基础设施：垫片头文件

BSC 编译器预定义了 `__bishengc`（已验证：`#define __bishengc 1`；普通 clang **不**定义它）。以它作为垫片的开关，使得一个**单一的、无条件的 `#include`** 在两个构建中都是正确的：在 BSC 下，它引入 `bishengc_safety.hbs` 并将限定词保留为真正的关键字；在普通 C 下，它引入 `<stdlib.h>`，擦除限定词，并将分配宏映射到原始 `malloc`/`calloc`/`free`。一个头文件，没有按构建的 `-include`，不会泄漏到 BSC 构建中，也不会在 C 构建中被遗忘。`nullptr` → `((void*)0)`（没有 `<stddef.h>`/`NULL` 依赖）。

```c
/* bsc_shim.h — 在任何地方 #include 此文件；根据编译器自动激活 */
#ifdef __bishengc
#  include "bishengc_safety.hbs"                  /* safe_malloc / safe_free — 仅 BSC */
   /* libcbs 没有提供 safe_calloc；提供一个 — 5 行，镜像 safe_malloc，对任何 T 字节清零 */
   _Safe T *_Owned safe_calloc<T>(void) {
       _Unsafe {
           T *addr = (T *)calloc(1, sizeof(T));
           if (!addr) { bsc_bad_alloc_handler(sizeof(T)); }
           return __take_from_raw(addr);
       }
   }
#  define SAFE_MALLOC(T, init) safe_malloc<T>(init)    /* 分配 + 初始化 → T *_Owned (_Safe) */
#  define SAFE_CALLOC(T)       safe_calloc<T>()        /* 清零的 T *_Owned (_Safe) */
#  define SAFE_FREE(p)         safe_free((void *_Owned)(p))
#else
#  include <stdlib.h>                             /* malloc / free — 仅 C */
#  define _Safe
#  define _Unsafe
#  define _Owned
#  define _Borrow
#  define _Nonnull
#  define _Nullable
#  define _Const
#  define _Mut
#  define nullptr ((void*)0)
#  define __take_from_raw(p) (p)
#  define __move_to_raw(p)   (p)
#  define SAFE_MALLOC(T, init) ({ T *_s = (T *)malloc(sizeof(T)); if (_s) *_s = (init); _s; })
#  define SAFE_CALLOC(T)       ((T *)calloc(1, sizeof(T)))
#  define SAFE_FREE(p)         free(p)
#endif
```

在两个编译器下验证：在 `-x bsc` 下仍然捕获泄漏（限定词存活），同一源代码在 `-xc` 下干净编译（限定词擦除，宏回退到 malloc/calloc/free）。C 端的 `SAFE_MALLOC` 使用 GNU/clang 语句表达式（`({ … })`）在一个表达式中分配并初始化——在 gcc 和 clang 中可用（这里的工具链）。

## 首选的分配：`SAFE_MALLOC` / `SAFE_CALLOC` / `SAFE_FREE`

在 BSC 下，这些映射到 `safe_malloc<T>` / `safe_free`——两者都是 `_Safe`，因此**没有 `_Unsafe`，没有 `__take_from_raw`**。对于固定类型分配，优先选择它们而不是原始 `malloc`+`__take_from_raw`：

```c
int  *_Owned p = SAFE_MALLOC(int, 42);   /* 分配 + 初始化 */
Node *_Owned n = SAFE_CALLOC(Node);      /* 清零（垫片提供了一个 5 行的 safe_calloc<T>——libcbs 没有） */
SAFE_FREE(p);                            /* 消耗——泄漏/UAF 仍然被检查 */
```

> **大的 `T`？不要使用 `SAFE_MALLOC`。** `safe_malloc<T>(T t)` **按值**接受初始化值，因此整个 `T` 被具体化在栈上（已验证：1 MB 的 `T` → ~2 MB 调用者帧**加** ~1 MB 在 `safe_malloc` 内部），有栈溢出风险。对于大的 `T`（大的内联缓冲区/数组）：使用 `SAFE_CALLOC(T)` ——没有值复制，~24 B 帧——当清零可接受时，或者使用原始 `malloc` + 通过 `_Unsafe` 中的指针初始化字段（没有完整的 `T` 栈临时变量）。使用 `-Wframe-larger-than=N` 构建以捕获过大的帧。

**这同时也解决了拥有的字段堆结构体问题。** 你不能在安全区域中逐个赋值 `_Owned` 字段，但你可以聚合初始化一个值，然后让 `SAFE_MALLOC` 将其移动到堆上——**纯 `_Safe`，没有 `_Unsafe` 块**：

```c
typedef struct Entry { char *_Owned _Nullable key; char *_Owned _Nullable val; } Entry;
_Safe Entry *_Owned _Nullable entry_new(const char *_Borrow k, const char *_Borrow v) {
    Entry e = { .key = xstrdup(k), .val = xstrdup(v) };  /* 在 _Safe 中聚合初始化 */
    return SAFE_MALLOC(Entry, e);                         /* 移动到堆上——没有 _Unsafe */
}
```

已验证：构造 + 拥有的字段拆除（释放每个字段，然后 `SAFE_FREE`）在纯 `_Safe` 中干净编译且双构建；泄漏变体在 `-x bsc` 下被捕获。仅对**变长 / 数组**分配回退到原始 `malloc`+`__take_from_raw`（在 `_Unsafe` 中）（`safe_malloc_array<T>(n, init)` 是 BSC 的数组对应物）。

## 重新声明外部配对的资源（不需要包装器）

对于**指针类型**的 C 资源，重新声明获取/释放对并带上所有权——检查器然后直接追踪原始 C 指针。没有包装器结构体，调用点是 `_Safe`：

```c
_Safe FILE *_Owned _Nullable fopen(const char *_Nonnull, const char *_Nonnull);
_Safe int                    fclose(FILE *_Owned stream);   /* 消耗 == 释放 */
```

现在 `fopen` → 必须在每条路径上恰好调用一次 `fclose`（否则编译时泄漏），并且 `f` 在 `fclose` 后不可用（编译时释放后使用）。在垫片下，这些擦除为标准原型。（`free`、`SSL_CTX_new`/`SSL_CTX_free` 等遵循相同的形式。）

> **不要将返回 `void*` 的分配器（`malloc`/`calloc`/`realloc`）重新声明为 `_Owned`**——将 `void *_Owned` 转换为有类型的 `T *_Owned` 在安全区域中是**禁止**的（已验证）。对于分配，使用上面的 `SAFE_MALLOC`/`SAFE_CALLOC` 宏（首选，`_Safe`）；只有在必须调用原始 `malloc`（变长/数组）时才保持其原始并在最小的 `_Unsafe { }` 块内使用 `__take_from_raw` 桥接。带 `_Owned` 的重新声明适用于返回**最终有类型**指针的 API（`fopen`→`FILE*`、`strdup`→`char*`）。

> **`int` 文件描述符是例外：** `_Owned` 是一个*指针*限定词——`int` fd（`open`/`accept` → `close`）不能携带它。重新声明无法帮助；要么让 fd 不受追踪，要么将其包装在 `_Owned struct Fd { int fd; ~Fd … }` 中——但那个析构函数是 BSC 专属的，并且会**破坏该文件的双构建**。逐情况决定。

## 工作流程

1. **阶段 0——使其作为 BSC 构建，不做更改。** 使用 `clang -x bsc` 编译每个 TU（BSC 是 C 的超集；普通 C 通常以**零**注解干净编译）。只修复：与 BSC 关键字冲突的标识符（例如 `This`），以及罕见的 `-xc`-vs-`-xbsc` 一致性错误。在 BSC 构建上运行测试套件 → 行为必须等同于 C 构建。
2. **阶段 1——重新声明外部配对资源 API**，带上所有权（如上）。
3. **阶段 2——自底向上注解签名 + 结构体字段（先叶模块）。** 将每个函数标记为 `_Safe`；给每个指针参数/返回值赋予 `_Owned`/`_Borrow`/`const _Borrow`（或有理由的故意原始）；注解拥有所有权的结构体字段为 `_Owned`。检查器**强制**在每条路径上正确释放。在每个文件后编译（BSC）+ 运行测试。
4. **保持 C 构建绿色**（普通 `-xc`；`#include` 的垫片通过 `__bishengc` 自我保护）以便你始终有一个工作且可比较的基线。
5. **验证：** BSC 干净编译 **且** 测试通过 **且** `valgrind` 干净——由于 `return f(&_Const local)` 的代码生成 UAF，"编译了" ≠ "内存安全"。

## 你得到什么 vs 放弃什么

**得到：** 编译时泄漏/释放后使用/双重释放/借用别名检查；C 构建全程保留（工作基线、可比较、可发布）；零数据结构重写。

**放弃：** 自动 RAII——你仍然**编写**每条路径上的 `free`（编译器只是强制正确性："检查的手动释放"，不是"自动释放"）；没有自动析构函数级联（带有 `_Owned` 字段的普通 `struct` 是合法且具有移动语义的，但你手动释放每个字段——检查器保护）；深度递归/循环所有权可能仍需要原始指针 + `_Unsafe`；没有高层容器（与先注解一致——稍后根据需要采用 libcbs，按照 `bsc-design`，接受这些文件的双构建损失）。

## 常见错误

| 错误 | 现实 |
|---|---|
| 使用完整的 `c-to-bsc`（成员函数、libcbs、`_Owned struct`+析构函数） | 过度破坏，每个文件**杀死 C 构建**。除非你已经决定放弃该文件的双构建，否则保持仅注解。 |
| 期望带注解的 C 在没有垫片的情况下作为 C 编译 | 裸 `_Owned` 在 `-xc` 下是解析错误。垫片头文件对于 C 构建是必需的。 |
| 在源代码中直接写 `safe_malloc<T>` | `<T>` 泛型语法会破坏 C 构建。使用 `SAFE_MALLOC(T, …)` / `SAFE_CALLOC(T)` 宏（BSC → `safe_malloc<T>`，C → `malloc`/`calloc`）。 |
| 对大的 `T`（大的内联缓冲区/数组）使用 `SAFE_MALLOC` | `safe_malloc` **按值**接受 `T` → `T` 在栈上构建（已验证：1 MB 的 `T` → ~2 MB 调用者帧 + ~1 MB 在 `safe_malloc` 内 → 栈溢出风险）。使用 `SAFE_CALLOC(T)`（无复制，~24 B 帧）或原始 `malloc` + 在 `_Unsafe` 中按字段初始化。`-Wframe-larger-than=N` 会捕获它。 |
| 相信"它编译了，所以它是安全的" | 返回借用代码生成 UAF（上游 IJC66K）通过了检查器但是 UAF。**始终运行 valgrind。** |
| 删除 `#ifndef __bishengc` 保护（或在 BSC 下强制定义限定词） | 那么 `_Owned` 等在 BSC 下也会被擦除，你就失去了所有检查。保护是保持垫片在 `-x bsc` 下不活跃的东西。 |
| 当函数只读取时将参数注解为 `_Owned` | `_Owned` = 消耗（调用者的变量失效）。读取使用 `const T *_Borrow`。（参见 `c-to-bsc` 第 2.5 步。） |
| 将 `malloc`/`calloc`/`realloc`（返回 `void*`）重新声明为 `void *_Owned` | 转换为 `T *_Owned` 在安全区域中被禁止。保持它们原始 + `__take_from_raw`。只重新声明返回**最终有类型**指针的 API。 |
| 在 `_Safe` 行上写 `malloc` / `__take_from_raw` / `__move_to_raw` / 原始转换 | 这些都不是 `_Safe`。将最小行包装在 `_Unsafe { }` 中（垫片在 C 下擦除它 → 双构建保持）。 |
| 通过逐个字段赋值构建堆上的 `_Owned` 字段结构体 | 在安全区域中被禁止（"assign to part of _Owned value"）。聚合初始化一个值 `T e = { .f = … };` 然后使用 `SAFE_MALLOC(T, e)` 将其移动到堆上——**纯 `_Safe`，没有 `_Unsafe`**（已验证）。在 `_Unsafe` 中的原始构建只是回退。 |

## 现实世界影响（使用项目工具链验证）

- 仅注解（重新声明 `xmalloc`/`xfree`，**没有**析构函数/成员函数/泛型）：忘记释放 → `error: memory leak of value: 'p'`；释放后使用 → `error: use of moved value: 'p'`。仅凭注解核心检查就能工作。
- 释放形式在 BSC **和**普通 C 下都干净编译，通过自我保护 `#ifndef __bishengc` 垫片（双构建已确认；同一源代码，单一 `#include`）。
- 成员函数和 `safe_malloc<int>` 泛型在 `-xc` 下都失败——确认了上述禁止列表是你为保持 C 构建需要避免的。
- 子代理应用测试（端到端强化一个 `malloc`+`fopen` 模块）确认了仅注解 + 双构建流程有效（BSC 干净、普通 C 干净、valgrind 0 错误）。一开始看起来拥有的字段堆结构体需要一个 `_Unsafe` 构造块——但 `SAFE_MALLOC(T, aggregate)` 宏**连这个也消除了**（已验证）：有了它，对于固定类型分配和拥有的字段构造，方案是完全不需要 `_Unsafe` 的。
