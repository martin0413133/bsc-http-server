---
name: bsc-borrowing
description: "BiSheng C 借用。当你需要理解 _Borrow 指针、&_Const（不可变借用）、&_Mut（可变借用）、借用生命周期规则、冻结语义、NLL（非词法生命周期）、借用类型转换、解引用/成员访问语义或借用函数签名时，使用此技能。"
---

# BiSheng C 借用技能

## 优先选择 `_Borrow`，而非 `_Owned`，作为指针参数的默认选择

一个**读取或修改**指针指向的值但**不获取所有权**的函数必须使用 `_Borrow`，绝不要使用 `_Owned`。

- 函数会释放指针、将其转移出去或长期存储 → `T *_Owned`。
- 函数只通过指针读取 → `const T *_Borrow`。
- 函数通过指针对值进行修改但不释放 → `T *_Borrow`。
- 函数进行指针算术/迭代 → 原始 `T *`。

`_Owned` 参数会消耗调用者的变量。大多数 C 函数不这样做。默认在所有地方使用 `_Owned` 会导致每个调用点都是一次性移动，并迫使调用点出现丑陋的重新分配模式——这是错误的。

关于翻译上下文，请参见 `/c-to-bsc` 第 2.5 步。

## 关键：`_Borrow` 语法

**`_Borrow` 放在 `*` 之后，而不是类型之前。** 它是一个指针限定词。

```c
// 正确
const int *_Borrow r = &_Const x;
int *_Borrow mr = &_Mut x;

// 错误（无法编译）
_Borrow int* r = &_Const x;
```

## 1. 概述

临时的、非拥有性的引用。编译器在编译时强制执行借用规则——没有悬垂引用，没有别名修改。

## 2. 创建借用

```c
int x = 42;
const int *_Borrow cr = &_Const x;   // 不可变借用——只读
int *_Borrow mr = &_Mut x;           // 可变借用——读/写
```

从 `_Owned` 指针创建：`&_Const *owned_ptr` 或 `&_Mut *owned_ptr`。

## 3. 语义

| 类型 | 访问权限 | 别名规则 |
|------|--------|----------|
| `const T *_Borrow` | 只读 | 允许多个同时存在 |
| `T *_Borrow` | 读 + 写 | 同一时间只能有一个 |

可变借用可以隐式转换为不可变借用：`T *_Borrow` -> `const T *_Borrow`（编译器插入 `&_Const *`）。这适用于声明、赋值、函数参数和返回值。

## 4. 非词法生命周期（NLL）

借用生命周期使用 **NLL**：借用从其创建（或重新赋值）到其**最后一次使用**为止是活跃的，而不是到词法作用域的末尾。NLL 可以被分段——如果借用变量被重新赋值，它会创建不连续的活跃范围。

```c
void use(int *_Borrow p) {}

void foo() {
    int local1 = 1, local2 = 2;
    int *_Borrow p = &_Mut local1;  // NLL 段 1 开始
    use(p);                          // NLL 段 1 结束（最后一次使用）
    // local1 在此处解冻——p 的借用已结束
    local1 = 10;                     // 可以：p 不再活跃
    p = &_Mut local2;                // NLL 段 2 开始（并结束——没有进一步使用）
}
```

延长 NLL 的使用：函数调用 `use(p)`、`return p`、解引用 `*p`、成员访问 `p->field`。

## 5. 规则

