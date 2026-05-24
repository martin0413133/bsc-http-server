---
name: c-to-bsc-annotate-only
description: "用于给存量 C 代码库做 BiSheng C 内存安全加固、同时它必须仍能当纯 C 编译(双编译);或当你想通过所有权标注获得编译期 泄漏 / use-after-free / double-free 检查,但又不想重写数据结构、不想引入成员函数 / 泛型 / trait / 析构 / libcbs 类型、也不想丢掉 C 构建时。它是 c-to-bsc 的保守、保留双编译的子集。"
---

# C → BSC:仅标注(保留双编译)的加固

## 概述

通过**只给现有函数和结构体字段添加所有权标注**来加固存量 C。停留在 BSC 的**宏可擦除子集**里,
使*同一份源码*仍能当纯 C 编译(双编译)。你**无需重写任何东西**,就能拿到编译期的
泄漏 / use-after-free / double-free / 借用检查。

**核心认识(已验证):** 安全检查来自**标注 + 借用检查器**,而**不是**来自析构、成员函数、
泛型或 libcbs。所以你可以跳过这些,仍然在编译期抓住 C 的堆内存 bug 类 —— 同时保住 C 构建。

**前置背景:** `c-to-bsc`(逐指针标注机制,`__take_from_raw`/`__move_to_raw`)、`bsc-ownership`。
本 skill 是 `c-to-bsc` 的*保守子集*:当完整翻译(libcbs、成员函数、`_Owned struct` + 析构)
扰动太大、或会丢掉 C 构建时,改用本 skill。

## 何时使用

- 迁移期间(或之后)项目**必须仍能当纯 C 构建/发布**。
- 你想要堆内存 bug 的安全网,但**无法承诺改成 `.cbs`** 或重写。
- 代码库很大;你想要**最小 diff、最大安全 ROI**,且增量推进。

**何时不要用:** 全新代码(直接用 `bsc-design` + libcbs);当你想要自动 RAII / 高级容器、
且不需要 C 构建时(用完整的 `c-to-bsc`)。

## 双编译子集 —— 能用什么 vs 什么会杀死 C 构建

| ✅ 允许(宏可擦除 → 双编译成立) | ❌ 禁止(BSC 专属语法 → 逐文件杀死 C 构建) |
|---|---|
| 函数上的 `_Safe` / `_Unsafe`;`_Unsafe { }` 块 | 成员函数 `Type::method`、方法调用糖 `obj->m()` |
| 参数、返回、**结构体字段**上的 `_Owned` / `_Borrow` / `const _Borrow` | 泛型:`Vec<T>`、`safe_malloc<T>`、任何 `<T>` |
| `_Nonnull` / `_Nullable`;`nullptr` | trait、`_Impl` |
| `&_Const` / `&_Mut` 取址 | `~T()` 析构 / **带析构体的** `_Owned struct` |
| 裸 `malloc`/`free`;`__move_to_raw`/`__take_from_raw` | libcbs 类型:`String`、`Vec`、`Rc`、… |
| 用所有权**重声明** extern/libc 函数(见下) | |

本会话已验证:成员函数(`S::get`)和泛型调用(`safe_malloc<int>`)在 `-xc` 下都**失败**;
裸 `_Owned` 在 `-xc` 下也失败(它不是 C)—— 这正是下面的 **shim** 对 C 构建是必需的原因。

## 唯一的一处基建:shim 头文件

BSC 编译器预定义了 `__bishengc`(已验证:`#define __bishengc 1`;普通 clang **不**定义它)。
据此分支,使**一句无条件的 `#include`** 在两种构建里都正确:BSC 下拉入 `bishengc_safety.hbs`、
限定符保持为真正的关键字;纯 C 下拉入 `<stdlib.h>`、擦除限定符、把分配宏映射到裸
`malloc`/`calloc`/`free`。一个头、无逐构建 `-include`、既不会泄漏进 BSC 构建也不会在 C 构建里
忘掉。`nullptr` → `((void*)0)`(不依赖 `<stddef.h>`/`NULL`)。

