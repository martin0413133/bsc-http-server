---
name: c-to-bsc
description: "BiSheng C 从 C 翻译。当你需要将 C 代码（.c/.h）翻译/转换/移植到 BiSheng C（.cbs/.hbs）、添加所有权注解或迁移 C 项目到 BSC 时，你必须先使用此技能。"
---

# C 到 BiSheng C 翻译技能

## 必读：在任何 C 到 BSC 翻译之前

如果你正在将 C 代码翻译到 BSC，你**必须**遵循此流程。**基本规则是：保持文件结构不变，只添加所有权注解。** 不要将 `.c`/`.h` 重命名为 `.cbs`/`.hbs`；不要剥离平台特定代码。使用 `clang -x bsc file.c` 编译。一个翻译意味着分析每个指针并添加 BSC 注解——而不是重构代码库。

## 最高规则：默认安全——`_Unsafe` 由编译器驱动，而非作者驱动

**将每个函数标记为 `_Safe`。将每个函数体写成好像是 `_Safe`。仅当编译器报告无法以其他方式修复的错误时才添加 `_Unsafe`。**

你不需要预测哪些行"感觉不安全"并预先包装它们。借用检查器和安全区域检查器是真相来源——如果一行在 `_Safe` 中编译通过，它就应该留在 `_Safe` 中。工作流程是：

1. 将函数签名注解为 `_Safe`。
2. 将函数体翻译为普通 BSC——任何地方都不使用 `_Unsafe`。
3. 编译。对于编译器报告的每个诊断：
   - 首先尝试**在没有 `_Unsafe` 的情况下修复它**（将 `&` 转换为 `&_Const`/`&_Mut`，用 `safe_malloc`/`safe_free` 替换原始 `malloc`/`free`，添加缺失的 `_Owned`/`_Borrow`/`_Nullable`，重组行）。
   - 仅当没有 `_Safe` 区域形式存在时（变参 `printf`、非容器缓冲区上的原始指针算术、FFI 转换、通过 `T*` 的原始下标等），才将**仅出问题的语句**包装在 `_Unsafe stmt;`（无大括号）或 `_Unsafe { ... }`（最小块）中。
4. 重新编译。重复直到干净。不要预先扩展任何 `_Unsafe` 块"以防万一"——保持严密；如果还需要更多，编译器会告诉你。

**禁止的快捷方式：**

- ❌ 因为内部某一行需要逃逸就将整个函数标记为 `_Unsafe`。
- ❌ 当只有一条语句触发错误时，将多行块包装在 `_Unsafe { ... }` 中。
- ❌ 在编译之前"防御性"地添加 `_Unsafe`——先写安全形式，让编译器决定。
- ❌ 将借用检查器错误视为逃逸到 `_Unsafe` 的理由。借用错误意味着所有权注解是错误的；修复注解，不要绕过检查器。

**`_Unsafe` 是传染性的——函数上的 `_Unsafe` 会强制每个调用者进入 `_Unsafe` 上下文。** 保持函数为 `_Safe` 并将逃逸限制在一行内，可以将成本本地化。包含 `_Unsafe { ... }` 块的函数**仍然算是 `_Safe`**——这正是块的全部意义。

本技能的其余部分详细说明了在 `_Safe` 内部编写什么以及如何修复特定的诊断。当一个部分说"对 X 使用 `_Unsafe`"时，它的意思是：编译器会拒绝 X 的安全形式——包装最小的失败行，而不是周围的逻辑。

## 最高规则 2：函数签名中的每个指针都必须被注解

**出现在函数声明、函数定义或参数列表中的每个指针都必须携带 `_Owned`、`_Borrow`，或者是带有结构性质因的故意原始。** 签名中的裸 `T *` 永远不能作为"默认"或"TODO"接受——它是一个积极的声明，表示该指针是原始的（内部指针、算术游标、输出双指针、函数指针、泛型上下文中的不透明 void* 或仅在 `_Unsafe` 的 API 内部）。

这适用于：

- **每个参数**：除非 `p` 确实是原始的，否则 `f(T *p)` 是错误的。使用 `f(T *_Owned p)`、`f(T *_Borrow p)` 或 `f(const T *_Borrow p)`。
- **每个返回类型**：除非返回的指针确实是原始的，否则 `T *f(...)` 是错误的。使用 `T *_Owned f(...)`（调用者释放）或 `T *_Borrow f(...)`（调用者不释放；生命周期绑定到输入）。
- **`.h`/`.hbs` 中的声明和 `.c`/`.cbs` 中的定义**——它们必须匹配。

**默认决策树（适用于每个签名中的每个指针）：**

1. 函数会释放指针、将其转移出去或存储在长期拥有者中？→ `T *_Owned`。
2. 函数通过指针读取而不修改？→ `const T *_Borrow`。
3. 函数通过指针修改但不释放？→ `T *_Borrow`。
4. 它是输出双指针（`T **out`）、函数指针、不透明转换中介或用于算术的游标？→ 原始 `T *` 是正确的，附一行注释解释原因。

如果 (1)–(4) 中没有一个明确适用，你还没有完成对函数的分析。**不要将其保留为原始作为"足够好"的占位符**——缺失的注解会在 C 中静默通过，在 BSC 的借用检查器在调用点失败，或者更糟的是，类型检查通过但编码了错误的所有权模型。

```c
// 错误——裸 T*；读者无法判断调用者释放、被调用者释放还是两者都做
Node *find(Container *c, int id);

// 正确——只读查找，返回指向 c 的引用
const Node *_Borrow find(const Container *_Borrow c, int id);

// 正确——工厂：被调用者分配，调用者释放
Node *_Owned create(int id);

// 正确——消费者：被调用者获取 item 的所有权
void enqueue(Container *_Borrow c, Node *_Owned item);
```

**头文件/源文件一致性。** 注解也必须出现在声明中，而不仅仅是定义中。暴露 `Node *find(Container *c, int id)` 的 `.h` 和定义 `const Node *_Borrow find(const Container *_Borrow c, int id)` 的 `.c` 会产生类型不匹配的诊断，或者静默地让调用者误用 API。

**翻译时的自检：** 在转到下一个函数之前，逐行扫描其签名，确认每个 `*` 都有限定词（`_Owned` / `_Borrow`）或是有理由的故意原始。如果你不能决定，那是一个信号，表明你需要阅读函数体和它的调用者——而不是将其留为裸指针。

**最低标准**：任何非平凡的 C 项目的有效 BSC 翻译**必须**包含 `_Owned` 注解。任何调用 `malloc`/`free` 的 C 代码都有必须在 BSC 中表达的所有权语义。如果你的翻译有零个 `_Owned` 注解，它是不完整的——回到第 3 步重新分析。"代码对于所有权来说太复杂了"不是跳过注解的有效理由；对复杂部分使用 `_Unsafe` 块，并注解你所能注解的。

## 关键规则：`_Owned` 默认不可空

`_Owned` 指针不能是 `NULL`，除非你添加 `_Nullable`：

```c
// 错误——编译器错误：_Owned 指针不能为空
cJSON *_Owned item = NULL;

// 正确——当指针可能为 NULL 时添加 _Nullable
cJSON *_Owned _Nullable item = nullptr;

// 正确——立即初始化，不需要 _Nullable
cJSON *_Owned item = safe_malloc<cJSON>((cJSON){0});
```

**规则**：如果 `_Owned` 变量曾经被赋值为 `NULL`/`nullptr`、初始化为 `NULL` 或与 `NULL` 比较，则必须声明为 `T *_Owned _Nullable`。在 BSC 代码中，使用 `nullptr` 而不是 `NULL`。

## 翻译流程

### 第 1 步：尽可能少做更改（基本规则）

**尽可能少地更改 C 代码库。** 只**添加** BSC 所有权注解。只在编译器真正要求时才更改其他任何内容。

- 保留 `.c`/`.h` 文件扩展名；使用 `clang -x bsc file.c` 编译（或 `-xbsc`，在 driver 和 `-cc1` 中都可以）。
- 平台特定代码（`#ifdef _WIN32`、`__declspec(...)`、`__cdecl`、`__stdcall`、`extern "C"` 保护、可见性宏）通常原样可用。BSC 基于 clang；这些要么适用于目标平台，要么被静默忽略。仅当特定构造触发编译错误时才处理它们。
- `#include` 路径保持不变（仍然是 `.h`）。通常需要的一个新引入：在使用 `safe_malloc`/`safe_free` 的地方使用 `#include "bishengc_safety.hbs"`。

理由：增量迁移让项目可以从同一源文件同时构建为普通 C 和 BSC；保持审阅差异集中在所有权上；如果需要，保留跨平台代码。

### 第 2 步：默认函数为 `_Safe`；仅在编译器要求时才添加 `_Unsafe`

这一步是应用本技能顶部的**最高规则**。在继续之前，重新阅读它。

当你添加或修改函数签名时，将其标记为 `_Safe`。将函数体写成完全 `_Safe`。编译。是编译器——不是你的直觉——告诉你哪些行需要逃逸到 `_Unsafe`。对于安全检查器确实无法建模的操作——变参调用如 `printf`、原始指针算术、FFI、需要转换的指针转换、通过 `T*` 的原始下标——仅将出问题的语句包装在 `_Unsafe stmt;`（单行，无大括号）或 `_Unsafe { ... }` 块中。