- **`_Borrow` 放在 `*` 之后**：写 `int *_Borrow`，而不是 `_Borrow int*`
- **不可变借用**活跃期间：原始变量只读，不允许可变借用
- **可变借用**活跃期间：原始变量被**冻结**——不允许读取、写入、移动或借用
- **结构体字段借用**：`&_Mut e.field` 只冻结**该字段**（阻止整个结构体的修改）；其他字段仍可访问。`&_Const e.field` 将该字段置于只读状态
- 借用生命周期 <= 被借用值的生命周期（编译器强制执行）
- 不能创建借用的借用：`int *_Borrow *_Borrow` 是非法的
- 借用变量必须在声明时初始化
- 不能是全局变量或联合体成员
- `_Owned` 和 `_Borrow` 不能共存于同一指针：`int *_Owned _Borrow` 是非法的
- 不允许对**普通**借用指针进行索引（`p[i]`）和指针算术（`p + n`、`p++`）。对于数组元素借用，使用 `_Borrow _ArrayElem`（参见 §11）——该变体支持 `[]`、`+`、`-`、`+=`、`-=`、`++`、`--`
- 不能对包含借用成员的结构体取借用
- 全局变量：在安全区域中，只允许不可变借用（`&_Const`）；不允许对全局变量进行可变借用
- 字符串字面量：禁止 `&_Mut "hello"`；禁止 `&_Mut * "hello"`

### 重新赋值规则
- 借用变量的重新赋值需要相同的类型，且新来源的生命周期必须 >= 借用变量的剩余生命周期

## 6. 解引用和成员访问

### 解引用（`*p`）
| 操作 | 不可变借用 | 可变借用（T 是 Copy） | 可变借用（T 是 Move） |
|-----------|-----------------|---------------------------|---------------------------|
| 读取 `*p` | 可以 | 可以 | 可以 |
| 赋值给 `*p` | 错误 | 可以 | 错误（不能移动赋值） |

### 成员访问（`p->field`）
- 不可变借用：可以读取字段，不能修改
- 可变借用：可以读取和修改字段（对于 Copy 类型）
- 不可变借用不能调用可变方法（`T *_Borrow this` 方法）

## 7. 类型转换

- **`T *_Borrow` → `void *_Borrow`**：当 `T` 是平凡数据类型（没有指针字段）时**隐式**允许；否则转换被**禁止**，即使使用显式转换也不行
  ```c
  struct S { int *ptr; };
  int a = 0; struct S s = {.ptr = nullptr};
  _Safe {
      int *_Borrow p1 = &_Mut a;
      void *_Borrow p2 = p1;                       // 可以：int 是平凡类型
      struct S *_Borrow p3 = &_Mut s;
      void *_Borrow p4 = (void *_Borrow)p3;        // 错误：S 有指针字段
  }
  ```
- **`void *_Borrow` → `T *_Borrow`**：**禁止**隐式转换；显式转换必须在 `_Unsafe` 中进行
- **`_Borrow` 和原始指针之间**：在 `_Safe` 区域中禁止；在 `_Safe` 之外，转换通过原始 `T *` 中间进行，绝不直接转换
- **`_Owned` 和 `_Borrow` 之间**：**禁止**任一方向的 C 风格转换
- **可变和不可变借用之间**：禁止显式转换。可变到不可变是隐式的（编译器插入 `&_Const *`）；反向绝不允许
- **`T *_Borrow _ArrayElem` → `T *_Borrow`**：允许作为隐式转换（相当于"在当前元素处重新借用，放弃数组迭代语义"）。反向 `T *_Borrow` → `T *_Borrow _ArrayElem` 被禁止
- **隐式 `_Bool` 转换**：`_Borrow _Nullable` 指针可用于条件表达式

## 8. 附加规则

- 借用指针可用于 `if`/`while`/`do-while`/`for`/三目运算符条件，但**不能**用于 `switch`
- **普通**借用指针上禁止的操作符：`-`、`~`、`[]`、`++`、`--`、`*`（算术）、`/`、`%`、`&`（按位）、`|`、`<<`、`>>`、二元 `+`、二元 `-`。`_Borrow _ArrayElem` 重新启用 `[]`、二元 `+`/`-`、`+=`/`-=`、`++`/`--`（参见 §11）。其余操作仍被禁止
- 同类型借用之间允许比较（`==`、`!=`、`<`、`<=`、`>`、`>=`）。比较时忽略被指向类型上的顶层 `const`/`volatile`/`restrict`——`int *_Borrow` 和 `const int *_Borrow` 可以比较
- `sizeof(T *_Borrow) == sizeof(T *)`，`_Alignof` 同理。`_Borrow _ArrayElem` 具有与 `T *` 相同的大小/对齐
- 字符串字面量在传递给 `const char *_Borrow` 参数时自动借用为 `const char *_Borrow`（编译器插入 `&_Const *`）

