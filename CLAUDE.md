
# 工作原则

## 1. 先思考，再编码

不要假设。不要隐藏疑惑。主动暴露权衡。

实现之前：

- 明确陈述你的假设。如果不确定，先问。
- 如果有多种理解，呈现出来 — 不要无声地选择。
- 如果有更简单的方案，说出来。有理由时直接反对。
- 如果有不清楚的地方，停下来。说出困惑之处。发问。

## 2. 简单优先

用最少的代码解决问题。不写投机性代码。

- 不添加未被要求的功能。
- 不为单一用途创建抽象。
- 不添加未被要求的"灵活性"或"可配置性"。
- 不为不可能发生的场景添加错误处理。
- 如果你写了 200 行但 50 行就够，重写。

自问："资深工程师会觉得这是过度设计吗？" 如果是，简化它。

## 3. 精准修改

只改必须改的。只清理自己引入的问题。

编辑已有代码时：

- 不要"改进"相邻的代码、注释或格式。
- 不要重构没有坏的东西。
- 匹配现有风格，即使你会采用不同做法。
- 如果你注意到无关的死代码，提出来 — 不要删除它。

当你的修改产生孤立物时：

- 移除你修改导致不再使用的导入/变量/函数。
- 不要删除预先存在的死代码，除非被要求。

测试标准：每一行修改都应直接追溯到用户的需求。

## 4. 目标驱动执行

定义成功标准。循环直到验证通过。

将任务转化为可验证的目标：

- "加校验" → "为非法输入写测试，然后让测试通过"
- "修 bug" → "写复现用的测试，然后修复"
- "重构 X" → "确保重构前后测试均通过"

对于多步骤任务，给出简要计划：

1. [步骤] → 验证: [检查]
2. [步骤] → 验证: [检查]
3. [步骤] → 验证: [检查]

强有力的成功标准让你可以独立循环。模糊的标准（"搞出来就行"）需要不断澄清。

---

# BiSheng C (BSC) — 写 .c 代码前必读

## 指针限定词语法（关键）

`_Owned` 和 `_Borrow` 是**指针限定词**，类似 `const`。它们放在 `*` 之后，绝不放在类型之前。

语法：`base_type *qualifier variable_name`

把它想成 `const`：写 `int *const p`，而不是 `const int* p`。
同样规则：`int *_Owned p`，不是 `_Owned int* p`。

### 正确

```c
char *_Owned msg = (char *_Owned)malloc(14);
int *_Owned p = (int *_Owned)malloc(sizeof(int));
const int *_Borrow r = &_Const x;
int *_Borrow mr = &_Mut y;
free((void *_Owned)p);
```

### 错误 — 导致编译错误: "unknown type name '_Owned'"

```c
_Owned char* msg = malloc(14);     // 错误 — _Owned 不是类型
_Borrow int* r = &_Const x;       // 错误 — _Borrow 不是类型
```

**返回 BSC 代码前自检**：如果你写了 `_Owned T*` 或 `_Borrow T*`，
重写为 `T *_Owned`。模式始终是 `T *_Owned`。

## 项目代码风格（强制）

本项目不使用 libcbs 标准库（String、Vec 等）。使用自定义 C 风格实现。

### 类型定义

- **禁用 `_Owned struct`**。所有类型使用 `typedef struct { ... } TypeName;`
- 每个类型提供显式 `_free()` 函数手工管理生命周期。**绝不依赖析构函数。**
- 禁用泛型（`<T>` 语法）。每种类型单独定义。

```c
// 正确: plain struct + 显式 _free
typedef struct Foo { cstring name; int count; } Foo;
_Safe void Foo_free(Foo f) {
    cstring_free(f.name);
}

// 错误: _Owned struct
_Owned struct Foo { _Public: cstring name; ~Foo(Foo this) { ... } };
```

### 堆内存管理

- 堆缓冲区使用 `_Owned _ArrayElem` 指针跟踪所有权（仅限简单元素类型如 `char`）
- `_Owned _ArrayElem` **不能**指向含 `_Owned` 字段的类型。对于含 `_Owned` 字段类型的数组，使用 raw 指针 + 手工 `malloc/realloc/free`。
- 每个 `_Owned` 值在离开作用域前必须被消费（通过 `_free()` 或移动）。

```c
// cstring: 使用 _Owned _ArrayElem 跟踪 char 数组
typedef struct cstring {
    char *_Owned _ArrayElem buf;  // _Owned 守卫堆内存
    size_t len;
    size_t cap;
} cstring;

// Request: 使用 raw 指针动态数组（Header 含 _Owned 字段，不能用 _ArrayElem）
typedef struct Request {
    cstring method;
    Header* headers_data;         // raw 指针，malloc/realloc 管理
    size_t headers_len;
    size_t headers_cap;
} Request;
```

### 函数调用风格

- **禁用成员函数**（`.` 调用语法）。全部使用自由函数。
- 可变借用：`func_name(&_Mut s, args)`
- 不可变借用：`func_name(&_Const s, args)` 或 `func_name(s, args)`（s 已是 borrow 指针）
- 所有权转移：`func_name(s)` 按值传递，消费实参

```c
// 正确: 自由函数
cstring_push(&_Mut s, 'h');
size_t n = cstring_len(&_Const s);
cstring_free(s);

// 错误: 成员函数调用
s.push('h');
size_t n = s.length();
```

### 安全区分层

- `src/*.c`（业务层）：**禁止 `_Unsafe`**
- `src/platform/*.c`（适配层）：允许 `_Unsafe`，对外暴露 `_Safe` 接口
- `include/*.h`（头文件）：可包含 `_Unsafe` 的内联辅助函数（check-layers 只扫描 `src/*.c`）

### 内存管理规则