**`_Unsafe` 是传染性的。** 一个 `_Unsafe` 函数会强制每个调用者也处于 `_Unsafe` 上下文中，成本会通过代码库传播。保持接口 `_Safe` 可以让借用检查器帮助调用者，即使实现内部有一些逃逸口。

```c
// 好——接口是 _Safe，只有一条不安全操作被包装
_Safe void log_value(const int *_Borrow v) {
    _Unsafe { printf("%d\n", *v); }     // printf 是变参，需要 _Unsafe
}

// 不好——整个函数仅因一行代码就是 _Unsafe
_Unsafe void log_value(const int *_Borrow v) {
    printf("%d\n", *v);                  // 强制每个调用者也变成 _Unsafe
}
```

**内部有 `_Unsafe { ... }` 块的函数仍然是 `_Safe`。** 两者并不冲突；这正是 `_Unsafe` 块的全部意义。不要仅仅因为函数体需要一个逃逸就将其降级为 `_Unsafe`。

**`_Unsafe` 块必须是最小的。** 只包装真正需要逃逸的语句，不要多包。检查你放在 `_Unsafe` 内部的每一行：如果它在 `_Safe` 中可以正常编译，就把它移出去。

```c
// 不好——臃肿的 _Unsafe 块，只有赋值需要逃逸
_Safe void f(uint8_t *buf, size_t i, uint8_t v) {
    _Unsafe {
        if (i >= cap) return;       // 可在 _Safe 中
        validate(v);                // 可在 _Safe 中
        buf[i] = v;                 // 原始下标——需要 _Unsafe
        log("wrote byte");          // 可在 _Safe 中
    }
}

// 好——只有原始下标行是 _Unsafe
_Safe void f(uint8_t *buf, size_t i, uint8_t v) {
    if (i >= cap) return;
    validate(v);
    _Unsafe buf[i] = v;             // 单语句 _Unsafe——不需要大括号
    log("wrote byte");
}
```

**提交 BSC 代码前包含任何 `_Unsafe` 时的自检：**
1. 外层函数被注解为 `_Safe`？如果没有，它能否作为 `_Safe` 通过借用检查器？如果能，更改它。
2. `_Unsafe { ... }` 内部的每一行是否真的需要逃逸？如果不是，将安全行移出来——一行 `_Unsafe stmt;`（无大括号）通常是正确的形式。

### 第 2.5 步：默认指针参数为 `_Borrow`，而不是 `_Owned`

一个通过指针**读取或修改**值但**不获取所有权**的函数必须使用 `_Borrow`，绝不要使用 `_Owned`。`_Owned` 意味着函数消耗该值——调用者的变量在调用后失效。大多数 C 函数不这样做；它们只需要查看或修改存在于其他地方的东西。

**选择参数限定词时的经验法则：**

- 函数释放指针、将其转移出去或存储在长期拥有者中 → `T *_Owned`。（调用者的变量在调用后失效。）
- 函数只通过指针读取 → `const T *_Borrow`。
- 函数通过指针**修改**但不释放 → `T *_Borrow`。
- 函数进行指针算术/迭代 → 原始 `T *`（不借用）。这是裸 `T *` 参数**唯一**正确的情况；所有其他指针参数必须是 `_Owned` 或 `_Borrow`（参见最高规则 2）。

```c
// 不好——_Owned 强制调用者为只读函数放弃所有权。调用者之后无法使用 cfg。
_Safe int get_port(const Config *_Owned cfg) { return cfg->port; }

// 好——借用；调用者保留 cfg
_Safe int get_port(const Config *_Borrow cfg) { return cfg->port; }

// 不好——_Owned 用于不消耗的 setter
_Safe void set_port(Config *_Owned cfg, int p) { cfg->port = p; }

// 好——可变借用；调用者保留 cfg
_Safe void set_port(Config *_Borrow cfg, int p) { cfg->port = p; }
```

**在注解函数签名前自检：** 对每个指针参数，问"调用者的变量在这个调用后是否失效？"如果否，它是 `_Borrow`，不是 `_Owned`。在所有地方默认使用 `_Owned` 会使每个调用点成为一次性移动——这是错误的，并且会强制调用者使用原始 C 中没有的丑陋的重新分配模式。

参见 `/bsc-safe-zone` 了解 `_Safe` 区域限制和 `_Unsafe` 块的合法使用的完整列表。

### 第 3 步：分析所有权（关键步骤）

对于代码库中的每个指针，将其分为三类之一：

#### 类别 A：拥有的指针 → 注解为 `*_Owned`

如果一个指针拥有数据，条件包括：
- 它是从 `malloc`/`calloc`/`realloc`/`strdup` 赋值的
- 代码对其调用 `free()`（或负责释放）
- 它是从创建/分配的函数返回的（调用者必须释放）
- 在结构体的清理/删除函数中被释放的结构体字段

#### 类别 B：借用的指针 → 注解为 `*_Borrow`

如果一个指针借用数据，条件包括：
- 它指向别人的分配（例如，反向指针、父指针）
- 它是用于只读或不释放的修改访问的函数参数
- 它用于遍历而不获取所有权
- 它临时别名另一个拥有的指针

#### 类别 C：原始指针 → 保持为普通 `T *`（无注解）

一些**特定的**指针不能或不应该被注解。保持这些为原始 `T *`。

**警告**：类别 C 是针对有特定原因的单个指针，而不是针对整个代码库。如果你发现自己将大多数指针放入类别 C，你做错了——回去重新分析。大多数涉及 `malloc`/`free` 的指针属于类别 A。

**类别 C 不适用于函数签名指针**（参数或返回值），除了最高规则 2 中列出的四种狭窄情况：输出双指针、函数指针、不透明转换中介和算术游标。如果签名指针不属于这四个之一，它**必须**是 `_Owned` 或 `_Borrow`。局部内部/游标变量是典型的类别 C——而不是 API 表面。

- **指针算术 / 内部指针**：指向缓冲区中间的指针
  ```c
  unsigned char *cursor = buffer + offset;  // 指向缓冲区内部，不是分配
  char *end = str + len;                    // 指针算术结果
  ```
- **输出参数 / 双指针**：用于返回值的 `T **out`
  ```c
  void get_result(int **out) { *out = &some_local; }
  ```
- **迭代器 / 扫描指针**：遍历数据的临时指针
  ```c
  const char *p = input;
  while (*p != '\0') { p++; }  // 只是扫描，没有所有权
  ```
- **条件所有权**：当同一个字段有时拥有、有时不拥有时（基于标志）
  ```c
  // 如果所有权取决于运行时标志，你可能需要保持其为原始
  // 并使用 _Unsafe 块处理
  struct Item {
      int flags;
      void *data;  // 如果 !(flags & IS_REFERENCE) 则拥有，否则借用
  };
  ```
- **不透明 / void* 在泛型上下文中**：转换中介
  ```c
  void *tmp = (void *)ptr;  // 中间转换，不是真正的分配
  ```
- **`_Unsafe` 块内的栈指针**：保持为原始 `T *` 的 `&local_var` 仅在 `_Unsafe` 中有效。在 `_Safe` 区域中，`&local_var` **是**一个借用，必须写为 `&_Const local_var` / `&_Mut local_var`，接收者类型为 `const T *_Borrow` / `T *_Borrow`。参见第 4.5 步。
  ```c
  _Unsafe {
      int x = 42;
      int *p = &x;       // 原始指针——仅在 _Unsafe 中可以
  }
  _Safe void f(void) {
      int x = 42;
      int *_Borrow p = &_Mut x;   // 不是原始 `int *p = &x`——那是禁止的
  }
  ```
- **函数指针**：不是数据指针，保持原样
- **`_Unsafe` 块内的指针**：当你显式选择退出安全检查时

### 第 3.5 步：将分配转换为 safe_malloc / safe_free（首选）

BSC 从 `bishengc_safety.hbs` 提供 `safe_malloc<T>(val)` 和 `safe_free()`。**优先选择这些而不是原始的 malloc/free**——它们直接返回 `T *_Owned`，不需要转换，并且在 `_Safe` 上下文中工作。

```c
#include "bishengc_safety.hbs"
```

**签名（来自 BSC 标准库）：**
- `_Safe T *_Owned safe_malloc<T>(T t)` ——分配，初始化为 `t`，返回 `T *_Owned`
- `_Safe void safe_free(void *_Nullable _Owned p)` ——释放一个可空拥有的指针

| C（原始） | BSC（首选：safe_malloc） | BSC（原始 malloc + __take_from_raw） |
|---|---|---|
| `malloc(sizeof(int))` | `safe_malloc<int>(0)` | `__take_from_raw((int *)malloc(sizeof(int)))` |
| `free(p)` | `safe_free((void *_Owned)p)` | `_Unsafe { free(__move_to_raw(p)); }` |
| `p = NULL; free(p);` | `safe_free((void *_Nullable _Owned)p)` | 不适用——对可空使用 safe_free |

### 关键：`(T *_Owned)` 和 `(T *)` 在原始和 `_Owned` 之间的转换在所有地方都被禁止

**编译器拒绝所有上下文中 `_Owned` 和原始指针之间的直接 C 转换——包括 `_Unsafe`。** 你必须使用内置函数 `__take_from_raw`（raw → `_Owned`）和 `__move_to_raw`（`_Owned` → raw）。