## 9. 函数签名

```c
// 只读访问
_Safe int len(const Vec<int> *_Borrow this) {
    return this->len;
}

// 可变访问
_Safe void push(Vec<int> *_Borrow this, int value) { ... }

// 返回借用——生命周期与输入借用绑定
_Safe int *_Borrow first(Vec<int> *_Borrow this) { ... }
```

- 如果返回值是 `_Borrow` 且有一个参数是 `_Borrow`：返回生命周期 = 参数生命周期
- 如果有多个 `_Borrow` 参数：返回生命周期 = 所有参数生命周期的并集
- 如果没有 `_Borrow` 参数但返回值是 `_Borrow`：**编译错误**——借用返回需要至少一个借用参数

## 10. 设计模式

当借用检查器拒绝"显而易见"的 C 风格代码时，这些惯用法会反复出现。

### 10.1 迭代遍历 → 转换为递归

**问题：** 你想通过 while 循环遍历链表结构，在每一步重新赋值游标。借用检查器禁止这样做，因为第 N+1 步的借用源自第 N 步的借用，重新赋值会使链条失效。

```c
// 错误——当借用通过 current 活跃时不能重新赋值
_Safe T* _Borrow walk(Root* _Borrow root, const Path* _Borrow p) {
    T* _Borrow current = root;
    for (size_t i = 0; i < p->length(); i++) {
        current = current->child(i);   // 重新赋值与派生借用冲突
    }
    return current;
}
```

**修复：** 转换为尾递归。每个递归调用都有自己的新栈帧，因此借用链保持线性。

```c
_Safe static T* _Borrow walk_rec(T* _Borrow current, const Path* _Borrow p, size_t i) {
    if (i >= p->length()) { return current; }
    return walk_rec(current->child(i), p, i + 1);
}
_Safe T* _Borrow walk(Root* _Borrow root, const Path* _Borrow p) {
    return walk_rec(root, p, 0);
}
```

编译器通常在 `-O1` 及以上优化级别将尾递归转换回循环，因此没有运行时开销。这个模式在 BSC 的 parson `path_get_value` 中是必需的。

### 10.2 链式调用，不要命名中间借用

**问题：** 将中间值提取到命名变量中可能会将其借用延长到超出你期望的范围。

```c
// 可能很尴尬——`obj` 在整个块中借用 `val`
JSON_Object* _Borrow obj = val.get_object();
obj->set_string(name, value);
// ... 大量代码 ...
// 对 `val` 的任何其他借用与 `obj` 冲突
```

**修复：** 当借用只在一个调用中使用时，使用链式调用：

```c
val.get_object()->set_string(name, value);
// 没有命名借用——没有生命周期延长
```

仅在多次使用时才命名借用；否则让它作为临时借用。

### 10.3 严格控制借用作用域

如果必须命名借用但不需要在整个函数中使用它，将其包装在一个块中，以便其生命周期显式结束：

```c
{
    JSON_Object* _Borrow obj = val.get_object();
    obj->set_string(name, value);
}   // <-- `obj` 在此处释放
val.validate(&_Mut schema);   // 现在可以自由地对 `val` 进行可变借用
```

### 10.4 提取数据，而非借用

**问题：** 你需要来自借用的数据，但之后也需要自由使用被借用者。

```c
// 错误——`key` 间接延长了 `parent` 的借用
const String* _Borrow key = parent->get_name(0);
parent->set_value(key, ...);   // 可变借用与 key 冲突
```

**修复：** 克隆所需数据，释放借用，然后进行修改。

```c
String key_owned;
{
    const String* _Borrow key = parent->get_name(0);
    key_owned = clone_string(key);
}
parent->set_value(&_Const key_owned, ...);   // parent 再次空闲
```

## 11. 完整示例

