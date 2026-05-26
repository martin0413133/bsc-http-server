---
name: bsc-compile
description: "BiSheng C 编译。当你需要理解如何编译 .cbs 文件、编译器路径设置、包含路径、诊断抑制标志、语法检查或源码到源码的重写时，使用此技能。"
---

# BiSheng C 编译技能

## 1. 编译器路径设置

BSC 编译器是一个自定义的 clang 构建——它不是系统自带的 `clang`。在编译之前，你需要 BSC clang 二进制文件的路径。

**如何找到路径：** 检查项目的 `CLAUDE.md`（或 `.cursorrules` / `AGENTS.md`），查找如下行：

```
BSC compiler path: /path/to/bsc/bin/clang
```

如果没有配置路径，询问用户。然后在所有编译命令中使用完整路径：

```bash
/path/to/bsc/bin/clang file.cbs -o output
```

**推荐的项目设置：** 将编译器路径添加到 `CLAUDE.md`，以便 AI 始终知道在哪里找到它：

```markdown
## BSC Compiler
- Path: /home/user/bsc/build/bin/clang
- libcbs include: /home/user/bsc/libcbs/src
```

## 2. 基本用法

```bash
# 编译为可执行文件
/path/to/bsc/bin/clang file.cbs -o output

# 带优化
/path/to/bsc/bin/clang file.cbs -O2 -o output

# 仅语法检查（无二进制输出）
/path/to/bsc/bin/clang -fsyntax-only file.cbs

# 带警告
/path/to/bsc/bin/clang -Wall -Wextra file.cbs -o output

# 带调试信息
/path/to/bsc/bin/clang -g file.cbs -o output
/path/to/bsc/bin/clang -g -gdwarf-4 file.cbs -o output  # 更好的 gdb 兼容性

# 将 .c / .h 文件作为 BSC 编译（覆盖基于扩展名的检测）
/path/to/bsc/bin/clang -x bsc file.c -o output       # driver 形式，空格分隔
/path/to/bsc/bin/clang -cc1 -xbsc file.c             # cc1 形式，无空格
```

**`-x bsc` 使用场景**：逐步 C→BSC 迁移，无需重命名每个文件；为兼容外部工具保留原始 `.c`/`.h` 扩展名。只有 `.cbs`/`.hbs` 会被自动检测为 BSC——其他所有文件都需要该标志。大多数编辑器/LSP 工具依赖于 `.cbs`/`.hbs`，因此保留 `.c`/`.h` 意味着你失去了 BSC 感知的编辑器功能。

## 3. 包含路径

BSC 有两个可能需要 `-I` 标志的包含目录：

| 路径 | 包含内容 |
|------|----------|
| `libcbs/src/` | 标准库头文件（`vec.hbs`、`string.hbs` 等） |
| `clang/lib/Headers/bsc_include/` | 内置头文件（`bsc_type_traits.hbs`、`future.hbs` 等） |

```bash
/path/to/bsc/bin/clang -I/path/to/libcbs/src -I/path/to/bsc_include file.cbs -o output
```

### 链接 libcbs（`String`、`Vec` 等并非仅头文件）

`String`、`Vec`、`LinkedList`、`Option`、`Result` 以及所有其他 libcbs 类型在 `libstdcbs.a` 中有已编译的实现。在不链接库的情况下包含它们的 `.hbs` 头文件会在链接时产生"undefined reference to `struct_String_new`"（及类似）错误。使用任何 libcbs 类型时，始终添加 `-L<install>/lib -lstdcbs`：

```bash
/path/to/bsc/bin/clang file.cbs \
    -I/path/to/install/include/libcbs \
    -L/path/to/install/lib -lstdcbs \
    -o output
```

如果程序还使用 pthreads（例如线程池），还需要添加 `-lpthread`。`bishengc_safety.hbs` 原语（`safe_malloc`、`safe_free`、`safe_swap`）也在 `libstdcbs` 中实现——它们不是宏。

## 4. 特殊模式

```bash
# 源码到源码重写（BSC → C）
/path/to/bsc/bin/clang -rewrite-bsc file.cbs
# 生成 file.c，BSC 特性降级为普通 C

# 带包含路径的重写（对于标准库类型是必需的）
/path/to/bsc/bin/clang -rewrite-bsc file.cbs -I/path/to/libcbs/src

# 带显式输出的重写
/path/to/bsc/bin/clang -rewrite-bsc file.cbs -o output.c

# 重写多个文件
/path/to/bsc/bin/clang -rewrite-bsc foo.cbs bar.cbs

# 带行号映射的重写（用于调试）
/path/to/bsc/bin/clang -rewrite-bsc -line file.cbs -o file.c

# AST 转储
/path/to/bsc/bin/clang -Xclang -ast-dump -fsyntax-only file.cbs
```

