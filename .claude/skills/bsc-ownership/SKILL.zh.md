---
name: bsc-ownership
description: "BiSheng C 所有权系统。当你需要理解 _Owned 指针、移动语义、_Owned struct、析构函数、RAII、_Public/_Private 访问、_Nullable、safe_malloc/safe_free、所有权转移规则或 _Owned 与 traits/联合体/函数指针的结合使用时，使用此技能。"
---

# BiSheng C 所有权技能

## 关键：`_Owned` 语法

**`_Owned` 放在 `*` 之后，而不是类型之前。** 它是一个指针限定词，像 `const` 一样。

```c
// 正确——_Owned 在 * 之后
int *_Owned p = safe_malloc(42);

// 错误——_Owned 在类型之前（无法编译）
_Owned int* p = safe_malloc(42);
```

`_Borrow` 也一样：写 `int *_Borrow`，不是 `_Borrow int*`。

## 1. 概述

编译时内存安全的移动语义。在编译时防止 use-after-free 和 double-free。一旦所有权转移，原始变量就失效了。

使用 `safe_malloc` / `safe_free`（来自 `bishengc_safety.hbs`）作为惯用的分配 API。原始的 `malloc`/`free` 需要 `_Unsafe` 上下文和显式转换。

## 2. safe_malloc / safe_free

签名：`T *_Owned safe_malloc<T>(T t)` ——分配，初始化为 `t`，返回 `T *_Owned`。
`void safe_free(void *_Owned)` ——释放一个 `_Owned` 指针。`safe_malloc` 不需要转换：
```c
#include "bishengc_safety.hbs"
_Safe void example(void) {
    int *_Owned p = safe_malloc(42);       // 不需要转换
    safe_free((void *_Owned)p);            // 必须转换为 void *_Owned
}
```

**malloc/free 替代（需要 `_Unsafe` 上下文）：**
```c
_Unsafe {
    int *_Owned p = (int *_Owned)malloc(sizeof(int));  // raw→owned 转换
    *p = 42;
    free((void *_Owned)p);
}
```

## 3. 拥有的指针

```c
#include "bishengc_safety.hbs"

int *_Owned create(int val) {
    return safe_malloc(val);  // 所有权转移给调用者
}

void consume(int *_Owned p) {
    int val = *p;
    safe_free((void *_Owned)p);
}

_Safe int main(void) {
    int *_Owned p = create(21);
    int *_Owned p2 = p;        // 所有权移动；p 现在已失效
    consume(p2);               // p2 现在已失效
    int *_Owned _Nullable maybe = nullptr;  // 可空拥有的指针
    return 0;
}
```

## 4. 多级指针释放（从内到外）

对于多级指针，从内到外释放。对于有 `_Owned` 指针成员的结构体，先释放所有 `_Owned` 成员，然后释放结构体指针。

```c
#include "bishengc_safety.hbs"
struct S { int *_Owned p; int *_Owned q; };

_Safe void foo(void) {
    // 多级指针：先释放内部，再释放外部
    int *_Owned inner = safe_malloc(1);
    int *_Owned *_Owned pp = safe_malloc(inner);
    safe_free((void *_Owned)*pp);
    safe_free((void *_Owned)pp);

    // 有 _Owned 成员的结构体：先释放成员，再释放结构体
    struct S s = {.p = safe_malloc(2), .q = safe_malloc(3)};
    struct S *_Owned sp = safe_malloc(s);
    safe_free((void *_Owned)sp->p);
    safe_free((void *_Owned)sp->q);
    safe_free((void *_Owned)sp);
}
```

## 5. 规则