```c
/* bsc_shim.h —— 到处 #include 它;按编译器自动生效 */
#ifdef __bishengc
#  include "bishengc_safety.hbs"                  /* safe_malloc / safe_free —— 仅 BSC */
#  define SAFE_MALLOC(T, init) safe_malloc<T>(init)    /* 分配+初始化 -> T *_Owned(_Safe)*/
#  define SAFE_CALLOC(T)       safe_malloc<T>((T){0})  /* 清零的 T(本 libcbs 无 safe_calloc)*/
#  define SAFE_FREE(p)         safe_free((void *_Owned)(p))
#else
#  include <stdlib.h>                             /* malloc / free —— 仅 C */
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

两种编译器下均已验证:`-x bsc` 下泄漏仍被抓到(限定符生效),同一份源码 `-xc` 下干净编过
(限定符擦除、分配宏退回 malloc/calloc/free)。C 侧 `SAFE_MALLOC` 用 GNU/clang 的语句表达式
(`({ … })`)在单个表达式里完成分配+初始化 —— gcc 与 clang 都支持(本环境用的就是它们)。

## 首选分配:`SAFE_MALLOC` / `SAFE_CALLOC` / `SAFE_FREE`

BSC 下它们映射到 `safe_malloc<T>` / `safe_free` —— 都是 `_Safe`,所以**无需 `_Unsafe`、无需
`__take_from_raw`**。固定类型分配优先用它们,而非裸 `malloc`+`__take_from_raw`:

```c
int  *_Owned p = SAFE_MALLOC(int, 42);   /* 分配+初始化 */
Node *_Owned n = SAFE_CALLOC(Node);      /* 清零(本 libcbs 无 safe_calloc;宏用 safe_malloc<T>((T){0}))*/
SAFE_FREE(p);                            /* 消费 —— 泄漏/UAF 仍受检 */
```

**这还顺带化解了"带 `_Owned` 字段堆结构"的难题。** 安全区里不能逐字段给 `_Owned` 字段赋值,
但你**可以**先聚合初始化一个值,再让 `SAFE_MALLOC` 把它 move 到堆上 —— **纯 `_Safe`,无 `_Unsafe`**:

```c
typedef struct Entry { char *_Owned _Nullable key; char *_Owned _Nullable val; } Entry;
_Safe Entry *_Owned _Nullable entry_new(const char *_Borrow k, const char *_Borrow v) {
    Entry e = { .key = xstrdup(k), .val = xstrdup(v) };  /* 安全区聚合初始化 */
    return SAFE_MALLOC(Entry, e);                         /* move 到堆 —— 无 _Unsafe */
}
```

已验证:构造 + 拥有型字段拆卸(逐个 free 字段,再 `SAFE_FREE`)在纯 `_Safe` 下干净编过且双编译;
漏释放的那版在 `-x bsc` 下被抓到。仅在**变长 / 数组**分配时才退回裸 `malloc`+`__take_from_raw`
(放进 `_Unsafe`);BSC 的数组对应物是 `safe_malloc_array<T>(n, init)`。

## 重声明外部配对资源(无需 wrapper)

对**指针型** C 资源,用所有权重声明 acquire/release 配对 —— 检查器随即直接在那个裸 C 指针上
跟踪。无 wrapper struct,调用点还是 `_Safe`:

```c
_Safe FILE *_Owned _Nullable fopen(const char *_Nonnull, const char *_Nonnull);
_Safe int                    fclose(FILE *_Owned stream);   /* consume == free(消费即释放)*/
```

于是 `fopen` → 每条路径上必须恰好走到一次 `fclose`(否则编译期泄漏),且 `f` 在 `fclose`
之后不可用(编译期 use-after-free)。在 shim 下这些会擦除成标准原型。
(`free`、`SSL_CTX_new`/`SSL_CTX_free` 等遵循同样的形态。)

> **别把 `void*` 返回型分配器(`malloc`/`calloc`/`realloc`)重声明成 `_Owned`** —— 把
> `void *_Owned` 转成有类型的 `T *_Owned` 在安全区是*禁止*的(实测)。分配请优先用上面的
> `SAFE_MALLOC`/`SAFE_CALLOC` 宏(`_Safe`);只有当你必须调裸 `malloc`(变长/数组)时,才让它
> 保持裸指针并用 `__take_from_raw` 在最小 `_Unsafe { }` 块里过桥。用 `_Owned` 重声明只适用于
> 返回**最终有类型**指针的 API(`fopen`→`FILE*`、`strdup`→`char*`)。

> **`int` 文件描述符是例外:** `_Owned` 是*指针*限定符 —— 一个 `int` fd(`open`/`accept` →
> `close`)带不了它。重声明帮不上忙;要么让该 fd 不被跟踪,要么用
> `_Owned struct Fd { int fd; ~Fd … }` 包起来 —— 但那个析构是 BSC 专属的,会**让该文件失去
> 双编译**。逐情况权衡。

## 工作流

1. **Phase 0 —— 让它原样当 BSC 编过。** 每个 TU 用 `clang -x bsc` 编(BSC 是 C 超集;普通 C
   通常**零**标注就干净编过)。只需修:与 BSC 关键字冲突的标识符(如 `This`),以及个别
   `-xc` 与 `-xbsc` 的一致性 bug。在 BSC 构建上跑测试套件 → 行为必须与 C 构建一致。
2. **Phase 1 —— 用所有权重声明外部配对资源 API**(见上)。
3. **Phase 2 —— 自底向上标注签名 + 结构体字段(叶子模块先做)。** 每个函数标 `_Safe`;每个
   指针参数/返回给 `_Owned`/`_Borrow`/`const _Borrow`(或带理由地保持裸指针);标注拥有型
   结构体字段 `_Owned`。检查器会**强制**每条路径上正确释放。每个文件改完即编(BSC)+ 跑测试。
4. **同时保持 C 构建为绿**(纯 `-xc`;被 `#include` 的 shim 通过 `__bishengc` 自守卫),
   这样你始终有一个能跑、可 diff 的基线。