- 每个 `_Owned` 局部变量在离开作用域前必须显式消费（调用 `_free()` 或移动给调用者）
- 从函数返回含 `_Owned` 字段的结构体时，所有权转移给调用者（调用者负责 `_free`）
- 早期 return 路径必须释放所有已分配的资源

```c
_Safe Response serve_static(const Config* _Borrow config, const Request* _Borrow req) {
    cstring rel = cstring_new();
    // ... 使用 rel ...
    if (!path_is_safe(&_Const rel)) {
        cstring_free(rel);    // 早期 return 前必须释放
        return Response_not_found();
    }
    cstring_free(rel);        // 正常路径也显式释放
    return response;
}
```

## 快速参考

- 文件：`.c`（源文件）、`.h`（头文件）。编译：`clang file.c -o output`
- `.c`/`.h` 文件可以用 `-x bsc` 以 BSC 模式编译：`clang -x bsc file.c -o output`
- `#include "bishengc_safety.hbs"` 获取安全 API
- `safe_malloc<T>(val)` 返回 `T *_Owned`（无需强转），`safe_free((void *_Owned)p)` 释放
- Raw ↔ `_Owned` 直接 C 风格强转**即使在 `_Unsafe` 中也不允许**。使用内建函数：
  - Raw → `_Owned`：`__take_from_raw(raw_ptr)`（如果开了 nullability-check 需要先 null 检查）
  - `_Owned` → raw：`__move_to_raw(owned_ptr)`（转移所有权）或 `(T *)&_Mut *owned_ptr`（仅借用，保留所有权）
  - 数组版本：`__take_array_from_raw` / `__move_array_to_raw`
  - 数组分配：`safe_malloc_array<T>(size, initializer)` 返回 `T *_Owned _ArrayElem`
  - 数组释放：`safe_free_array<T>(p)`

## BSC 项目编译命令

```
编译器：/home/zly/bsc/llvm-project/build/bin/clang   （不是系统 clang）
Include：-I/home/zly/bsc/llvm-project/install/include/libcbs
链接：   -L/home/zly/bsc/llvm-project/install/lib -lstdcbs -lpthread
选项：   -Wall -Wextra -Wno-nullability-completeness -g
```

构建模型：**单翻译单元** — `src/main.c` `#include` 所有其他 `.c`
（业务 `src/*.c`，然后是适配器 `src/platform/*.c`）。
`-lstdcbs` 提供 `safe_free`、`bsc_bad_alloc_handler` 等运行时函数。

命令：
```
make                    # 构建 bin/httpd（先运行 check-layers）
make smoke              # 工具链冒烟测试
make test-<name>        # 构建+运行 tests/test_<name>.c（如 make test-router）
bash tests/valgrind_units.sh    # 对所有单测运行 valgrind（BSC 测试可能 PASS 但存在 UAF！）
bash tests/run_integration.sh   # 基于 curl 的 AC-2..AC-5 + 路径穿越测试
bash tests/check_no_unsafe.sh   # 强制检查：业务 src/*.c 不含 _Unsafe
# 对单个文件语法检查：<compiler> <flags> <includes> -fsyntax-only <file>
```

**分层规则（强制）：** 业务 `src/*.c` 必须不含 `_Unsafe`；所有 `_Unsafe` 活在适配器 `src/platform/*.c` 中（每个适配器是 `_Safe` 接口，内含 `_Unsafe` 块）。由 `make` 通过 `check-layers` 强制执行。

## 代码验证

写完或编辑任何 `.c`/`.h` 文件（或用 `-x bsc` 编译的 `.c`/`.h`）后，运行项目的验证命令。修复所有编译错误后再报告任务完成。

## LSP — 主动使用

BSC-aware clangd 暴露所有权流和生命周期。在对 `_Owned`/`_Borrow` 代码做重要修改前，对变量 `hover` 查看其移动、借用或释放的位置 — 不要仅凭局部上下文猜测。完整参考（可用性探测、已知 clangd 限制、回退到 Grep 的规则），加载 `/bsc-lsp`。

## 何时加载其他 Skill

- 重要 BSC 改动（新 struct、新 `_Owned`/`_Borrow` 签名、跨所有权边界的重构、`_Safe`/`_Unsafe` 边界决策）→ 规划前加载 `/bsc-design`。
- 将 C 翻译/移植到 BSC → 开始前加载 `/c-to-bsc`。仅改文件扩展名不等于翻译。

## 何时委派给 `bsc-planner` Agent

当规划重要 BSC 改动，需要阅读大量 `.c` 文件以理解所有权流时 — 重构、新 API、跨模块影响分析、任何会消耗主上下文的任务 — 使用 `bsc-planner` 子代理。该代理只读，返回计划；主线程实施。对于单文件编辑、拼写修正或已知函数的 borrow-checker 错误，不要委派 — 直接处理。

## 会话结束时调用 `bsc-learn`

在一个重要的 BSC 编码会话之后 — 任何修改或创建 `.c`/`.h` 文件、遇到 borrow-checker 或所有权错误、将 C 翻译到 BSC、或调试析构函数/移动语义的会话 — 通过 Agent 工具调用 `bsc-learn`。

传递：
1. 你遇到的主要问题的简要总结（检查器拒绝了什么、哪些地方需要反复调整、哪些令人意外）。
2. 本次会话覆盖的 git commit 范围（如 `HEAD~3..HEAD` 或具体 SHA）。

该代理读取 diff 和现有 skill 文件，然后以文本形式返回建议的 skill 改进。先向用户展示建议请求批准，再应用编辑。不要自主应用编辑。

以下情况跳过 `bsc-learn`：仅有注释编辑、单行拼写错误的修复、或无 `.c` 改动的会话。