- **`_Owned` 放在 `*` 之后**：写 `int *_Owned`，不是 `_Owned int*`
- `_Owned` 只能修饰**指针类型**，不能修饰非指针类型
- `_Owned` 类型具有**移动语义**：赋值、传递、返回**转移**所有权
- 转移后，原始变量**已失效**——任何使用都是编译错误
- 在作用域结束前，`_Owned` 变量**必须**释放所有权
- 释放方式：(a) 传递给接受 `_Owned` 的函数，(b) `safe_free((void *_Owned)p)`，(c) 返回，(d) 赋值给另一个 `_Owned` 变量
- 不允许对 `_Owned` 指针进行指针算术（没有 `+`、`-`、`[]`、`++`、`--`）。对于拥有数组且需要 `[]` 的 `_Owned` 指针，使用 `_Owned _ArrayElem` 替代——参见 §8。
- 允许比较操作符（`==`、`!=`、`<` 等）
- **任意方向**的 `_Owned` 和原始指针之间没有隐式转换——严格类型匹配
- `_Owned` 和原始指针之间的显式转换需要 `_Unsafe` 上下文
- 例外：在安全上下文中允许 `T *_Owned` -> `void *_Owned`

### `_Owned` 的禁止用途
- **全局变量**（包括函数局部 `static`）
- **联合体成员**：`_Owned` 不能修饰联合体类型成员
  ```c
  union U { int *_Owned p; };  // 错误
  ```
- **数组元素**：不能在数组中存储 `_Owned` 指针，包括带有 `_Owned` 成员的结构体作为数组元素。同样的限制适用于 `T *_Owned _ArrayElem` 的指向类型（内部元素类型不能有 `_Owned` 成员）

### 拥有字段的数组类型的回退：使用 `_Unsafe` 的原始 `T*`

当 `T *_Owned _ArrayElem` 因 `T` 包含 `_Owned` 字段而被拒绝时（例如，带有 `cstring` 字段的 `Header`），回退方案是完全在 `_Unsafe` 中使用原始 `T*` 字段配合手动的 `malloc`/`realloc`/`free`。每个 `_Unsafe` 块必须附带理由注释。

```c
// cstring 有 _Owned _ArrayElem buf -> Header "包含拥有的类型"
typedef struct Header { cstring name; cstring value; } Header;

typedef struct Router {
    Route* routes_data;      // raw T* — Route 的 _Owned _ArrayElem 被拒绝
    size_t routes_len;
    size_t routes_cap;
} Router;

// 所有分配、访问和释放都包装在 _Unsafe 中：

static inline _Safe void router_routes_init(Router* _Borrow r, size_t cap) {
    // _Unsafe: raw malloc — safe_malloc_array 无法处理 Route（包含 _Owned 字段）
    _Unsafe {
        r->routes_data = (Route*)malloc(cap * sizeof(Route));
        if (!r->routes_data) bsc_bad_alloc_handler(cap * sizeof(Route));
    }
    r->routes_cap = cap;
}

_Safe void Router_free(Router r) {
    for (size_t i = 0; i < r.routes_len; i++) {
        // _Unsafe: 可空的原始指针下标（routes_data 是 raw Route*）
        _Unsafe { Route_free(r.routes_data[i]); }
    }
    // _Unsafe: raw free — safe_free_array 无法处理 Route（包含 _Owned 字段）
    _Unsafe { free((void*)r.routes_data); }
}
```

### 可空的 `_Owned` 指针
- `int *_Owned _Nullable p = nullptr;` 允许空的拥有的指针
- `__take_from_raw` 和 `__move_to_raw` 保留可空性

### 逻辑和条件操作符
- 逻辑操作符（`!`、`&&`、`||`）可在 `_Owned` 指针上工作（空检查，不消耗所有权）
- `_Owned` 指针可以作为 `if`/`while`/`do-while`/`for`/三目运算符的条件，但**不能**用于 `switch`
- 允许隐式 `_Owned` -> `_Bool` 转换（不消耗所有权）

### `_Owned` 与 `_Trait` 类型
- 隐式转换：如果 `S` 实现了 `T`，则 `S *_Owned` 可以转换为 `_Trait T *_Owned`
- 可以通过 `_Trait T *_Owned` 指针调用 trait 方法

