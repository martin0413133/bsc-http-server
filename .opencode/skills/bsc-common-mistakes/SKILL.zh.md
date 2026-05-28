---
name: bsc-common-mistakes
description: "BiSheng C 常见错误和修复。当你遇到 BSC 编译错误、需要调试代码或想避免所有权、借用或安全区域的常见陷阱时，使用此技能。"
---

# BiSheng C 常见错误技能

## 1. 安全区域错误

### 1.1 使用 `&` 而不是 `&_Const`/`&_Mut`
```c
_Safe void f(void) {
    int x = 42;
    // int* p = &x;                    // 错误：安全区域中禁止 '&'
    const int *_Borrow p = &_Const x;  // 正确
}
```

### 1.2 将 `++`/`--` 结果用作表达式
`++`/`--` 允许作为语句，但返回 `void`——不能使用其结果。
```c
_Safe void f(void) {
    int i = 0;
    i++;                               // 可以：仅副作用
    for (int j = 0; j < 10; j++) {}    // 可以：迭代子句
    // int x = i++;                    // 错误：结果类型是 void
}
```

### 1.3 空参数没有 `void`
```c
// _Safe int f() { return 42; }     // 错误
_Safe int f(void) { return 42; }    // 正确
```

### 1.4 带指针字段的结构体部分初始化
只有带指针字段的结构体需要完整的初始化器。仅含基本类型的结构体可以部分初始化。
```c
_Safe {
    struct HasPtr hp = {nullptr, 0};  // 可以：完整初始化（有指针字段）
    // struct HasPtr hp2 = {0};       // 错误：部分初始化
    struct NoPtr { int a; int b; };
    struct NoPtr np = {0};            // 可以：没有指针字段
}
```

### 1.5 安全区域中的联合体成员访问
联合体可以被声明/初始化/传递，但禁止成员访问。
```c
_Safe {
    union U { int i; float f; } u = {.i = 1};
    // int x = u.i;                   // 错误
    _Unsafe { int x = u.i; }          // 可以：不安全逃逸
}
```

### 1.6 安全区域中禁止的转换
不允许跨类别指针转换（`_Owned`/`_Borrow`/原始）、指针-整数转换、浮点-整数转换。例外：允许 `T *_Owned` 到 `void *_Owned`。

### 1.7 安全区域中对全局变量的可变借用
只允许对全局变量使用 `&_Const`；禁止对全局变量使用 `&_Mut`。
```c
int g = 10;
_Safe void f(void) {
    // int *_Borrow p = &_Mut g;          // 错误
    const int *_Borrow p = &_Const g;     // 可以
}
```

## 2. 所有权错误

### 2.1 `_Owned`/`_Borrow` 限定词位置错误
**#1 LLM 错误。** `_Owned`/`_Borrow` 放在 `*` **之后**，而不是类型之前。
```c
// 错误                              // 正确
// _Owned char* msg                   char *_Owned msg = safe_malloc('\0');
// _Borrow int* r                     const int *_Borrow r = &_Const x;
```
模式：始终是 `T *_Owned` 和 `T *_Borrow`，从不是 `_Owned T*` 或 `_Borrow T*`。

### 2.2 所有权转移后使用变量
```c
int *_Owned p = safe_malloc(42);
int *_Owned q = p;        // p 已移动
// printf("%d\n", *p);    // 错误：使用已移动的值
```

### 2.3 忘记在作用域结束前释放 `_Owned`
```c
void f(void) {
    int *_Owned p = safe_malloc(42);
    safe_free((void *_Owned)p);   // 必须释放：释放、传递或返回
}
```

### 2.4 对 `_Owned` 进行指针算术
```c
int *_Owned p = safe_malloc(42);
// p++;                    // 错误：_Owned 不允许算术
// p[3] = 0;               // 错误：普通 _Owned 不允许 []
```

对于想要进行下标操作的堆**数组**，使用 `T *_Owned _ArrayElem`（使用 `safe_malloc_array` 分配，使用 `safe_free_array` 释放）。它支持 `p[i]`，但仍禁止算术——`_Owned _ArrayElem` **不是**到 C 风格数组指针的自由升级。参见 `bsc-ownership` §8。

```c
int *_Owned _ArrayElem arr = safe_malloc_array(10, 0);
arr[3] = 3;                            // 可以：_ArrayElem 允许下标
// arr += 1;                           // 错误：仍然不允许算术
safe_free_array((void *_Owned _ArrayElem)arr);
```