```c
// 错误——编译器拒绝，报错"cannot cast between _Owned and raw pointer;
//          use __move_to_raw or __take_from_raw for ownership transfer"
int *_Owned p = (int *_Owned)malloc(sizeof(int));   // 错误
int *raw = (int *)p;                                  // 错误（p 是 _Owned）

// 正确
int *_Owned p = __take_from_raw((int *)malloc(sizeof(int)));
int *raw = __move_to_raw(p);                          // 移出所有权；之后 p 不可用
```

已针对 `clang/test/BSC/Negative/Ownership/RuleCheck/owned_raw_cast_disallowed/owned_raw_cast_disallowed.cbs` 和 `clang/test/BSC/Positive/Ownership/owned_raw_transfer_builtins/owned_raw_transfer_builtins.cbs` 验证。

**例外——通过 void 的 owned-to-owned 转换是允许的**：当 `p` 是 `T *_Owned` 时，`(void *_Owned)p` 没问题（两边都是 `_Owned`；没有涉及原始）。这就是为什么 `safe_free((void *_Owned)p)` 保持正确。

**借用-然后-转换模式**——当你需要将原始指针传递给 C API 而不转移所有权时：
```c
int *_Owned p = safe_malloc<int>(42);
foo((int *)&_Mut *p);       // 通过 *p 借用，然后将借用转换为原始。p 保持所有权。
safe_free((void *_Owned)p);
```

### `__take_from_raw` 之前的空检查

当可空性检查启用时（默认在 `-nullability-check=safeonly` 下），`__take_from_raw(raw)` 在 `raw` 未被空检查时会出错。要么先空检查，要么将结果声明为 `_Nullable`：

```c
// 选项 A：先空检查，赋值给非可空 _Owned
int *raw = (int *)malloc(sizeof(int));
if (raw != nullptr) {
    int *_Owned p = __take_from_raw(raw);
    // 使用 p
}

// 选项 B：在结果类型中接受可空性
int *_Owned _Nullable p = __take_from_raw((int *)malloc(sizeof(int)));
```

**何时使用哪个：**
- `safe_malloc<T>(val)` ——在分配已知类型的单个值时使用。返回 `T *_Owned`，初始化为 `val`。不需要转换或空检查。在 `_Safe` 上下文中工作。**覆盖了几乎所有 C 的 `malloc(sizeof(T))` 情况**——除非需要可变大小，否则不要使用原始 `malloc` 回退。
- `safe_free(p)` ——**所有释放的首选**。参数类型是 `void *_Nullable _Owned`。根据源指针的类型进行转换：
  - 如果 `p` 是 `T *_Owned`（非可空）：`safe_free((void *_Owned)p)`。调用处的 `_Nullable` 扩展是隐式的。
  - 如果 `p` 是 `T *_Nullable _Owned`：`safe_free((void *_Nullable _Owned)p)`。转换必须保留 `_Nullable`——转换为非可空 `(void *_Owned)` 会收窄并报错。
- 原始 `malloc` + `__take_from_raw` ——仅在 `safe_malloc<T>` 不适用时使用：变长分配（`malloc(n * sizeof(T))`）、`calloc`、`realloc`、`strdup` 或缓冲区分配。需要 `_Unsafe` 上下文。
- 原始 `free()` ——接受原始 `void *`。使用 `__move_to_raw(owned_ptr)` 调用以移出 `_Owned` 指针。仅在 `_Unsafe` 块中使用。如果指针可能为 NULL，使用 `safe_free`。

#### 将 malloc 转换为 safe_malloc
```c
// 转换前（C）
Node *n = (Node*)malloc(sizeof(Node));
memset(n, 0, sizeof(Node));

// 转换后（BSC）——首选
Node *_Owned n = safe_malloc<Node>((Node){0});

// 转换后（BSC）——原始 malloc + __take_from_raw（当 safe_malloc 不适用时）
_Unsafe {
    Node *raw = (Node *)malloc(sizeof(Node));
    if (raw == nullptr) { /* 处理 */ }
    memset(raw, 0, sizeof(Node));
    Node *_Owned n = __take_from_raw(raw);
}
```

#### 将 free 转换为 safe_free
```c
// 转换前（C）
free(node->name);
free(node);

// 转换后（BSC）——safe_free 接受 _Nullable _Owned，对可能为 NULL 的指针安全
safe_free((void *_Owned)node->name);
safe_free((void *_Owned)node);

// 如果指针可能为 NULL（在清理路径中常见）：
safe_free((void *_Nullable _Owned)node->name);  // 即使 name 是 NULL 也安全
```

#### 变长分配——原始 malloc + `__take_from_raw`
```c
// 转换前（C）
char *buf = (char*)malloc(length + 1);
char *copy = strdup(original);

// 转换后（BSC）——在 _Unsafe 中原始 malloc，然后通过 __take_from_raw 获取 _Owned
_Unsafe {
    char *raw_buf = (char *)malloc(length + 1);
    if (raw_buf == nullptr) { /* 处理 */ }
    char *_Owned buf = __take_from_raw(raw_buf);

    char *raw_copy = strdup(original);
    if (raw_copy == nullptr) { /* 处理 */ }
    char *_Owned copy = __take_from_raw(raw_copy);
}
```

#### Realloc 所有权语义
`realloc` 接受原始 `void *` 并返回原始 `void *`。你必须将旧的拥有缓冲区通过 `__move_to_raw` 移出，调用 `realloc`，然后通过 `__take_from_raw` 获取结果。失败时（返回 NULL），C 的 `realloc` **不会**释放旧缓冲区，但所有权已经被移出——所以你必须恢复原始并重新包装：

```c
_Unsafe {
    // 将旧的 _Owned 缓冲区移出到原始指针
    char *raw_old = __move_to_raw(old_buf);

    char *raw_new = (char *)realloc(raw_old, newsize);
    if (raw_new == nullptr) {
        // realloc 失败：根据 C 语义，raw_old 仍然有效。
        // 重新包装为 _Owned 并释放。
        char *_Owned recovered = __take_from_raw(raw_old);
        safe_free((void *_Owned)recovered);
        return;
    }
    // 成功：raw_new 是移动/增长后的缓冲区。包装为 _Owned。
    char *_Owned new_buf = __take_from_raw(raw_new);
    old_buf = new_buf;   // old_buf 现在拥有调整大小后的缓冲区
}
```

### 第 4 步：注解指针（仅限类别 A 和 B）

只注解明显属于类别 A 或 B 的指针。如有疑问，保持为原始并添加 `/* TODO: ownership unclear */` 注释。

#### 结构体字段

**规则**：结构体的清理/删除函数是真相来源。如果清理函数释放了 `field` → 标记为 `_Owned`。如果没有 → 将字段保持为普通（原始）指针或使用值类型。**不要将 `_Borrow` 用作结构体字段，除非你已验证设计有效**（见下面的限制）。

```c
// 转换前（C）
typedef struct Token {
    char *name;
    char *value;
    int kind;
} Token;

// 转换后（BSC）——清理函数释放 name 和 value；kind 是纯数据
typedef struct Token {
    char *_Owned name;             // 由 ~Token / cleanup 释放 → _Owned
    char *_Owned value;            // 由 ~Token / cleanup 释放 → _Owned
    int kind;                      // 普通值
} Token;
```

#### `_Borrow` 作为结构体字段——严格限制，不要用于反向引用

根据用户手册 §3.2.6，结构体 `_Borrow` 字段受到排除了大多数 C 模式的规则限制：

1. 结构体的 `_Borrow` 字段**不能从包含它的结构体或其其他字段借用**（规则 3）。因此 `s.p = &_Const s.m` 会报错。
2. 包含 `_Borrow` 字段的结构体**本身不能被借用**（规则 5）。这会级联：一旦 `T` 有 `_Borrow` 成员，`T *_Borrow` 就是非法的。
3. 堆分配的结构体，其 `_Borrow` 字段交叉引用其他堆节点——经典链表/树带父指针——**不满足借用检查器的生命周期模型**。借用的目标可能被独立释放。

**对于链表、树、图、观察者模式中的反向引用**：使用原始指针（无注解），访问包装在 `_Unsafe` 中。参见下面的"循环结构体"部分。标准库的 `LinkedList<T>`（`libcbs/src/list/list.hbs`）正是出于这个原因使用原始指针：

```c
// 来自标准库——链表节点使用原始指针，而不是 _Owned 或 _Borrow
struct _BSC_ListNode<T> {
    _BSC_ListNode<T>* next;    // 原始
    _BSC_ListNode<T>* prev;    // 原始
    T element;
};
```

旧的"带有 `_Owned next` 和 `_Borrow prev` 的双向链表"示例曾经出现在这里，但那是错误的——它既不能干净编译，也不代表有效的 BSC 模式。改用 §"循环结构体"中的模式 A。

#### 函数参数——借用（当不获取所有权时）
```c
// 转换前（C）
int count_items(const Node *list) { ... }  // 只读，不释放

// 转换后（BSC）
int count_items(const Node *_Borrow list) { ... }
```

#### 函数参数——获取所有权（调用者放弃所有权）
```c
// 转换前（C）
void container_add(Container *c, Node *item) { ... c 获取 item 的所有权 ... }

// 转换后（BSC）
void container_add(Container *_Borrow c, Node *_Owned item) { ... }
```