### 函数指针匹配
- 函数指针类型必须与 `_Owned` 注解完全匹配——不能将有 `_Owned` 参数的函数赋值给无 `_Owned` 参数的指针，反之亦然

### 显式所有权转移接口
- `__move_to_raw(p)` ——移出所有权，返回原始指针
- `__take_from_raw(p)` ——从原始指针获取所有权，返回 `_Owned` 指针
- 两者都保留可空性

### 转换顺序很重要
在 `T *_Owned` 和 `void *` 之间转换时：
- **顺序 1**：`T *_Owned` → `void *_Owned` → `void *`（内部的 `_Owned` 指针必须**不**拥有所有权）
- **顺序 2**：`T *_Owned` → `T *` → `void *`（内部的 `_Owned` 指针**保持**所有权）

反向转换 `void *_Owned` → `T *_Owned` 在变量仍然拥有内存时是允许的（在 `_Unsafe` 中），**但转换后结果结构体的内部 `_Owned` 成员并不拥有其指向的内容**。在读取之前，你必须要么重新赋值它们，要么将其视为原始指针。示例：`struct S *_Owned sp = _Unsafe((struct S *_Owned)memAlloc(...));` 之后，在 `sp->p` 被重新赋值之前，将 `sp->p` 作为 `int *_Owned` 读取是编译错误。

### `_Owned` 与泛型
在泛型函数中，当 `_Owned` 修饰泛型类型参数 T（作为 `T _Owned` 或 `_Owned T`）时，`_Owned` 应用于完整的类型 T。当 T 实例化为 `int*` 时，结果是 `int* _Owned`。

## 6. _Safe / _Unsafe 上下文

- `_Safe` 函数使用 `safe_malloc`/`safe_free`；原始的 `malloc`/`free` 需要 `_Unsafe` 块
- 没有 `_Safe` 或 `_Unsafe` 的函数默认是非安全的

## 7. 拥有的结构体（RAII）

```c
#include "bishengc_safety.hbs"
#include <stdio.h>

_Owned struct Buffer {
_Private:                    // _Private 是默认访问修饰符
    char *_Owned data;
    int len;
_Public:
    int cap;
    ~Buffer(Buffer this) {   // 析构函数——在作用域结束时自动调用
        safe_free((void *_Owned)this.data);  // _Owned 成员必须在此释放
    }
};

Buffer Buffer::new(int capacity, char init) {
    return (Buffer){ .data = safe_malloc(init), .len = 0, .cap = capacity };
}

_Safe int main(void) {
    {
        Buffer buf = Buffer::new(256, '\0');
    }   // ~Buffer 在此自动调用
    {
        Buffer buf1 = Buffer::new(128, '\0');
        Buffer buf2 = buf1;  // 移动：buf1 已失效，只调用 ~Buffer(buf2)
    }
    return 0;
}
```

### 7.1 拥有的结构体规则

- `_Owned struct` 作为一个整体具有移动语义
- 析构函数语法：在结构体体内的 `~TypeName(TypeName this) { ... }`
- 变量超出作用域时自动调用析构函数（如果未被移动）
- **用户不能显式调用析构函数**——只有编译器调用它
- 如果没有定义析构函数，编译器会提供一个默认的
- 结构体内部的 `_Owned` 指针成员**必须**在析构函数中手动释放
- **访问修饰符**：`_Private`（默认）和 `_Public`——只有结构体体内的成员/函数可以访问 `_Private` 成员；扩展函数和外部代码只能访问 `_Public` 成员
- **禁止部分移动**：在作用域结束时，`_Owned struct` 必须要么完全拥有（没有移出任何东西），要么作为一个整体完全移出。移动单个 `_Owned` 成员而保留结构体会导致编译错误
- 全局 `_Owned struct` 变量不会调用其析构函数（包括函数局部 `static`）

### 7.2 陷阱：作为联合体替代的标签结构体带有隐藏成本