## 3. 借用错误

### 3.1 不可变借用活跃时进行可变借用
```c
int x = 42;
{
    const int *_Borrow r = &_Const x;
    printf("%d\n", *r);
}
int *_Borrow mr = &_Mut x;  // 可以：不可变借用已结束
```

### 3.2 借用超过来源生命周期
```c
// const int *_Borrow dangling(void) {
//     int x = 42;
//     return &_Const x;  // 错误：借用超过局部变量
// }
```

### 3.3 返回依赖于局部辅助值的借用

最常见的所有权模型冲突之一。你计算一个局部 `String`（或其他拥有的值）用作键，然后想要返回通过它查找得到的借用。

```c
// 错误——`key` 在作用域退出时销毁，但返回的借用间接依赖它
_Safe JSON_Value* _Borrow lookup(Obj* _Borrow this, const char* k) {
    String key = String::from(k);
    JSON_Value* _Borrow child = this->get_value(&_Const key);
    return child;   // 错误：`key` 生命周期不够长
}
```

**三个合法的修复方法，按偏好顺序：**

**修复 A——将键作为来自调用者的借用**（最佳）。调用者拥有 `String`，其生命周期覆盖整个调用：
```c
_Safe JSON_Value* _Borrow lookup(Obj* _Borrow this, const String* _Borrow key) {
    return this->get_value(key);
}
```

**修复 B——递归**，当辅助值在遍历的每一步产生时使用。将路径的剩余部分传递到递归调用中；辅助值的生命周期自包含在一个递归层级内：
```c
_Safe static T* _Borrow navigate(Parent* _Borrow p, const Path* _Borrow path, size_t pos) {
    String segment = path->slice(pos, ...);   // 局部变量；在返回时销毁
    T* _Borrow child = p->get(&_Const segment);
    // 尾调用递归携带 child 的生命周期，而非 segment 的
    return navigate_further(child, path, next_pos);
}
```

**修复 C——最小的 `_Unsafe` 块**，当查找的生命周期确实无法被检查器建模时（例如，哈希表命中保证返回指向现有数据的指针）。通过原始指针转换：
```c
_Safe JSON_Value* _Borrow lookup(Obj* _Borrow this, const char* k) {
    String key = String::from(k);
    _Unsafe {
        JSON_Value* _Borrow child = (JSON_Value* _Borrow)this->get_value(&_Const key);
        // `key` 在块结束时销毁，但返回的借用指向 `this`，
        // 而不是 `key`——检查器看不到这一点。
        return child;
    }
}
```

仅当 A 和 B 在结构上不可能时使用修复 C。记录**原因**——下一个读者会问。这是 parson 中 `dotget_value` 使用的模式。

### 3.4 如何阅读 BSC 借用检查器诊断

BSC 编译器的借用诊断信息比初看起来更具信息量。**始终阅读 `note:` 行，而不仅仅是 `error:` 行**——`note:` 是提示所在之处。

**借用冲突（别名）：**
```
file.cbs:22:29: error: cannot borrow `b` as mutable more than once at a time
file.cbs:21:29: note: first mut borrow occurs here
```
`note:` 精确地告诉你哪个更早的行开始了冲突的借用。修复：将第一个借用范围限制得更紧（将其包装在第二个借用之前结束的块中）或重新组织，使一次只有一个活跃。

**跨冲突修改的借用：**
```
error: cannot use `X` because it was mutably borrowed
error: cannot borrow `*X` as mutable more than once at a time
```
相同的思路——`note:` 指向第一个借用。典型修复：限制第一个借用的范围，或以不同的顺序执行操作。

**移动后使用（目前没有 `note:`——已知差距）：**
```
file.cbs:12:13: error: use of moved value: `b`
```
编译器目前没有为 `b` 被移动的位置打印 `note:`。使用 LSP 对 `b` 的声明执行 `hover` 查看其完整的所有权流（它显示 `Moved into foo()` 及精确行号）。参见 `/bsc-compile` §5 了解 LSP 设置。

**生命周期/借用返回：**
```
error: no _Borrow qualified type found in the function parameters,
       the return type is not allowed to be _Borrow qualified
```
这直接解释了规则——函数返回 `_Borrow` 但没有 `_Borrow` 参数来绑定返回值的生命周期。修复：添加一个生命周期与返回的借用匹配的 `_Borrow` 参数，或改为返回一个拥有的值。