#### 函数返回——转移所有权
```c
// 转换前（C）
Node* create_node(void) {
    Node *n = malloc(sizeof(Node));
    return n;  // 调用者必须释放
}

// 转换后（BSC）——首选：使用 safe_malloc
Node *_Owned create_node(void) {
    Node *_Owned n = safe_malloc<Node>((Node){0});
    return n;  // 所有权转移给调用者
}

// 转换后（BSC）——如果必须使用原始 malloc，通过 __take_from_raw 包装
Node *_Owned create_node_raw(void) {
    _Unsafe {
        Node *raw = (Node *)malloc(sizeof(Node));
        if (raw == nullptr) { /* 处理 */ }
        Node *_Owned n = __take_from_raw(raw);
        return n;
    }
}
```

#### 函数返回——借用（返回指向现有数据的指针）
```c
// 转换前（C）
Node* find_node(Container *c, int id) { ... 返回指向 c 数据的指针 ... }

// 转换后（BSC）——调用者不释放结果
Node *_Borrow find_node(Container *_Borrow c, int id) { ... }
```

#### Getter 的 const 正确性——一个影响整个代码库的翻译决策

C 的 getter `T* get(Container* c, ...)` 通常是只读的。天真的翻译是 `T *_Borrow get(Container *_Borrow this, ...)`——但这是**不是 const**：`Container *_Borrow` 是一个可变借用。任何来自 `const Container *_Borrow` 上下文的调用者都不能调用它。

这很微妙，因为 C 没有借用检查器——C 的只读 getter 和 C 的修改 setter 具有相同的指针类型。BSC 需要你在翻译时做出选择：

| C 中的 getter 行为 | BSC 签名 |
|----------------------|---------------|
| 只读取字段；不修改容器 | `const T *_Borrow get(const Container *_Borrow this, ...)` |
| 返回容器的可变借用以便调用者可以修改 | `T *_Borrow get_mut(Container *_Borrow this, ...)` |
| 两者都需要 | 提供**两个**成员函数（`get` + `get_mut`） |

**忘记只读 getter 的 const 是整个代码库的麻烦。** 一旦你有一个函数需要 `const Container *_Borrow`，每个它想要调用的非 const getter 都变得不可访问。然后调用者通过直接访问 `_Public` 字段、转换或复制帮助函数来绕过它——所有这些都破坏了 API。

**翻译时的检查清单：**
- 审计每个名字以 `get_`、`find_`、`has_`、`count_`、`is_`、`peek_` 或任何描述只读访问的动词开头的 C 函数。
- 如果函数体不修改 `this` 或调用其上的修改方法，将 `this` 参数和返回的指针都改为 `const`：`const T *_Borrow method(const Container *_Borrow this, ...)`。
- 如果 C 风格的"getter"恰好在首次调用时延迟初始化、排序或缓存，它**实际上**不是只读的——保持非 const 并记录原因。

#### 原始指针——保持不注解
```c
// 这些保持为普通指针——没有 _Owned 或 _Borrow
unsigned char *cursor = buffer_at_offset(buf);      // 内部指针
const unsigned char *input_pointer = input + 1;     // 指针算术
unsigned char *output_pointer = output;              // 别名到拥有的缓冲区
char *after_end = nullptr;                           // 由 strtod 使用
```

### 第 4.5 步：将取地址（`&`）转换为 `&_Const` / `&_Mut`

在 BiSheng C 中，`_Safe` 区域**禁止普通 `&`**——你必须使用 `&_Const`（不可变借用）或 `&_Mut`（可变借用）。参见 `bsc-borrowing` 技能 §7（完整示例）和 `bsc-safe-zone` 技能了解完整语义。

#### 何时转换

| 上下文 | C 的 `&` | BSC 转换 |
|---------|-------|----------------|
| **`_Safe` 区域** | `&x` | 必须转换为 `&_Const x` 或 `&_Mut x` |
| **`_Unsafe` 区域** | `&x` | 可以保持为 `&x`（不需要转换） |
| **函数地址** | `&func` | 保持为 `&func`——例外：函数地址在 `_Safe` 中允许 |

#### 如何选择 `&_Const` vs `&_Mut`

| 情况 | 使用 | 原因 |
|-----------|-----|--------|
| 目标是 `const T *_Borrow` 或代码**只通过指针读取** | `&_Const x` | 不可变借用——只读，允许多个 |
| 目标是 `T *_Borrow` 且代码通过指针**写入** | `&_Mut x` | 可变借用——读/写，一次只能一个 |
| 传递给 `void foo(const T *_Borrow p)` | `&_Const x` | 函数期望只读借用 |
| 传递给 `void bar(T *_Borrow p)` | `&_Mut x` | 函数可能通过指针修改 |
| `const` 变量 / 字符串字面量 | 仅 `&_Const` | 不能使用 `&_Mut`——不可修改 |
| 全局变量（在 `_Safe` 中） | 仅 `&_Const` | `&_Mut` 在 `_Safe` 中禁止对全局变量使用 |

#### 示例

```c
// 转换前（C）
void init(int *out) { *out = 0; }
void read_val(const int *p) { (void)*p; }
void example(void) {
    int x = 42;
    int *p = &x;
    init(&x);
    read_val(&x);
}

// 转换后（BSC）——在 _Safe 区域中
_Safe void init(int *_Borrow out) { *out = 0; }
_Safe void read_val(const int *_Borrow p) { (void)*p; }
_Safe void example(void) {
    int x = 42;
    init(&_Mut x);                       // 写入 → &_Mut（没有其他活跃借用）
    read_val(&_Const x);                 // 只读参数 → &_Const
    const int *_Borrow r = &_Const x;    // 只读 → &_Const
    (void)*r;                            // 在作用域结束前使用 r
}
```

#### 从 `_Owned` 指针借用

```c
// 转换前（C）——指向堆对象的指针
Node *node = malloc(...);
process(&node->value);

// 转换后（BSC）
Node *_Owned node = safe_malloc<Node>((Node){0});
process(&_Mut node->value);    // 如果 process 只读取，则使用 &_Const
```

从拥有的指针本身（不是字段）：使用 `&_Const *ptr` 或 `&_Mut *ptr`：

```c
Node *_Owned node = create_node();
const Node *_Borrow ref = &_Const *node;   // 从拥有的指针借用
```

#### 何时不转换（保持普通 `&`）

- **在 `_Safe` 外**：在 `_Unsafe` 块或非 `_Safe` 函数中，普通 `&` 是有效的。
- **函数地址**：`&printf`、`&my_callback` ——在 `_Safe` 中允许。
- **输出/双指针模式**：接收 `&local_ptr` 的 `int **out` ——通常保持在 `_Unsafe` 中；如果在 `_Safe` 中，内部的 `&` 需要根据目标类型使用 `&_Mut`/`&_Const`。

#### 常见错误

```c
// 错误——& 在 _Safe 中被禁止
_Safe void f(void) { int x = 1; int *p = &x; }

// 正确
_Safe void f(void) { int x = 1; int *_Borrow p = &_Mut x; }

// 错误——在 const / 字面量上使用 &_Mut
const int c = 1;
int *_Borrow mp = &_Mut c;           // 错误：不可修改
char *_Borrow sp = &_Mut "hello";    // 错误：字符串字面量不可变

// 正确
const int *_Borrow r = &_Const c;
const char *_Borrow sr = &_Const "hello";

// 错误——在 _Safe 中对全局变量使用 &_Mut
int global_x;
_Safe void f(void) { int *_Borrow p = &_Mut global_x; }  // 错误

// 正确
_Safe void f(void) { const int *_Borrow p = &_Const global_x; }
```

### 第 5 步：处理特殊模式

#### 条件所有权（基于标志）
一些 C 代码使用标志来决定是否释放：
```c
if (!(item->type & IS_REFERENCE)) {
    free(item->child);  // 只有在我们拥有它时才释放
}
```
选项：
1. 将字段保持为原始 `T *` 并使用 `_Unsafe` 块进行释放
2. 重构为两种独立的类型（拥有的 vs 引用变体）
3. 保持为 `*_Owned` 并使用 `_Unsafe` 在设置标志时跳过释放

#### 字符串字面量 vs 分配的字符串
```c
const char *literal = "hello";                                         // 原始指针——字符串字面量，从不释放
char *_Owned allocated;                                                // 堆副本——必须释放
_Unsafe { allocated = __take_from_raw(strdup("hello")); }              // strdup 返回原始；通过 __take_from_raw 包装
```

#### 移植时决定 `struct` vs `_Owned struct`

C 只有一种聚合类型。BSC 有两种：

- **`struct S`** ——复制语义（如 C 结构体）。可能仍包含 `*_Owned` 字段，这种情况下 `S` 自动成为移动语义类型（参见用户手册 §2.3.2 中的 `is_move_semantic<T>`）。没有自动析构函数：每个 `*_Owned` 字段必须在每条代码路径上显式释放。
- **`_Owned struct S`** ——移动语义，整个值被追踪。可以有析构函数 `~S(S this) { ... }`，在作用域退出时自动运行。嵌套的 `_Owned struct` 成员由编译器自动析构；只有原始的 `*_Owned` 指针成员需要在 `~S` 中显式 `safe_free`。

**在以下任何条件成立时，将 C 结构体提升为 `_Owned struct`：**