```c
#include <stdio.h>

void printValue(const int *_Borrow ref) {
    printf("value = %d\n", *ref);
}

void doubleValue(int *_Borrow ref) {
    *ref = *ref * 2;
}

const int *_Borrow getFirst(const int *_Borrow arr, int len) {
    return arr;
}

int main() {
    int x = 21;

    // 不可变借用
    const int *_Borrow cr = &_Const x;
    printValue(cr);
    printValue(&_Const x);   // 内联借用

    // 可变借用
    int *_Borrow mr = &_Mut x;
    doubleValue(mr);
    // NLL：mr 的生命周期在最后一次使用（doubleValue 调用）时结束
    printf("x = %d\n", x);  // 可以：x 不再被冻结

    // 多个不可变借用是可以的
    const int *_Borrow r1 = &_Const x;
    const int *_Borrow r2 = &_Const x;
    printf("r1=%d, r2=%d\n", *r1, *r2);

    // 结构体字段借用——只有被借用的字段被冻结
    struct Point { int x; int y; };
    struct Point p = {.x = 10, .y = 20};
    int *_Borrow px = &_Mut p.x;  // 只有 p.x 被冻结
    p.y = 30;                      // 可以：p.y 是不同字段
    *px = 50;
    // px 的 NLL 在此处结束（最后一次使用在上面）

    // 可变到不可变的隐式转换
    int val = 5;
    int *_Borrow mp = &_Mut val;
    const int *_Borrow ip = mp;  // 可以：隐式转换

    // 字符串字面量自动借用
    void print_str(const char *_Borrow s);
    print_str("hello");  // 编译器自动插入 &_Const *

    return 0;
}
```

## 11. `_Borrow _ArrayElem`：借用到数组

使用 `&_Mut arr[i]` 或 `&_Const arr[i]` 获取数组元素的地址会产生一个 `_Borrow _ArrayElem` 指针。这是一个**记住它指向数组内部**的借用，因此允许对其进行下标和算术操作（与普通 `_Borrow` 不同）。

```c
_Safe int foo(void) {
    int arr[4] = {1, 2, 3, 4};
    int *_Borrow _ArrayElem p = &_Mut arr[0];
    p = p + 1;            // 可以：_Borrow _ArrayElem 支持 +
    p += 1;               // 可以：+=
    ++p;                  // 可以：++
    int x = p[0];         // 可以：下标

    int *_Borrow q = p;   // 可以：隐式降级为普通借用
    // q + 1;             // 错误：普通借用禁止算术
    // q[0];              // 错误：普通借用禁止下标
    return x;
}
```

特定于 `_Borrow _ArrayElem` 的规则：

- 允许的操作符：`[]`、二元 `+`、`-`、`+=`、`-=`、`++`、`--`。所有其他普通 `_Borrow` 的限制仍然适用（没有 `*` 算术、没有按位操作等）。
- **隐式降级** `T *_Borrow _ArrayElem` → `T *_Borrow` 是允许的（相当于"在当前元素处获取新的普通借用"）。反向显式转换被**禁止**。
- 对于混合 `_Safe`/`_Unsafe` 声明，`_Borrow _ArrayElem` 是一个单一的限定词单元——`_Safe` 重新声明可以向裸指针的 `_Unsafe` 参数添加 `_Borrow _ArrayElem`，但一旦声明了 `_Borrow` 或 `_Borrow _ArrayElem`，就不能在它们之间切换。
- `sizeof(T *_Borrow _ArrayElem) == sizeof(T *)`。

除了上述操作符和这些转换规则外，**适用于普通 `_Borrow` 的所有其他规则（生命周期、冻结、NLL、可空性、类型兼容性、不能借用借用等）同样适用于 `_Borrow _ArrayElem`**。大多数时候你只会通过 `&_Mut arr[i]` 隐式看到此类型，并立即将其绑定到普通 `_Borrow`；只有当你需要下标或步进时才显式命名它。

> 关于所有权（拥有指针），请参见 `bsc-ownership` 技能
> 关于需要借用的安全区域，请参见 `bsc-safe-zone` 技能
> 关于借用的可空性，请参见 `bsc-nullability` 技能
> 关于借用错误（BSC-E02xx），请参见 `bsc-errors` 技能