5. **验证:** BSC 干净编过 **且** 测试通过 **且** `valgrind` 干净 —— 因为存在
   `return f(&_Const local)` 的 codegen UAF,目前**"能编译" ≠ "内存安全"**。

## 你得到什么 vs 放弃什么

**得到:** 编译期的 泄漏 / use-after-free / double-free / 借用别名 检查;全程保留 C 构建
(可跑、可 diff、可发布的基线);零数据结构重写。

**放弃:** 自动 RAII —— 你仍需**自己写**每条路径上的 `free`(编译器只是强制其正确:是
"受检的手工 free",而非"自动 free");无自动析构级联(带 `_Owned` 字段的**普通 `struct`**
合法且是 move 语义,但你要手动释放每个字段 —— 受检查器守护);深递归/环状所有权可能仍需
裸指针 + `_Unsafe`;无高级容器(与"标注优先"一致 —— 之后再按 `bsc-design` 引入 libcbs,
并接受那些文件失去双编译)。

## 常见错误

| 错误 | 真相 |
|---|---|
| 直接上完整 `c-to-bsc`(成员函数、libcbs、`_Owned struct`+析构) | 扰动过大且**逐文件杀死 C 构建**。除非已决定放弃该文件的双编译,否则坚持只标注。 |
| 以为标注后的 C 不用 shim 也能当 C 编 | 裸 `_Owned` 在 `-xc` 下是 parse 错误。shim 头对 C 构建是必需的。 |
| 在源码里直接写 `safe_malloc<T>` | `<T>` 泛型语法破坏 C 构建。改用 `SAFE_MALLOC(T, …)` / `SAFE_CALLOC(T)` 宏(BSC → `safe_malloc<T>`,C → `malloc`/`calloc`)。 |
| 相信"它编过了,所以安全" | return-borrow 的 codegen UAF(上游 IJC66K)能过检查器但确是 UAF。**务必跑 valgrind。** |
| 去掉 `#ifndef __bishengc` 守卫(或在 BSC 下强行定义这些限定符) | 那么 `_Owned` 等在 BSC 下也被擦除,你会丢掉全部检查。守卫正是让 shim 在 `-x bsc` 下保持惰性的东西。 |
| 函数只读却把参数标成 `_Owned` | `_Owned` = 消费(调用方的变量随之失效)。只读应取 `const T *_Borrow`。(见 `c-to-bsc` Step 2.5。) |
| 把 `malloc`/`calloc`/`realloc`(返回 `void*`)重声明成 `void *_Owned` | 转成 `T *_Owned` 在安全区被禁止。让它们保持裸指针 + `__take_from_raw`(放进 `_Unsafe`)。只重声明返回**最终有类型**指针的 API。 |
| 把 `malloc` / `__take_from_raw` / `__move_to_raw` / 裸指针强转写在 `_Safe` 行上 | 这些都不是 `_Safe`。把那一行用 `_Unsafe { }` 包住(C 下被 shim 擦除 → 双编译成立)。 |
| 用逐字段赋值去构造带 `_Owned` 字段的堆结构 | 安全区禁止("assign to part of _Owned value")。先聚合初始化一个值 `T e = { .f = … };` 再用 `SAFE_MALLOC(T, e)` move 到堆上 —— **纯 `_Safe`,无 `_Unsafe`**(已验证)。裸指针构造(放 `_Unsafe`)只是退路。 |

## 实测影响(用项目工具链验证)

- 仅标注(重声明的 `xmalloc`/`xfree`,**无**析构/成员函数/泛型):漏掉 free →
  `error: memory leak of value: 'p'`;释放后再用 → `error: use of moved value: 'p'`。
  仅靠标注核心检查就生效。
- 已释放的那版在 BSC 下干净编过 **且** 通过自守卫的 `#ifndef __bishengc` shim 在纯 C 下也
  干净编过(双编译确认;同一份源码,单次 `#include`)。
- 一个成员函数和一个 `safe_malloc<int>` 泛型在 `-xc` 下各自失败 —— 印证上面的禁止清单
  正是为保住 C 构建而要避开的东西。
- 一次 subagent 应用测试(端到端加固一个 `malloc`+`fopen` 模块)确认了仅标注 + 双编译流程可行
  (BSC 干净、纯 C 干净、valgrind 0 错误)。它起初看似"带 `_Owned` 字段的堆结构需要一小段
  `_Unsafe` 构造块",但 `SAFE_MALLOC(T, aggregate)` 宏**连这一点也消除了**(已验证):有了它,
  该 regime 在固定类型分配与拥有型字段构造上可做到完全 `_Unsafe`-free。