1. 结构体"拥有"堆分配的资源（从 `malloc`/`calloc`/`strdup` 赋值的字段，或 `_Owned` 注解的字段）。
2. C 代码有相应的 `free_S(S* s)` / `S_destroy(S* s)` / 类似的遍历结构体字段的清理函数。
3. 实例在概念上是唯一的，不应该被位复制。

```c
// 转换前（C）
typedef struct Buffer {
    char *data;
    size_t len;
} Buffer;
void buffer_free(Buffer *b) { free(b->data); }

// 转换后（BSC）——提升为 _Owned struct
_Owned struct Buffer {
_Public:
    char *_Owned data;
    size_t len;
    ~Buffer(Buffer this) {
        safe_free((void *_Owned)this.data);
    }
};
// 调用者不再调用 buffer_free——析构函数自动触发。
```

**在以下情况下保持为普通 `struct`（不提升）：**

1. 结构体是类似 POD 的——只有值字段，没有堆指针，没有清理。
2. C 代码按值传递实例并期望廉价复制。
3. 你确实需要复制语义（例如 `Point`、`Rect`、`Color`）。

```c
// 保持为普通结构体——没有所有权，只是一对数字
struct Point { int x; int y; };
```

**带有 `*_Owned` 字段的普通 `struct` 是合法但危险的。** 编译器将其标记为移动语义但不生成自动析构函数——你必须在每个释放路径上手动释放拥有的字段。如果你发现自己这样做，请提升为 `_Owned struct`。

##### 析构函数顺序和"部分移动"规则

来自用户手册 §3.4.1.2：在作用域结束时，一个 `_Owned struct`（递归包括成员）必须处于恰好两种状态之一：

- **`_Owned`**：没有移出任何东西，析构函数将自动触发。
- **`moved`**：整个结构体被作为一个整体显式移出。

任何中间状态（例如，一个字段被移出，其余存活）会被拒绝，报错：`partially moved _Owned struct: X at scope end, Y moved`。**在移植一次释放一个字段然后忘记父结构的 C 代码时**，这条规则会抓到你。修复方法：要么将整个结构体移出，要么保持不动让析构函数运行。

#### 使用 `memcpy` / `memmove` / `memset` 进行结构体位复制

C 通常使用 `memcpy(dst, src, sizeof(T))` 来复制结构体。这是一个**类型擦除的位复制**：编译器的所有权追踪看不到它。如果 `T` 包含 `*_Owned` 或 `_Owned struct` 成员，原始 `memcpy` 会创建同一堆内存的两个拥有者——当两个析构函数触发时导致 double-free。

```c
// 不安全——r 内部的 _Owned 指针现在被 val 别名。
// 当 r 超出作用域时，~Resource(r) 释放 r.s。
// 当调用者使用 val 并丢弃它时，同一缓冲区再次被释放。
Resource r = { .s = safe_malloc<char>(100) };
memcpy(val, (const void *)&r, sizeof(Resource));
// BUG：pending double free。
```

**认可的模式（用户手册 §6.1.4）：** 将 `memcpy` 与 `forget<T>(r)` 配对。`forget` 获取 `r` 的所有权并将其丢弃而不调用析构函数。所有权已通过位复制转移到 `val`；`r` 的析构函数被正确抑制。

```c
#include "bishengc_safety.hbs"

_Unsafe void ffi_out(char *val) {
    Resource r = { .s = safe_malloc<char>(100) };
    memcpy(val, (const void *)&r, sizeof(Resource));
    forget<Resource>(r);   // 抑制 ~Resource；val 现在拥有 r.s
}
```

**移植 `memcpy`/`memmove` 在拥有类型上的规则：**

- 如果复制后源和目的地都将存活 → 你复制了所有权。使用源上的 `.clone()`（如果可用）而不是 `memcpy`。
- 如果源被复制"移出"（FFI 边界、手动移动）→ 将原始复制包装在 `_Unsafe` 中并立即在之后调用 `forget<T>(src)`，这样编译器不会触发源的析构函数。
- 对于在包含 `*_Owned` 字段的结构体上使用 `memset(s, 0, sizeof(T))`：同样的问题反转过来——它用 NULL 覆盖拥有的指针而不释放。通过赋值进行值替换，或先手动释放字段再包装在 `_Unsafe` 中。

将对拥有类型的 `mem*` 视为**边界操作**：`_Unsafe`，配以显式的所有权声明（`forget`、先前的 `safe_free` 或匹配的复制）。

#### 循环结构体：反向引用、父节点、图、观察者

BSC 的单所有者 `_Owned` 模型无法直接表达循环。有两种**惯用模式**——根据节点是否真正共享来选择：

| 情况 | 模式 |
|-----------|---------|
| **容器唯一拥有其节点**（典型链表、树、队列） | 原始 `Node*` 字段 + `_Unsafe` 操作 + 自定义 `~Container` 析构函数，遍历并释放。没有 `Rc`/`Weak`。 |
| **节点在多个所有者之间共享**，或者**反向引用需要在某些所有者丢弃后仍可存活**，或者**你想要自动析构而无需编写遍历并释放的析构函数** | 拥有引用的 `Rc<T>`，反向引用的 `Weak<T>`，通过共享引用进行修改的 `RefCell<T>`。来自用户手册 §6.3 的模式。 |

标准库自己的 `LinkedList<T>`（libcbs `list.hbs`）使用**第一种模式**——`_Unsafe` 中的原始指针——正是因为列表唯一拥有其链。如果自定义析构函数能做到，不要使用 `Rc`/`Weak`。

##### 模式 A——原始指针 + `_Unsafe` + 自定义析构函数

当容器是唯一所有者时使用。`_Owned struct` 包装根指针；析构函数遍历并释放节点。公开 API 保持 `_Safe`。

```c
typedef struct SNode SNode;
struct SNode {
    int           value;
    struct SNode *next;   // 原始；间接由列表拥有
};

_Owned struct SList {
_Public:
    struct SNode *head;
    size_t        count;

    ~SList(SList this) {
        _Unsafe {
            struct SNode *n = this.head;
            while (n != NULL) {
                struct SNode *nxt = n->next;
                free((void *)n);
                n = nxt;
            }
        }
    }
};
// push/pop/etc：_Safe 体，带有围绕 malloc/free 的小型 _Unsafe 块。
```

前向声明使用 `typedef struct X X;` ——除非存在 typedef，否则 BSC 要求在原始引用上使用 `struct` 标签。

##### 模式 B——`Rc<T>` / `Weak<T>` / `RefCell<T>`

当节点共享时使用，或者当反向引用必须在某些所有者丢弃后仍然可用时使用。手册的 §6.3.6.4 树示例是规范案例。

```c
// 正确：RefCell<Option<Weak<Node>>>——Option::None 状态是你在任何父节点存在之前
// 构造节点的方式。Weak<T>::new 需要一个现有的 Rc<T> 来借用，所以没有"默认 Weak"——
// Option 填补了这个空白。
_Owned struct Node {
_Public:
    int value;
    RefCell<Option<Weak<Node>>>    parent;    // 反向引用；在连接前为 None
    RefCell<Vec<Rc<Node>>>         children;  // 拥有的前向引用
};

_Safe Node Node::new(int v) {
    Node n = {
        .value    = v,
        .parent   = RefCell<Option<Weak<Node>>>::new(Option<Weak<Node>>::None()),
        .children = RefCell<Vec<Rc<Node>>>::new(Vec<Rc<Node>>::new()),
    };
    return n;
}

// 连接：父节点通过 Rc::clone 拥有子节点，子节点通过 Weak 反向引用。
Rc<Node> root = Rc<Node>::new(Node::new(10));
Rc<Node> leaf = Rc<Node>::new(Node::new(5));
root.deref()->children.borrow_mut().deref()->push(leaf.clone());
*leaf.deref()->parent.borrow_mut().deref() =
    Option<Weak<Node>>::Some(Weak<Node>::new(&_Const root));
```

**为什么 `Option<Weak<T>>` 而不是仅 `Weak<T>`：** `Weak<T>::new(const Rc<T>*)` 是唯一构造函数——没有 `Weak::none()` / 默认值。要在父节点存在之前创建 `Node`（叶优先构造顺序所需的），`parent` 字段必须以合法为空的状态开始。`Option` 提供了这个。用户手册中的示例完全省略了 `parent` 初始化器，这将使其处于不确定状态——不要复制它。

**为什么 `Rc` 和 `Weak` 都需要：** 仅 `Rc<T>` 会创建不朽的循环（每个节点的引用计数保持 ≥1，因为它的邻居引用它）。`Weak<T>` 不计入引用计数，所以当拥有链丢弃其最后一个 `Rc` 时，析构函数正常触发。用户手册 §6.3.6 展示了没有 `Weak` 时的泄漏以及使用 `Weak` 的解决方法。

**为什么 `RefCell`：** BSC 的借用检查器禁止通过 `const T *_Borrow` 进行修改（这就是 `Rc::deref` 返回的内容）。如果你的循环结构体需要修改（添加子节点、重新连接边），将可从共享引用的可变字段包装在 `RefCell` 中。`borrow_mut()` 提供一个 `RefMut<T>`，其 `deref()` 是 `T *_Borrow`。违反会在**运行时**中止，而不是编译时。

##### 翻译表