**让人困惑的非明显消息：**
- `does not live long enough` ——编译器对生命周期比上下文要求更短的局部变量的称呼（通常是返回指向局部变量的 `_Borrow`）。参见 §3.3 了解三种修复方法。
- `cannot cast between _Owned and raw pointer` ——你需要使用 `__take_from_raw` / `__move_to_raw` 进行所有权转移，或使用 `(T *)&_Mut *p` / `(T *)&_Const *p` 进行非转移性转换。

**当诊断仍然不够时，使用 LSP hover：**

对报错的变量执行 `hover`——BSC 的 LSP（配置后；参见 `/bsc-compile` §5）报告其**完整的所有权时间线，包括活动范围**：

```
Ownership Flow:
line 21: Declared
line 21: Mut borrow of b
Live range: lines 21-21
```

对于拥有的值，`hover` 还显示 `Moved into foo()` 及行号。这是移动后使用诊断应打印但没有打印的信息——LSP 填补了空白。

## 4. 可空性错误

### 4.1 可能为空的 `_Owned` 指针没有 `_Nullable`
```c
// int *_Owned p = nullptr;               // 错误：_Owned 默认为 Nonnull
int *_Owned _Nullable p = nullptr;        // 正确
```

### 4.2 未进行空检查就解引用可空指针
```c
_Safe void f(int *_Borrow _Nullable p) {
    // *p = 10;                           // 错误：可空指针
    if (p != nullptr) { *p = 10; }       // 正确：先空检查
}
```

### 4.3 将可空指针传递给非空参数
```c
_Safe void bar(int *_Borrow p) {}     // 非空参数
_Safe void f(int *_Borrow _Nullable p) {
    // bar(p);                          // 错误：可空到非空
    if (p != nullptr) { bar(p); }      // 正确：已检查
}
```

## 5. 初始化错误

### 5.1 使用未初始化的变量
```c
_Safe void f(void) {
    int x;
    // int y = x;                       // 错误：未初始化
    x = 42;
    int y = x;                          // 正确
}
```

### 5.2 数组元素逐个赋值不被计数
```c
_Safe void f(void) {
    int arr[3];
    arr[0] = 1; arr[1] = 2; arr[2] = 3;
    // int x = arr[0];                  // 错误：arr 不被视为已初始化
    // 修复：使用初始化列表或 __assume_initialized
    int arr2[3] = {1, 2, 3};
    int x = arr2[0];                    // 正确
}
```

### 5.3 对未初始化变量取地址
```c
_Safe void f(void) {
    int x;
    // int *_Borrow p = &_Mut x;       // 错误：x 未初始化
    int x2 = 0;
    int *_Borrow p = &_Mut x2;         // 正确
}
```

## 6. 异步错误

### 6.1 二元表达式中的 `_Await`
```c
// int result = _Await compute(1) + _Await compute(2);  // 错误
int a = _Await compute(1);
int b = _Await compute(2);
int result = a + b;
```

### 6.2 同一参数列表中的多个 `_Await`
```c
// f(_Await g(), _Await h());          // 错误：同一级别的多个 _Await
int a = _Await g();
f(a, _Await h());                      // 正确：预先计算一个
```

## 7. 调试运行时内存错误

当 BSC 代码编译干净但运行时出现 **double-free** 或 **use-after-free** 时，在怀疑编译器之前按以下排查流程进行。

### 7.1 使用 valgrind 而非 gdb 来定位 double-free

glibc 的 `free(): double free detected in tcache 2` 在 gdb 下只给你第二次 free 的堆栈跟踪。Valgrind 同时显示两次 FREE 和原始的 `malloc`，这才是你找到别名的根本原因所需要的。

```bash
valgrind --error-exitcode=1 --leak-check=no ./binary
```

查找"Invalid read"/"Invalid free"报告。"Address X is N bytes inside a block of size M free'd"这行告诉你同一地址之前在哪里被释放，以及该次释放时的堆栈。

### 7.2 在怀疑编译器之前排除测试顺序污染

如果某个函数在隔离环境（独立的可重现二进制）中正常工作，但在大型测试套件中在其他测试之后调用时失败，则错误是**状态污染**，而不是代码生成：