当你用包含所有变体字段的 `_Owned struct` 替换 C 联合体时（一种常见模式，因为 BSC 没有安全的 `_Owned` 联合体），**每个实例都承担所有变体的析构函数成本**，而不仅仅是活跃的那个。

```c
// 替换 C 联合体 { Object*; Array*; double; ... }
_Owned struct ValueValue {
_Public:
    String         string;
    double         number;
    Object* _Owned object;   // 每个实例都堆分配
    Array*  _Owned array;    // 每个实例都堆分配
    int            boolean;

    ~ValueValue(ValueValue this) {
        free_Object(this.object);   // 总是运行，即使对于非 Object 值
        free_Array(this.array);
    }
};
```

需要注意的后果：

- 构造会通过 `safe_malloc` 分配**所有**堆指针（每个变体指针字段一个），即使对于原始变体如 `JSONNull` 或 `JSONBoolean`。
- 析构会对**所有**它们调用 `safe_free`——因此任何跨实例的别名（特别是通过 `clone` 路径或 `safe_swap` 模式）会使释放加倍。
- 每个值的内存成本是恒定的，但**在容器中会成倍增加**（`Vec<ValueValue>` 中的 1000 个 null 仍然分配 1000 个 Object + 1000 个 Array）。

**设计时要考虑的缓解措施**（交叉参考 `/bsc-design` 规则 7 和 §2 "替换 C 联合体"）：

- 对于真正异构的变体，使用基于 trait 的和类型
- 延迟初始化（仅当设置变体时才分配内部堆内存）
- 接受成本并用 FIXME 显式记录

当调试使用此模式的代码中的 double-free 时，最可能的原因是 `clone` 或 `safe_swap` 在全字段结构体上引入的字段级别名——参见 `/bsc-common-mistakes` §8。

### 7.3 作为堆缓冲区字段的原始指针（RawVec 惯用语）

普通的 `T *_Owned` **禁止数组下标**（`ptr[i]`）。对于需要索引的堆分配数组，你有两个安全选择：

1. **`T *_Owned _ArrayElem`**（当缓冲区确实是 T 的统一数组时首选）——支持 `[]` 和指针算术，保持在 `_Safe` 中。参见 §8。
2. **原始 `T *` 字段**（下面的 RawVec 惯用语）——当缓冲区被重新解释（混合记录的字节缓冲区）、由非 BSC API（C 库的 `malloc`）分配或不适合 `_ArrayElem` 的统一元素模型时需要。

`_Owned struct` 析构函数在两种情况下都处理释放。

```c
// 错误——_Owned ptr 禁止下标；this->data[i] = v 是编译错误
struct BadBuf {
    uint8_t *_Owned data;
    size_t   used;
};

// 正确——原始 T* 字段；结构体析构函数拥有分配
_Owned struct GoodBuf {
_Public:
    uint8_t *data;   // 原始指针——索引可以工作
    size_t   used;
    size_t   cap;

    ~GoodBuf(GoodBuf this) {
        _Unsafe { free(this.data); }   // 手动释放；结构体是拥有者
    }
};
```

通过原始字段的所有读取和写入必须包装在 `_Unsafe` 中：

```c
_Safe void buf_write(GoodBuf *_Borrow _Nonnull this, uint8_t v) {
    _Unsafe this->data[this->used] = v;   // 原始下标——_Unsafe 语句
    this->used += 1;
}
```

对于普通 `struct`（非 `_Owned`）包装器，模式是相同的：原始 `T *` 字段，所有解引用周围有 `_Unsafe`，在配对的销毁函数（而非析构函数）中显式 `free`。与 `T *_Owned` 的区别是有意且重要的：使用原始 `T *` 时，结构体本身是逻辑拥有者；字段只是一个游标。

### 7.4 `_Owned struct` 必须在文件作用域定义