| C 模式 | 推荐的 BSC 翻译 |
|-----------|-----------------------------|
| 单链或双链表，其列表结构体是唯一所有者 | 模式 A（原始 + `_Unsafe` + 自定义析构函数）。以标准库 `LinkedList<T>` 为模型。 |
| 从单个根遍历的树，无反向引用 | 普通 `_Owned struct` 搭配 `Vec<Node>` 子节点。递归处理遍历；自动析构函数处理清理。不需要 `Rc`。 |
| 带有父反向引用的树（调用者有时从叶到根遍历） | 模式 B，使用 `RefCell<Option<Weak<Node>>>` 父节点、`RefCell<Vec<Rc<Node>>>` 子节点。 |
| 一个节点可以从多个前驱到达的图 | 模式 B：边通过 `Rc<Node>` 拥有目标；如果边应该是非拥有视图，使用 `Weak<Node>` 并在访问时升级。 |
| 观察者 / 监听者模式 | 主题 → 通过 `Vec<Weak<Observer>>` 的观察者；观察者 → 通过 `RefCell<Option<Weak<Subject>>>` 的主题。 |

**何时不使用 `Rc`/`Weak`：** 如果容器唯一拥有其节点且没有其他人引用它们，使用模式 A。它更短、更便宜且经过验证——标准库遵循它。只有当共享或从叶向上的遍历确实需要时，Rc+Weak 的开销才值得。

参见 `bsc-stdlib-advanced` 技能了解完整的 `Rc`/`Weak`/`RefCell`/`Cell` API。

#### C 面向对象模式（vtable / METHOD 宏）

许多 C 代码库通过函数指针结构体（"vtables"）实现 OO 风格的多态。一个典型模式（由 strongSwan 的 `METHOD` 宏和类似辅助工具使用）：

```c
/* C 原始 */
typedef struct public_t public_t;
struct public_t { void (*write)(public_t *this, uint8_t v); };

typedef struct { public_t public; uint8_t *buf; size_t used; } private_t;

METHOD(public_t, write, void, private_t *this, uint8_t v)
{
    this->buf[this->used++] = v;
}
// 扩展为：接受 private_t* 的函数体，加上用于 vtable 的原始 fn-ptr 别名 _write。
```

**为什么 `METHOD`（以及任何 `transparent_union` 方法）无法移植到 BSC：**

1. BSC 混合模式声明（`_Unsafe` 函数的 `_Safe` 重载）要求参数列表具有相同的类型，仅为原始指针参数添加 `_Owned`/`_Borrow` 限定词。`transparent_union` 将*联合体*作为第一个参数——不是原始指针——所以混合模式无法应用。

2. **在不同结构体指针参数类型之间的函数指针转换在异构 `_Safe`/`_Unsafe` 声明（混合模式机制）中被禁止。** 无论如何都要使用 body+trampoline 模式——trampoline 为你提供正确类型的参数，没有任何 fn-ptr 转换。

**手工编写的 body + trampoline 模式：**

每个 vtable 槽位写两个 `_Safe` 函数：

- **Body** ——接受 `private_t *_Borrow _Nonnull this`，包含所有逻辑。
- **Trampoline** ——接受 `public_t *_Borrow _Nonnull self`（匹配 vtable 槽位类型），通过 void-borrow 两步转换转换为 `private_t *_Borrow`，然后调用 body。

```c
/* Body — _Safe，操作 private_t */
_Safe static void write_uint8(private_t *_Borrow _Nonnull this, uint8_t value)
{
    _Unsafe this->buf[this->used] = value;   // 原始字段下标
    this->used += 1;
}

/* Trampoline — _Safe，参数类型匹配 vtable 槽位 */
_Safe static void _write_uint8(public_t *_Borrow _Nonnull self, uint8_t value)
{
    private_t *_Borrow _Nonnull this = _Unsafe(
        (private_t *_Borrow _Nonnull)(void *_Borrow _Nonnull)self);
    write_uint8(this, value);
}
```

转换链 `(private_t *_Borrow _Nonnull)(void *_Borrow _Nonnull)self`：
- `public_t *_Borrow → void *_Borrow` ——隐式，在 `_Safe` 中允许。
- `void *_Borrow → private_t *_Borrow` ——显式转换，包装在 `_Unsafe(expr)` 中。
- 直接的 `public_t *_Borrow → private_t *_Borrow` 被**禁止**（不同的指向类型）。

**Vtable 初始化**分配 trampoline（其参数类型匹配 vtable 槽位），而不是 body：

```c
public_t *_Owned _Nullable create(void)
{
    private_t *this;
    INIT(this,
        .public = {
            .write_uint8 = _write_uint8,   // trampoline — 不是 write_uint8
            .destroy     = _destroy,
        },
    );
    return __take_from_raw(&this->public);
}
```

**Destroy body 注意事项。** `this` 是 `_Borrow`，所以 BSC 不模型化释放它——将所有释放包装在 `_Unsafe` 中：

```c
_Safe static void destroy(private_t *_Borrow _Nonnull this)
{
    _Unsafe {
        free(this->buf);
        free((void *)(void *_Borrow _Nonnull)this);   // 两步剥离 _Borrow
    }
}
```

**警告——这是一个静默的 use-after-free 风险。** 编译器的所有权模型看不到通过 `_Borrow` 的 `free(this)`。任何在 `destroy` 返回后继续持有或使用同一对象的 `_Owned` 或 `_Borrow` 的调用者将得到运行时的 use-after-free，而检查器没有捕获。这种逃逸仅在 vtable 协议要求 trampoline 参数是 `public_t *_Borrow` 时才可接受——C 接口是固定的，而 `_Owned` 替代方案会破坏 vtable 契约。在普通（非 vtable）代码中，总是在销毁函数中使用 `_Owned this`，以便编译器模型化释放。

**移植 C vtable 结构体时的检查清单：**
- [ ] 不使用 `METHOD` 宏或 `transparent_union` 技巧
- [ ] 每个槽位写一个 `_Safe` body——接受 `private_t *_Borrow _Nonnull`
- [ ] 每个槽位写一个 `_Safe` trampoline——接受 `public_t *_Borrow _Nonnull`
- [ ] 在 trampoline 内部转换：`_Unsafe((private_t *_Borrow _Nonnull)(void *_Borrow _Nonnull)self)`
- [ ] 在 vtable 初始化器中分配 trampoline（不是 body）
- [ ] 在 body 函数中将所有原始字段索引包装在 `_Unsafe` 语句/块/表达式中
- [ ] 在 destroy body 中，先在 `_Unsafe` 中释放所有原始缓冲区字段，然后释放结构体指针

#### 函数指针钩子（自定义分配器）

如果 C 代码使用函数指针钩子进行内存分配（例如，可配置的 `allocate`/`deallocate` 结构体），你必须用直接的 `malloc`/`free`/`realloc` 调用替换它们。这是必需的，因为：
- BSC 所有权注解需要知道实际的分配器——函数指针间接隐藏了这一点
- 保持钩子并使所有指针保持原始会破坏翻译到 BSC 的目的
- 目标是在类型级别表达所有权，而不是保留运行时分配器的可配置性

```c
// 转换前（C）——自定义分配器钩子
typedef struct {
    void *(*allocate)(size_t);
    void (*deallocate)(void *);
} hooks;
static hooks global_hooks = { malloc, free };
// ... global_hooks.allocate(size) 到处都是 ...

// 转换后（BSC）——移除钩子，使用带 _Owned 的直接调用
// 完全删除 hooks 结构体
// 将 global_hooks.allocate(size) → malloc(size) 在 _Unsafe 中，然后 __take_from_raw
// 将 global_hooks.deallocate(p) → safe_free((void *_Owned)p)
```

不要使用"代码有基于钩子的分配器"作为跳过所有权注解的理由。移除钩子并注解。

### 第 6 步：处理 goto + 所有权清理

C 代码经常使用 `goto fail` 进行错误处理。在 BSC 中，`_Owned` 变量必须在所有路径上被正确消耗：

```c
// 转换前（C）
Node *item = create_item();
if (!item) goto fail;
// ... 更多工作 ...
return item;
fail:
    free(item);
    return NULL;

// 转换后（BSC）——使用 _Nullable，因为 item 可能在清理时为 NULL
Node *_Owned _Nullable item = create_item();
if (item == nullptr) goto fail;
// ... 更多工作 ...
return item;  // 所有权转移给调用者
fail:
    safe_free((void *_Nullable _Owned)item);
    return nullptr;
```

### 第 6.5 步：微妙的表达式翻译

BSC 是 C 的*超集*，因此表达式级别的 C 仍然可以编译。但是当你重构代码时——特别是拆分行以适应所有权模型——一些 C 惯用法会静默改变含义。显式检查这些。

#### 循环中的后自减 / 后自增

C 的 `while (length--)` 在条件检查**后**递减。如果你在 BSC 中跨行拆分（当你需要插入边界检查或借用时常见），顺序会翻转。

```c
// C 原始——length 在测试后递减
while (length--) {
    if (strchr("xX", string[length])) { ... }
}

// 错误的 BSC 翻译——在第一次迭代时访问空终止符
while (length) {
    if (strchr("xX", string->at(cur_index + length))) { ... }
    length -= 1;
}

// 正确——在访问前递减
while (length) {
    length -= 1;
    if (strchr("xX", string->at(cur_index + length))) { ... }
}
```