## 5. 阅读 BSC 编译器诊断信息

### 始终同时阅读 `note:` 行和 `error:` 行

BSC 编译器在借用检查器错误旁边发出 `note:` 源代码位置，告诉你**冲突的先前借用从哪里开始**：

```
file.cbs:22:29: error: cannot borrow `b` as mutable more than once at a time
file.cbs:21:29: note: first mut borrow occurs here
```

不阅读 `note:`，错误看起来难以理解。阅读后，修复方法变得明显——将在第 21 行的借用作用域限制到在第 22 行之前结束。

**已知差距**（截至当前编译器）：
- `error: use of moved value: X` 没有附带的 `note:` 指向移动位置。使用 LSP 对 `X` 使用 `hover` 查看移动位置。
- 没有 `help:` 建议修复模式——你需要知道惯用法（参见 `/bsc-common-mistakes` §3.3 了解"局部借用寿命不足"的解决方案）。

### LSP 是活动范围信息的规范来源

在任何 `_Owned` 或 `_Borrow` 变量上使用 `hover` 返回完整的所有权时间线以及显式的活动范围：

```
Ownership Flow:
line 21: Declared
line 21: Mut borrow of b
Live range: lines 21-21
```

对于拥有的值：`hover` 显示 `Moved into foo()` / `Freed` / `Dropped` 事件及精确的行号。

当编译器诊断信息过于简洁时，**始终在错误命名的变量上检查 LSP hover**——它包含比诊断打印更多的细节。有关通过 LSP + AST 转储进行析构函数 bug 调试，请参见下面的 §6。

## 6. 检查析构函数插入（调试内存错误）

当你遇到运行时 `double free` 或 `use after free` 并怀疑编译器的析构函数插入错误时，真相来源是**去语法糖后的 AST**（driver 模式）。

### 关键：使用 driver，而不是直接使用 `-cc1`

```bash
# 错误——缺少系统包含，使每个 _Owned struct 虚假地变成"无效"，
# 产生误导性的 RecoveryExpr / <dependent type> AST 节点
clang -cc1 -fsyntax-only -ast-dump file.cbs -I./include

# 正确——driver 获取系统包含（stdlib.h、string.h 等）
clang -Xclang -ast-dump -fsyntax-only file.cbs -I./include
```

在没有系统包含的 `-cc1` 模式下，找不到 `stdlib.h` → `bishengc_safety.hbs` 解析失败 → 每个使用 `String`/`Vec` 的 `_Owned struct` 被标记为 `referenced invalid struct ... definition` → 通过它们进行的每个成员访问变成 `RecoveryExpr` / `CXXDependentScopeMemberExpr`。这个 AST 看起来坏了，但并不反映实际代码生成产生的结果。**始终使用 driver 模式（`-Xclang -ast-dump`）进行析构函数相关的调试。**

### 正确的析构函数插入应该是什么样

对于每个 `_Owned struct` 局部变量或参数，去语法糖后的 AST 应显示：

```
# 在函数/参数入口处：
DeclStmt
  VarDecl 'varname_is_moved' 'bool' cinit
    IntegerLiteral 'int' 0

# 在使用该变量的 CallExpr 或 BinaryOperator 之后：
BinaryOperator 'bool' '='
  DeclRefExpr 'varname_is_moved' 'bool'
  IntegerLiteral 'int' 1

# 在作用域退出处（或 ReturnStmt）：
IfStmt
  UnaryOperator '!' → DeclRefExpr 'varname_is_moved'
  CallExpr → BSCMethod '~TypeName' → DeclRefExpr 'varname'
```

如果你怀疑的变量这三个部分都存在且格式良好，**编译器的插入是正确的**，错误在其他地方（通常是结构体布局中堆指针字段之间的别名问题）。

### 泛型模板的转储方式不同

`Vec<T>::insert_at` 在其**模板形式**中，通过 `T` 进行的每个成员访问都显示 `<dependent type>`——这是正常的。析构函数去语法糖是**每次实例化**进行的，因此请检查 `Vec<ConcreteType>::insert_at`（在 AST 转储中搜索 mangled/实例化形式）以查看真实的代码生成。

不要让模板形式的 `<dependent type>` 标记吓到你，导致错误的"编译器 bug"诊断。

### 调试析构函数相关的 double-free 时的排查流程