`_Owned struct S { ... };` 定义只能出现在翻译单元（文件）作用域——与 `_Trait` 定义和函数定义在同一级别。它们**不能**在函数体或块内定义（这与标准 C 不同，标准 C 允许普通 `struct` 在局部定义）。

```c
// 正确——文件作用域
_Owned struct Person { _Public: int age; ~Person(Person this) {} };

_Safe int main(void) {
    Person p = {.age = 18};
    // _Owned struct S { };  // 错误：不能在函数作用域中定义
    return 0;
}
```

编译器错误：`_Owned struct cannot be defined in function scope; move the definition to file scope`。

## 8. `_ArrayElem`：可索引数组的拥有指针

第二个限定词 `_ArrayElem` 可以与 `_Owned`（或 `_Borrow`——参见 `bsc-borrowing`）组合，形成指向 **T 的堆分配数组**的指针。

```
T *_Owned _ArrayElem p;   // 拥有 T 的堆数组，支持 p[i]，p == q
```

`_Owned _ArrayElem` 遵循普通 `_Owned` 的所有规则（移动语义、必须释放、不能与原始类型隐式转换等），区别如下：

- **允许 `[]` 下标**（`p[i] = ...`、`int x = p[i];`）
- 指针**算术仍然禁止**（`p += 1` 是错误——即使在 `_Owned _ArrayElem` 上）
- 允许比较（`==`、`!=`）
- 使用**数组专用**的 API 分配/释放（来自 `bishengc_safety.hbs`）：

  ```c
  _Safe T *_Owned _ArrayElem safe_malloc_array<T>(size_t n, T initial);
  _Safe void safe_free_array(void *_Owned _ArrayElem);
  ```

- 原始指针转换使用专用的内置函数（不是 `__move_to_raw` / `__take_from_raw`）：
  - `__move_array_to_raw(p)` ——将 `_Owned _ArrayElem` 移出到原始指针
  - `__take_array_from_raw(p)` ——将原始指针作为 `_Owned _ArrayElem` 获取
- **禁止 `T *`、`T *_Owned` 和 `T *_Owned _ArrayElem` 之间的 C 风格转换**——这三者是不同的类别。使用数组内置函数。
- **指向类型不能包含 `_Owned` 成员**（与适用于 `_Owned` 的普通数组相同的限制）。
- `_ArrayElem` 不能修饰**原始**指针；它只附加到 `_Owned` 或 `_Borrow`。

```c
#include "bishengc_safety.hbs"

_Safe int main(void) {
    int *_Owned _ArrayElem p = safe_malloc_array(10, 0);  // [0]..[9] = 0
    p[3] = 3;                                             // 可以：下标
    // p += 1;                                            // 错误：没有算术
    int *_Owned _ArrayElem q = safe_malloc_array(10, 1);
    if (p == q) { /* ... */ }                             // 可以：比较
    safe_free_array((void *_Owned _ArrayElem)p);
    safe_free_array((void *_Owned _ArrayElem)q);
    return 0;
}
```

**`_Safe` / `_Unsafe` 互操作**：兼容性规则将 `_Owned _ArrayElem` 和 `_Borrow _ArrayElem` 视为**完整的限定词**——`_Safe` 重新声明可以向未注解的 `_Unsafe` 参数添加 `_Owned _ArrayElem`，但你不能跨声明将 `_Owned` "升级"为 `_Owned _ArrayElem` 或在 `_Owned` 和 `_Owned _ArrayElem` 之间移动。

### 何时优先选择 `_Owned _ArrayElem` 而非 RawVec 惯用语（§7.3）

- 缓冲区是一种元素类型 `T` 的统一数组，并且你将其索引为这样的数组 → `_Owned _ArrayElem`。
- 缓冲区被视为原始字节、被重新解释或来自非 BSC 分配器 → 保持原始 `T *` 字段 + `_Unsafe`（RawVec）。

> 另请参见：`bsc-borrowing`、`bsc-safe-zone`、`bsc-nullability`、`bsc-design`、`bsc-errors` 技能