在实际中看到的真正 bug：一个数字解析谓词静默拒绝每个输入，因为在第一次迭代时它访问了位置 `length`（空终止符）。`strchr("xX", '\0')` 然后返回非 NULL——`strchr` 在*neddle*字符串中找到尾随 `\0`——所以守卫在每次调用时触发。

#### 基于指针的偏移 vs 基于索引的偏移

C 使用 `string + start` 指向缓冲区内部。在 BSC 中使用 `String`，你有一个进入容器的索引 `start`——基指针移动了，但你的索引算术相对于容器的起点。

```c
// C：指针 p 随调用者的位置移动
strncmp(p, "-0", 2)

// 错误的 BSC：从整个字符串的开头比较，而不是从 cur_index
strncmp((const char*)string->as_str(), "-0", 2)

// 正确：显式偏移到容器中
strncmp((const char*)string->get(cur_index), "-0", 2)
```

在实际中看到的真正 bug：一个数字解析谓词将 `"-0"` 与整个输入缓冲区的开头而不是数字的起始偏移比较。C 代码将偏移内置在 `p` 中；BSC 移植在 `p` 变成 `String` 引用时丢失了它。

#### 字符串字面量的 `sizeof(expr) - 1`

像 `SIZEOF_TOKEN(s)`（`sizeof(s) - 1`）这样的 C 宏在 BSC 中工作相同，但仅适用于**数组**字面量。如果你不小心传递了 `String` 或 `const char *`，`sizeof` 给出的是指针大小，而不是字符串长度。审计每个对字符串的 `sizeof`。

#### 对 `String` 内容的指针算术

C 中的 `string[i]` 变成 BSC 中的 `string->at(i)`——它做**边界检查**。如果你的 C 代码依赖于读取超过已知长度（在快速词法分析器中常见），你现在会得到越界 panic。在显式边界内使用 `string->at(i)`，或通过 `as_str()` + `_Unsafe` 使用原始缓冲区。

#### 移植写通过游标循环时不要遗漏默认发射分支

许多 C 例程——序列化器、编码器、词法分析器、格式化器——使用移动游标来发射字节：

```c
// C 模式
for (i = 0; i < len; i++) {
    c = input[i];
    switch (c) {
        case '\n': *buf++ = '\\'; *buf++ = 'n'; break;
        case '\t': *buf++ = '\\'; *buf++ = 't'; break;
        /* ...其他转义情况... */
        default:   *buf++ = c;          /* <-- 重要的分支 */
    }
}
```

当你重构 `buf` 并切换到输出构建器（例如 `out->push(c)`）时，本能是移植每个 `case` 然后就继续。很容易忘记 `default` 分支——代码仍然编译、仍然运行，但静默地丢弃每个不匹配 case 的字节：

```c
// 错误的移植——default 分支丢失
for (size_t i = 0; i < len; i++) {
    char c = input->at(i);
    switch (c) {
        case '\n': out->push('\\'); out->push('n'); break;
        case '\t': out->push('\\'); out->push('t'); break;
        default:   break;                /* BUG：没有发射任何东西 */
    }
}

// 正确的移植——default 镜像 C 的游标写入
for (size_t i = 0; i < len; i++) {
    char c = input->at(i);
    switch (c) {
        case '\n': out->push('\\'); out->push('n'); break;
        case '\t': out->push('\\'); out->push('t'); break;
        default:   out->push(c);         /* 按原样发射字节 */
    }
}
```

**规则：** 对于每个通过旧游标惯用法（`*buf++ = ...`）写入输出的 C `case`/`default`，在移植中编写等效的 `out->push(...)`（或输出类型中的 BSC 等效）。在断定移植完成之前，并排比较两者。

**如何注意到这个 bug：** 输出长度比预期短得多，或者与已知良好输出比较的测试失败，显示"前缀匹配，中间缺失"。

#### 移植推进 `const char *` 的 C 迭代器循环

C 通过指针推进迭代 C 字符串：

```c
// C 模式
const char *p = input;
while (*p != '\0') {
    emit(*p);
    p++;
}
```

你不能直接将其移植到 `_Safe` 中的 `const char *_Borrow`：`_Borrow` 指针禁止索引 `p[i]` 和算术 `p + n` / `p++`（参见 `bsc-borrowing` §5 和 §8）。三种合法的移植：

1. **索引 + 基指针借用**——保持原始基地址，在索引上循环，一个紧密的 `_Unsafe` 块用于解引用：
   ```c
   const char *base = input;   // 原始，不是 _Borrow
   size_t i = 0;
   _Unsafe {
       while (base[i] != '\0') { emit(base[i]); i++; }
   }
   ```

2. **转换为 `String` 类型的输入**——惯用的重写，使用边界检查索引：
   ```c
   for (size_t i = 0; i < input->length(); i += 1) {
       char c = input->at(i);
       if (c == '\0') break;
       emit(c);
   }
   ```

3. **固定字面量辅助函数**——如果被迭代的"字符串"是一小组编译时常量（例如 "null"/"true"/"false" 标记），根本不迭代。每个字面量写一个辅助函数，直接将字节推送到输出。零 `_Unsafe`、零分配、无循环。

根据字节的来源选择：用户输入 → #2；已知字面量 → #3；原始互操作缓冲区 → #1。

### 第 7 步：编译和修复

```bash
clang file.cbs -o output
```

BSC 编译器会报告所有权违规的错误。迭代修复它们。

## 常见翻译错误（来自真实项目）

这些是将 C 翻译到 BSC 时最常见的错误。对照每一个检查你的翻译。

### 错误 1：`_Owned` 指针初始化为 NULL 但没有 `_Nullable`
```c
// 错误——_Owned 默认不可空
cJSON *_Owned root = NULL;

// 修复
cJSON *_Owned _Nullable root = nullptr;
```
**何时发生**：任何从 NULL 开始的 `_Owned` 局部变量，或任何将 `_Owned` 设置为 NULL 的错误路径。

### 错误 2：将 `_Owned` 指针传递给期望原始指针的函数
```c
// 错误——internal_func 期望 raw T*，但 item 是 _Owned
void internal_func(cJSON *item);
cJSON *_Owned item = create_item();
internal_func(item);  // 错误：所有权不匹配

// 修复选项 A：如果函数消耗则使其接受 _Owned
void internal_func(cJSON *_Owned item);

// 修复选项 B：借用-然后-转换（函数临时借用；item 保持所有权）
//   `(cJSON *)item` 是直接的 owned→raw 转换——禁止，即使在 _Unsafe 中。
//   正确的模式是：先借用，然后将借用转换为原始。
_Unsafe { internal_func((cJSON *)&_Mut *item); }

// 修复选项 C：使函数接受 _Borrow（当函数不释放时最好）
void internal_func(cJSON *_Borrow item);
```

### 错误 3：将 `_Owned` 返回值赋值给原始变量
```c
// 错误——create 返回 _Owned，但结果是原始的
cJSON *result = cJSON_CreateObject();  // 所有权泄漏

// 修复
cJSON *_Owned result = cJSON_CreateObject();
```

### 错误 4：从声明返回 `_Owned` 的函数返回原始指针
```c
// 错误——原始 `item` 不能作为 _Owned 返回
cJSON *_Owned detach_item(cJSON *parent) {
    cJSON *item = parent->child;   // 来自结构体字段的原始指针
    return item;                    // 错误：raw → _Owned 转换
}
```
```c
// 修复——使用 __take_from_raw 从原始指针转移所有权。
//   (cJSON *_Owned)item 是一个 raw→owned 的 C 转换——禁止，即使在 _Unsafe 中。
cJSON *_Owned detach_item(cJSON *parent) {
    cJSON *item = parent->child;
    // 如果调用者必须确保 item 非空，先空检查。
    _Unsafe { return __take_from_raw(item); }
}
```
**注意**：`__take_from_raw` 保留其参数的可空性。如果 `item` 可能为空且可空性检查开启，要么先空检查，要么将返回类型声明为 `cJSON *_Owned _Nullable`。

### 错误 5：对 `_Owned` 指针使用 `NULL` 而不是 `nullptr`
```c
// 错误——NULL 是 C 宏，不是 BSC 感知的
cJSON *_Owned _Nullable item = NULL;

// 修复——在 BSC 中使用 nullptr
cJSON *_Owned _Nullable item = nullptr;
```

### 错误 6：所有权转移到容器然后继续使用
```c
// 错误——在 AddItemToObject 后，item 已被移动，调用者不能使用它
cJSON *_Owned item = cJSON_CreateObject();
cJSON_AddItemToObject(root, "key", item);     // item 所有权已移动
cJSON_AddStringToObject(item, "name", "val"); // 错误：移动后使用
```

**修复选项 A——在转移所有权之前填充**（首选：不需要别名）：
```c
cJSON *_Owned item = cJSON_CreateObject();
cJSON_AddStringToObject(&_Mut *item, "name", "val"); // 在仍拥有时修改
cJSON_AddItemToObject(root, "key", item);             // 最后转移
```

**修复选项 B——转移后从容器重新借用**（当修改必须在 `root` 看到 item 之后完成时）：
```c
cJSON *_Owned item = cJSON_CreateObject();
cJSON_AddItemToObject(root, "key", item);             // item 已移动
cJSON *_Borrow ref = cJSON_GetObjectItem(root, "key"); // 从 root 查找
cJSON_AddStringToObject(ref, "name", "val");           // 通过借用修改
```