1. **首先使用 valgrind 重现**——`valgrind --leak-check=no ./binary`。"Invalid read"堆栈跟踪同时显示失败的释放和之前的释放位置，以及 `malloc` 来源。这比在 abort 上使用 gdb 能更好地定位错误。
2. **检查测试顺序污染**——失败的函数在仅包含该代码的新二进制文件中能工作吗？如果可以，之前的测试正在留下全局状态（解析器全局变量、分配计数器），在后续触发该错误。
3. **转储 driver 模式 AST** 并验证可疑变量的 `_is_moved` 标志机制。
4. **仅在 1-3 之后**考虑编译器级别的 bug 假设。已确认的编译器 bug 确实存在（参见 common-mistakes §8.6 关于 `if` 条件移动追踪 bug），但库级别的别名是更常见的原因。

### AST 转储上有用的 grep 模式

```bash
# 保存完整的 AST 用于 grepping（driver 模式！）
clang -Xclang -ast-dump -fsyntax-only file.cbs -I./include 2>/dev/null > ast.txt

# 查找 `varname` 的 move-flag VarDecl + 每次赋值 + 析构函数 IfStmt
grep -B1 -A2 "varname_is_moved" ast.txt

# 查找非模板代码上的所有 RecoveryExpr / dependent-type 标记（真正的错误指示器）
grep -B2 "RecoveryExpr\|<dependent type> contains-errors" ast.txt | grep -v "<T>"

# 仅显示特定函数的主体
sed -n '/FunctionDecl.*funcname/,/^|-/p' ast.txt

# 查找缺失的移动追踪（声明但没有赋值给 1）
# 如果你看到 VarDecl '...is_moved' 但没有设置它的 BinaryOperator '='，
# 编译器错过了一个移动事件——很可能就是 if-condition bug。
```

### 从 AST 中发现 if-condition 编译器 bug

如果 AST 显示变量 `b`：
- 一个 `VarDecl 'b_is_moved'`（声明，初始化为 0）
- 一个检查 `!b_is_moved` 的析构函数 `IfStmt`
- 但两者之间**没有** `BinaryOperator '=' b_is_moved = 1`

……并且源代码中有一个消耗 `b` 的函数调用——检查该调用是否出现在 `if`/`while`/`for`/`switch` 条件内部。如果是，你已经遇到了文档中记录的编译器 bug。参见 common-mistakes §8.6 了解解决方法。

## 7. 诊断抑制

BSC 安全检查默认是错误。要抑制特定的诊断，在命令行中使用 `-Eno-<identifier>` 或在源代码中使用 `#pragma`。

### 命令行抑制：`-Eno-<identifier>`

```bash
# 抑制重复借用错误
/path/to/bsc/bin/clang -Eno-repeated-borrow file.cbs

# 抑制所有借用检查
/path/to/bsc/bin/clang -Eno-bsc-borrow file.cbs

# 抑制所有 BSC 安全检查
/path/to/bsc/bin/clang -Eno-bsc-safety-check file.cbs
```

### 源代码抑制：`#pragma`

```c
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Eassign-borrowed"
*p1 = 2;  // 此错误现在已被抑制
#pragma GCC diagnostic pop
```

### 诊断标识符层级

```
bsc-safety-check                    # 所有 BSC 安全检查
├── bsc-nullability                 # 所有可空性检查
│   ├── deref-nullable              #   解引用可空指针
│   ├── pass-nullable               #   传递可空参数
│   ├── return-nullable             #   返回可空指针
│   ├── cast-nullable               #   将可空转换为非空
│   ├── assign-nullable             #   通过可空指针访问成员
│   └── assign-nonnull              #   将可空赋值给非空
├── bsc-ownership                   # 所有所有权检查
│   ├── use-owned                   #   移动后/未初始化使用组
│   │   ├── use-moved-owned         #     移动后使用
│   │   └── use-uninit-owned        #     使用未初始化
│   ├── assign-owned                #   赋值给拥有的变量组
│   │   ├── assign-moved-owned      #     赋值给已移动的值
│   │   └── assign-uninit-owned     #     赋值给部分未初始化的值
│   ├── cast-owned                  #   转换为 void *_Owned 时无效
│   │   └── cast-moved-owned        #     转换已移动的值
│   ├── check-memory-leak           #   内存泄漏检测
│   ├── init-nonnull                #   _Nonnull 指针未初始化
│   ├── destruct-owned-struct       #   析构函数不正确
│   └── partially-moved-struct      #   作用域结束时部分移动
└── bsc-borrow                      # 所有借用检查
    ├── assign-borrowed             #   赋值给借用的值
    ├── move-borrowed               #   移动借用的值
    ├── use-mutably-borrowed        #   可变借用期间使用
    ├── repeated-borrow             #   多次可变借用
    ├── return-local-borrow         #   返回对局部变量的引用
    └── short-life-borrow           #   生命周期太短
```

> 关于错误消息和代码，请参见 `bsc-errors` 技能