- **可变全局变量**（例如，解析器游标、分配计数器）被之前的测试留在了非零状态
- **库中的静态缓存**在调用之间没有重置
- **堆布局敏感性**——测试 N 中的泄漏仅在测试 N+M 中分配大小恰好别名时才表现为 double-free

重现方法：构建一个最小 `main`，在一个新进程中**仅调用失败的函数**。如果通过了，则错误是上下文相关的。

### 7.3 在声称编译器 bug 之前检查去语法糖后的 AST（driver 模式）

大多数关于 double-free 的"编译器 bug"假设最终都是错误的。使用以下命令验证：

```bash
clang -Xclang -ast-dump -fsyntax-only file.cbs -I./include
```

查找你怀疑的变量的 `varname_is_moved` 标志和 `if (!varname_is_moved) ~Type(varname)` IfStmt。如果编译器的机制完好，则错误在你的库的堆指针别名中。

**不要为此使用 `clang -cc1 -fsyntax-only`**——它缺少系统包含，这会使每个带 `_Owned` 字段的结构体在 AST 中虚假地变成"无效"。有关详细信息，请参见 `/bsc-compile` 技能 §6。

### 7.4 常见的库级 double-free 原因

当编译器在做正确的事情时，真正的原因通常是以下之一：

- **标签结构体作为联合体的替代**：每个实例为每个变体携带一个堆指针，因此如果构造无意中共享了指针，两个实例可能别名。参见 `/bsc-design` 规则 7 和 §2 "替换 C 联合体"。
- **别名指针之间的 `safe_swap`**：使双方都指向重叠所有权的交换。
- **返回借用，而底层拥有的值被调用者移动**：借用变成悬垂。
- **手动的 `_Unsafe` 字节赋值覆盖了 `_Owned` 槽，而没有释放先前的内容**：旧的堆指针泄漏（单个副本）或别名（如果它们仅被 `memmove` 移位）。

### 7.5 排查流程总结

1. 在 valgrind 下重现 → 获取两个释放位置 + malloc 来源。
2. 在隔离环境中测试 → 确认/否认测试顺序污染。
3. Driver 模式 AST 转储 → 确认编译器移动追踪正确。
4. 然后才考虑编译器级别的错误。实际上，>90% 的 BSC 项目中的 double-free 追溯到库级别名，而非代码生成。

## 8. 快速参考

| 错误 | 修复 |
|---------|-----|
| `_Owned char*` | `char *_Owned` — 限定词在 `*` 之后 |
| `_Borrow int*` | `int *_Borrow` — 限定词在 `*` 之后 |
| `_Safe` 中的 `&` | 使用 `&_Const` 或 `&_Mut` |
| `_Safe` 中的 `int x = i++` | `i++` 作为语句可以；结果是 `void` |
| `_Safe int f()` | `_Safe int f(void)` |
| 安全区域中的部分初始化 | 仅对**有指针字段的**结构体是必需的 |
| 安全区域中的联合体 `.member` | 禁止——使用 `_Unsafe {}` 逃逸 |
| 安全区域中的类别转换 | 不允许跨类别转换（`T *_Owned` 到 `void *_Owned` 除外） |
| 安全区域中的 `&_Mut` 全局变量 | 禁止——仅允许对全局变量使用 `&_Const` |
| 移动后使用 | 在转移所有权之前使用 |
| 拥有的变量未释放 | 在作用域结束前 `safe_free`、传递或返回 |
| 表达式中的 `_Await` | 先赋值给变量 |
| 参数中的多个 `_Await` | 预先计算一个：`int a = _Await g(); f(a, _Await h());` |
| `_Owned _Borrow` | 非法——选择一个 |
| 借用的借用 | `T *_Borrow *_Borrow` ——限制（选择单层） |
| 没有 `_Nullable` 的 `_Owned` | 如果指针可能为空，添加 `_Nullable` |
| 解引用可空指针 | 先空检查：`if (p != nullptr) { *p = ... }` |
| 数组按元素初始化 | 使用初始化列表 `{1,2,3}` 或 `__assume_initialized` |
| 未初始化的局部变量 | 在使用前初始化；字段级追踪适用 |
> 关于详细的错误码，请参见 `bsc-errors` 技能
> 关于安全区域规则，请参见 `bsc-safe-zone` 技能
> 关于所有权规则，请参见 `bsc-ownership` 技能
> 关于可空性规则，请参见 `bsc-nullability` 技能
> 关于初始化分析，请参见 `bsc-initialization` 技能