**不要**保存 `item` 的 `cJSON *` 原始指针"别名"并在移动后使用它。在 BSC 中，`_Owned` 的原始别名在 `_Safe` 中是类型错误，在 `_Unsafe` 中是移动后使用风险。选择选项 A 或 B。

## 验证工作流——报告成功的停止条件

一个 C 到 BSC 的翻译直到项目编译命令干净运行**并且**现有测试套件通过才**完成**。这是不可协商的；借用检查器错误正是使翻译正确的因素，一个"完成的"但不编译的翻译是一个隐藏错误的翻译。

### 步骤 A——找到项目的编译命令

在 `CLAUDE.md`（或 `AGENTS.md`）中查找标有"BSC Project Compile Command"或"Compile command for this project"的部分。这是由 `install.sh -a claude-plugin` 设置的，包含此项目的精确编译器路径、包含标志和验证命令。

如果该部分仍然是 `[FILL IN: ...]` 或完全缺失：

> **停止。不要猜测。** 不要回退到 `clang file.cbs -o output` 或 `clang -x bsc file.c -fsyntax-only`——这些对几乎所有真实项目都会失败（错误的编译器、缺失的包含）。询问用户：
>
> *"我在 `CLAUDE.md` 中没有看到编译命令。在这个项目中编译和验证 BSC 文件的确切命令是什么？（编译器路径、包含标志以及如何运行测试套件）"*
>
> 在编写或修改任何 BSC 代码之前等待用户的回答。一个在你的头脑中编译但在项目工具链下不编译的翻译比没有翻译更糟糕。

### 步骤 B——在每次有意义的更改后编译

在翻译一个文件（或一个连贯的块）后，在其上运行项目的验证命令。在转到下一个文件之前修复每个诊断。不要批量处理十几个翻译后的文件并期望它们都在最后编译通过——所有权错误一次修复一个更容易，而且在一个文件的工作量之后的 `_Owned`/`_Borrow` 模型的心理状态在文件还是新鲜的时候最清晰。

### 步骤 C——运行现有测试套件

翻译后的代码必须通过**项目的现有测试**。测试是契约。一个编译通过但破坏测试的翻译已经改变了程序的行为，这意味着所有权注解是错误的（你移动了不应移动的东西，丢弃了需要存活的东西，或引入了意外的复制/修改）。

如果项目没有测试，在报告中明确说明：

> *"翻译已编译，但项目没有我可以运行的测试套件来验证行为。该翻译是语法上有效的 BSC，但未经过行为验证。"*

不要将发明测试作为翻译工作的一部分——那是单独的任务。报告这个差距。

### 步骤 D——报告

只有在步骤 A–C 通过后，你才报告翻译完成。报告必须按此顺序说明：

1. 你运行的确切编译命令以及它返回了 0。
2. 你运行的确切测试命令以及它返回了 0（或不存在测试套件）。
3. 所有权决策的摘要（`_Owned`、`_Borrow`、`_Unsafe` 块的数量；任何你升级到 `_Unsafe` 而不是注解的情况）。

任何少于这些的——"我认为它应该编译"、"借用检查器可能抱怨 X"、"测试可能仍然通过"——都**不算完成**。回到步骤 B。

## 检查清单——在完成前验证

- [ ] 文件扩展名**保持**为 `.c`/`.h` ——使用 `clang -x bsc file.c` 编译（不要重命名）
- [ ] `#include` 路径不变——头文件仍然引用 `.h`，不是 `.hbs`
- [ ] `#include "bishengc_safety.hbs"` 在使用 safe_malloc/safe_free 的地方添加（这是唯一的新引入）
- [ ] 平台特定代码**保留在原处**——`#ifdef _WIN32`、`__declspec`、`__cdecl`、`extern "C"` 保护都保留；BSC clang 处理它们
- [ ] **每个指针都已分析**——分类为拥有的、借用的或原始的
- [ ] **拥有的结构体字段**注解为 `*_Owned`
- [ ] **借用的结构体字段**注解为 `*_Borrow`
- [ ] **内部/算术/迭代器指针**保持为原始 `T *`
- [ ] **_Safe 中的取地址**：`&x` 转换为 `&_Const x`（只读）或 `&_Mut x`（可变）；`&_Mut` 不使用在 const、字面量或全局变量上
- [ ] **单值分配**尽可能转换为 `safe_malloc<T>(val)`
- [ ] **变长分配**（malloc/calloc/strdup/realloc）通过 `_Unsafe` 中的 `__take_from_raw(raw_ptr)` 处理——直接的 `(T *_Owned)` C 转换在所有地方都被禁止，不仅仅在 `_Safe` 中
- [ ] **将原始指针返回为 `_Owned`**：使用 `__take_from_raw(raw_ptr)`，不是 C 转换
- [ ] **传递 `_Owned` 给期望原始指针的地方**：使用借用-然后-转换 `(T *)&_Mut *p`（保持所有权）或 `__move_to_raw(p)`（移出所有权）
- [ ] **每个 free()** 都转换为 `safe_free((void *_Owned)p)` 或 `safe_free((void *_Nullable _Owned)p)`
- [ ] **每个函数签名中的每个指针都已注解**——任何参数或返回类型中都没有裸 `T *`，除非它是四种显式的原始情况之一（输出双指针、函数指针、不透明转换中介、算术游标）。决策在 `.h`/`.hbs` 声明和 `.c`/`.cbs` 定义之间匹配。
- [ ] **函数参数**根据决策树注解为 `_Owned` / `_Borrow` / `const _Borrow`
- [ ] **函数返回**注解为 `_Owned`（调用者释放）或 `_Borrow`（调用者不释放）
- [ ] **只读 getter 是 `const`**：名为 `get_`/`find_`/`has_`/`count_`/`is_`/`peek_` 且不修改的函数接受 `const T *_Borrow this` **并**返回 `const ...*_Borrow`——否则来自 `const` 上下文的调用者无法访问它们
- [ ] **默认发射分支已保留**：switch/default 中的每个 C `*buf++ = c` 都移植到 BSC 中的等效 `out->push(c)`（静默丢弃的字节是经典的移植 bug）
- [ ] **对 `const char *` 的指针推进循环**：移植为一个 `_Unsafe` 中的索引 + 基指针，或重写为带长度边界的 `String::at(i)`，或当来源是编译时常量时替换为每个字面量的辅助函数
- [ ] **结构体提升已决定**：每个具有堆拥有字段或匹配的 `*_free` 函数的 C 结构体被提升为带析构函数的 `_Owned struct`；纯值结构体保持普通 `struct`
- [ ] **对拥有类型没有原始 `mem*`**：每个包含 `*_Owned` 或 `_Owned struct` 成员的结构体上的 `memcpy`/`memmove`/`memset` 被包装在 `_Unsafe` 中并与 `forget<T>(src)`（所有权已转移）或之前的字段 `safe_free`（预覆盖）配对
- [ ] **循环和共享所有权已审慎决定**：单所有者列表/树使用模式 A（原始指针 + `_Unsafe` + 自定义析构函数，如标准库 `LinkedList<T>`）。仅在节点真正共享或需要从叶向上的遍历时才使用模式 B（`Rc`+`Weak`+`RefCell`）。`Weak<T>` 字段的类型为 `Option<Weak<T>>`，因为 `Weak::new` 需要一个现有的 `Rc`。
- [ ] **条件所有权**模式使用 `_Unsafe` 或重构处理
- [ ] **`_Owned` + NULL**：每个可能为空的 `_Owned` 指针使用 `_Nullable` 和 `nullptr`
- [ ] **没有移动后使用**：在将 `_Owned` 传递给消耗函数后，只使用原始别名
- [ ] **`goto` 清理路径**：错误处理程序中释放的变量使用 `_Owned _Nullable`
- [ ] **限定词放置**：`T *_Owned`，从不 `_Owned T*`
- [ ] **非零注解**：翻译在分配函数、释放函数和拥有的结构体字段上包含 `_Owned`。零个 `_Owned` = 不完整的翻译。
- [ ] **没有钩子/分配器间接**：自定义分配器结构体已移除，替换为直接的 malloc/free
- [ ] **可空性**：可能为空的指针使用 `_Nullable`；在 BSC 中使用 `nullptr` 而不是 `NULL`
- [ ] **初始化**：变量在 `_Safe` 区域中使用前已初始化；数组使用初始化列表（不是逐个元素）
- [ ] **项目编译命令**（来自 `CLAUDE.md`）在更改的文件上干净运行。不是通用的 `clang file.cbs`——项目的实际命令。如果 `CLAUDE.md` 没有编译命令，模型已**停止**并询问了用户（参见"验证工作流"）
- [ ] **项目测试套件**在更改的文件上干净运行（或者明确报告了没有测试套件）
- [ ] **每个 `_Unsafe` 块已审查**：封闭函数如果其接口允许则是 `_Safe`；块只包含实际需要逃逸的行
- [ ] **编译器驱动的 `_Unsafe`**：差异中的每个 `_Unsafe` 对应于一个无法通过调整注解或在 `_Safe` 区域形式中重写来修复的特定编译器诊断。没有 `_Unsafe` 是预先添加的。如果删除 `_Unsafe` 后文件仍然编译，删除它。
