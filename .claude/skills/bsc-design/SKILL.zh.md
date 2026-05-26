---
name: bsc-design
description: "BiSheng C 设计和架构决策。在编写涉及任何非平凡 BSC 变更的计划、提案或实现之前——包括：新模块或 API、新结构体（特别是 _Owned struct）、具有 _Owned/_Borrow 参数的新函数、改变所有权流的重构、向现有类型添加特性、选择 _Safe/_Unsafe 边界的位置、在值类型和 *_Owned 指针之间做决定、成员函数 vs 自由函数的组织、联合体替代策略或析构函数设计——加载此技能。在计划时使用此技能，而不是在计划编写完成后。仅在以下情况下跳过：拼写修正、注释编辑、不改变类型或签名的单行错误修复。"
---

# BiSheng C 设计技能

BSC 不仅仅是"带注释的 C"——所有权系统改变了 API 的塑造方式。
本技能涵盖**从空白文件开始**时面临的设计决策，而不是移植现有 C 代码时。

## 1. 经验法则（请先阅读）

设计任何 BSC API 时要参考的八条规则。如果你只记住一件事，请记住**规则 2**。

### 规则 1 —— 返回值，借用参数
按值返回 `_Owned`；将 `_Borrow` 作为参数。只有当函数真正**消耗**（移动）值时才使用 `_Owned` 参数（例如 `Vec::push`、`set_string`）。

```c
// 好：调用者保留所有权，函数借用
_Safe String format_greeting(const String* _Borrow name);

// 不好：调用者必须移动一个它可能仍想使用的值
_Safe String format_greeting(String name);
```

### 规则 2 —— 让 `_Unsafe` 面尽可能小

不要仅仅因为某一行需要 `_Unsafe` 就将整个函数标记为 `_Unsafe`。用 `_Unsafe { ... }` 包装最小的部分，并保持函数的接口为 `_Safe`。一个 `_Unsafe` 接口是**传染性的**——每个调用者都要付出代价。

```c
// 好：接口 _Safe，只有转换是不安全的
_Safe String from_cstr(const char* s) {
    _Unsafe { return String::from(s); }
}

// 不好：整个函数因一行代码而变成 _Unsafe
_Unsafe String from_cstr(const char* s) {
    return String::from(s);
}
```

### 规则 3 —— 慎重选择 `T` 还是 `T *_Owned`

这是一个真正的设计决策，而不是默认选择。两者都是有效的；根据类型的大小、生命周期和使用位置来选择。

**按值返回 `T`，当：**
- 类型**小而中等**（~小于几百字节）——移动成本低。
- 调用者会局部使用它并在作用域结束时释放。
- 不需要可空性（没有"无结果"情况需要表示）。
- 你想要零分配的构造。

```c
_Safe String String::new(void);                       // 小，局部使用
_Safe JSON_Value json_parse_file(String filename);    // 中等，调用者拥有
```

**返回 `T *_Owned`，当：**
- 类型**大**——指针移动是 `O(1)`，值移动会复制字节。
- 结果可能为**空**（`T *_Owned _Nullable`）——值类型无法自然表示缺失而无需 `Option<T>` 的开销。
- 结果将**存储在持有指针的容器中**（`Vec<Node *_Owned>` 用于图/树节点——递归类型无法按值存储）。
- 调用者的生命周期模型需要**显式的 `safe_free`**（例如，移交给 C 代码、自定义分配器）。

```c
_Safe Buffer *_Owned Buffer::with_capacity(size_t mb);   // 大
_Safe Node *_Owned _Nullable find(Tree* _Borrow t, Key k);  // 可能不存在
```

**现实检查：** parson 两者都用。`json_parse_file` 按值返回 `JSON_Value`（结构体大约 100 字节），但内部 `JSON_Value_Value` 持有 `JSON_Object *_Owned` 和 `JSON_Array *_Owned`，因为这些子类型是堆分配的且需要可为空。不要假设按值返回总是对的；假设**正确的选择取决于大小和可空性**。

### 规则 4 —— 多个 `_Const` 读取者 OR 一个 `_Mut` 写入者（不能同时）

`_Const` 借用在组合上很友好：任意数量的不可变借用可以在同一个值上共存。检查器禁止的是**将 `_Mut` 与任何其他借用重叠**——另一个 `_Mut` 或任何 `_Const`。

```c
// 没问题：多个读取者，没有写入者
const T* _Borrow r1 = &_Const x;
const T* _Borrow r2 = &_Const x;
const T* _Borrow r3 = &_Const x;
read(r1); read(r2); read(r3);   // 全部可以

// 不好：写入者与读取者重叠
const T* _Borrow r = &_Const x;
T* _Borrow w = &_Mut x;          // 错误：r 仍然活跃
modify(w);

// 不好：两个写入者重叠
T* _Borrow w1 = &_Mut x;
T* _Borrow w2 = &_Mut x;          // 错误：w1 仍然活跃
```

**设计启示：** 如果你的 API 需要多个读取者，使用 `const T* _Borrow` 并自由传递。如果需要修改，使用 `T* _Borrow`（可变）并确保在调用期间没有其他借用活跃。如果你发现自己想要重叠的 `_Mut` 借用，设计就是错的——重新组织。

```c
// 常见的设计错误：尝试对 `val` 进行两次可变借用
JSON_Object* _Borrow obj = val.get_object();   // 可变借用 #1
val.validate(&_Mut schema);                     // 可变借用 #2 —— 冲突

// 修复：严格控制第一个借用的作用域
{
    JSON_Object* _Borrow obj = val.get_object();
    obj->set_string(...);
}   // 借用 #1 在此结束
val.validate(&_Mut schema);   // 现在可以了
```

### 规则 5 —— 析构函数拥有所有权，函数进行移动
`_Owned struct` 的析构函数在作用域退出时**自动**触发。不要编写显式的 `free_*(ptr)` 函数，除非类型是堆分配的且由原始指针持有（即析构函数无法到达）。

```c
_Owned struct Config {
    String name;     // ~String 自动触发
    Vec<int> values; // ~Vec 自动触发

    ~Config(Config this) {
        // 通常为空——子字段的析构函数自行运行
    }
};
```

### 规则 6 —— 成员用于分发，自由函数用于其他一切
在以下情况下使用 `Type::method(...)`：(a) 函数在逻辑上属于该类型，(b) 你希望在调用点使用 `this->method()` 点语法，或 (c) 你需要 trait 分发。否则，自由函数更简单且组合性更好。

```c
// 成员：属于该类型，读起来自然
_Safe size_t String::length(const String* _Borrow this);

// 自由函数：实用工具，没有自然的"拥有者"
_Safe String json_parse_file(String filename);
```

### 规则 7 —— 审慎选择不安全接缝
每个 BSC 库至少有一个 `_Unsafe` 接缝。将其放置在 BSC 的模型与现实不匹配的**边界**处：

- **FFI / 系统调用**（`fopen`、`read`、缓冲区上的指针算术）
- **内部可变性 / 共享引用**（如 `Rc<T>` 的引用计数容器暴露 `_Unsafe` 偷看方法）
- **性能关键的热路径**（检查器会强制不必要的克隆的地方）

可能未命中的查找**不是**接缝——返回 `T *_Borrow _Nullable`（编译时可空性追踪使调用点保持 `_Safe`）。参见规则 8。

永远不要将接缝放在业务逻辑的中间。

### 规则 8 —— 优先选择 `_Nonnull`；仅在"缺失"是真实情况时才使用 `_Nullable`

`_Owned` 和 `_Borrow` 默认为 `_Nonnull`。保持这样，除非 API 确实有需要表示的无结果/可选/缺失情况。`_Nullable` 强制每个使用点在解引用之前进行空检查——在指针实际上从不为空时选择它只会给调用者增加无谓的负担。

**保持默认 `_Nonnull`，当：**
- 参数是必需的（没有"无输入"的情况）
- 函数成功时始终会产生返回值
- 结构体字段在构造后始终被填充

**使用 `_Nullable`，当：**
- 分配可能失败并且你将其暴露出来：`T *_Owned _Nullable try_alloc(size_t)`
- 清理路径持有可能尚未设置的指针：`Node *_Owned _Nullable item = nullptr;`
- 结构体字段确实可选
- setter 接受"清除"作为有效输入：`void set_cache(Cache *_Owned _Nullable c)`

```c
// 好——默认非空；调用者不需要空检查
_Safe void render(const Canvas *_Borrow c, const Frame *_Borrow f);

// 好——分配可能失败，可空性将其暴露出来
_Safe Buffer *_Owned _Nullable try_create_buffer(size_t n);

// 不好——出于谨慎声明为 _Nullable；调用者无谓地进行空检查
_Safe void render(const Canvas *_Borrow _Nullable c, const Frame *_Borrow _Nullable f);
```

更改现有 API 上指针的可空性（Nonnull ↔ Nullable）是一个**破坏性变更**——每个调用者的空检查假设都会改变。在设计时决定，而不是在补丁时。

> 关于编译时可空性追踪机制和空检查模式，请参见 `bsc-nullability` 技能。

---

## 2. 设计类型

### 值类型、`_Owned struct` 还是 trait？

| 你的类型有... | 使用 |
|---|---|
| 纯数据（没有堆指针，没有清理） | 普通 `struct`（不是 `_Owned`） |
| 拥有堆内存的字段（`String`、`Vec`、`*_Owned`） | `_Owned struct` |
| 需要动态分发（一个接口后有多个"种类"） | `_Trait` + `_Impl` |
| 一组封闭的变体（如联合体） | 带有标签字段 + 内部联合体替代的 `_Owned struct` |

**除非确实需要动态分发，否则不要使用 `_Trait`。** 如果在编译时知道具体类型，普通 `_Owned struct` 或泛型 `<T>` 更快更清晰。

### 替换 C 联合体

C 联合体允许你将 `double`、`char*` 和指针放在同一个存储槽中。在 BSC 中这是不安全的：析构函数无法知道哪个变体是活跃的，因此无法运行正确字段的析构函数。

**两种选择：**

1. **标签结构体**——所有字段共存；析构函数在所有字段上运行（即使在非活跃的字段上）。简单但浪费内存。Parson 的 `JSON_Value_Value` 这样做，带有一个 FIXME 注释承认了成本。

2. **基于 Trait 的和类型**——每个变体一个 trait 和一个 `_Impl`。动态分发开销，但内存高效且类型安全。当变体大小差异很大时使用。

**经验法则：** 如果最大的变体 < 最小变体的 3 倍，使用标签结构体。如果大小差异很大（例如，一个变体持有 1KB 缓冲区），使用 traits。

### 析构函数设计

析构函数体通常是**空的**。BSC 自动为每个 `_Owned` 字段插入析构函数调用。仅在以下情况下编写显式清理：

- 结构体持有一个指向堆内存的**原始指针**（不是 `*_Owned`）。你必须手动 `safe_free`。
- 有除内存之外的**资源**（文件句柄、套接字、锁）需要释放。
- 你需要在清理之前运行**逻辑**（日志记录、通知观察者）。

```c
_Owned struct FileHandle {
    FILE *_Owned fp;   // 析构函数会自动触发吗？不——FILE* 没有析构函数

    ~FileHandle(FileHandle this) {
        _Unsafe { fclose(__move_to_raw(this.fp)); }
    }
};
```

---

## 3. 设计 API

### 值进值出 vs 借用进借出

```c
// 模式 A：所有权转移
_Safe JSON_Object JSON_Object::new(void);            // 产生拥有的值
_Safe void Obj::set_value(Obj* _Borrow this, JSON_Value v);  // 消耗 `v`

// 模式 B：只读访问
_Safe const String* _Borrow Obj::get_name(const Obj* _Borrow this, size_t i);
_Safe size_t Obj::get_count(const Obj* _Borrow this);

// 模式 C：通过借用的修改
_Safe void Obj::clear(Obj* _Borrow this);
```

**规则：** 读取 → `_Borrow this` + `_Borrow` 返回。修改 → `_Borrow this`（可变）+ `void` 返回或状态码。构造 → `_Owned` 返回，没有 `this`。

### 返回依赖于局部变量的 `_Borrow`

这最终会困扰每个人。如果你计算一个局部 `String` 并想返回通过它查找得到的东西的 `_Borrow`，检查器会拒绝它，因为局部变量在作用域退出时销毁。

**三种合法的修复方法：**

1. **重新组织**——通过借用从调用者处获取键：
   ```c
   _Safe T* _Borrow lookup(Table* _Borrow t, const String* _Borrow key);
   ```

2. **递归**——如果局部变量是输入的转换，将转换后的值传递给递归调用，其生命周期自包含。

3. **最小的 `_Unsafe` 接缝**——对于检查器无法建模的查找（哈希表命中），在严格的 `_Unsafe` 块内通过原始指针进行转换。记录原因。（Parson 的 `dotget_value` 这样做。）

### 成员函数布局

```c
// 在 .hbs 中声明
_Safe RetType TypeName::method(TypeName* _Borrow this, ArgT arg);

// 在 .cbs 中定义——相同签名
_Safe RetType TypeName::method(TypeName* _Borrow this, ArgT arg) {
    ...
}

// 调用
obj->method(arg);     // TypeName::method(obj, arg) 的语法糖
```

对于 `_Safe` 成员函数，`this` **始终**是 `_Borrow`（const 或 mut）或值类型（对于构造函数）。永远不要使用原始 `T*`。

### 构造函数模式

BSC 没有特殊的构造函数语法。使用约定俗成的 `Type::new`（以及变体 `with_capacity`、`from`、`default`）：

```c
_Safe String String::new(void);
_Safe String String::with_capacity(size_t cap);
_Unsafe String String::from(const char* cstr);

_Safe Config Config::new(void);
_Safe Config Config::from_file(String path);
```

---

## 4. 设计所有权流

### 所有权在哪里

对于每个堆分配，决定：**谁拥有它，持续多长时间，以及它如何被释放？** 然后让类型反映答案。

```c
// 所有者：结构体。生命周期：与结构体一起。释放：由析构函数。
_Owned struct Cache { Vec<Entry> entries; };

// 所有者：add() 的调用者。生命周期：直到被移入 vec。
_Safe void Cache::add(Cache* _Borrow this, Entry e);  // 消耗 `e`

// 所有者：缓存。作为借出用于只读使用。
_Safe const Entry* _Borrow Cache::get(const Cache* _Borrow this, size_t i);
```

### API 边界的移动语义

当函数接受 `_Owned T`（或按值接受 T（对于拥有的类型））时，调用者在调用后失去对变量的访问权限。设计你的 API，使调用者要么：

- **有意地移入**（例如 `vec.push(item)` ——item 被消耗，调用者预料到这一点）
- **传递一个克隆**，如果他们需要继续使用该值（`vec.push(item.clone())`）

不要有"有时消耗，有时不消耗"的函数——选择一个并坚持下去。

### 共享：避免，或使用 `Rc<T>`

BSC 默认是单一所有者的。如果两个地方需要拥有同一个值，使用 stdlib-advanced 的 `Rc<T>`（引用计数）。不要发明你自己的共享方案——你会破坏所有权不变性。

### 与工作线程共享长期存在的只读状态

`Rc<T>` 是单线程的。对于工作线程在进程生命周期内需要可读的不可变数据（配置、路由器、查找表），正确的模式是：

1. 使用 `safe_malloc` 进行堆分配以获得稳定地址。
2. 在 `_Unsafe` 块内调用 `__move_to_raw` 将所有权转移到原始指针。不会在其上运行析构函数——这是一个有意的进程生命周期分配。记录下来。
3. 将原始指针存储为普通（非 `_Owned`）共享上下文结构体中的 `const T* _Nonnull`，传递给每个线程。
4. 仅使用普通复制类型（`int` fd、索引）跨越线程边界。绝不要在线程之间传递 `_Borrow` 或 `_Owned` 指针。`_Borrow → raw` 在 `_Safe` 和 `_Unsafe` 中都被**禁止**——在任何地方都没有从 `_Borrow` 指针到原始指针的转换。

```c
// 在线程池设置函数的 _Unsafe 块中：
Config *_Owned cfgp = safe_malloc(config);   // 稳定的堆地址；获取所有权
Router *_Owned rtp  = safe_malloc(router);

// 有意的进程生命周期分配——没有配对的释放。
static struct ServerCtx ctx;
ctx.config = (const Config* _Nonnull)__move_to_raw(cfgp);
ctx.router = (const Router* _Nonnull)__move_to_raw(rtp);

// 工作线程从原始指针重建 _Borrow：
_Safe void worker(int fd, void* _Nonnull ctx_raw) {
    struct ServerCtx* ctx = _Unsafe((struct ServerCtx*)ctx_raw);
    const Config* _Borrow cfg = _Unsafe(&_Const *(ctx->config));
    ...
}
```

当赋值给 `_Nonnull` 类型字段时，需要显式 `(const T* _Nonnull)` 转换，因为 `__move_to_raw` 返回一个可空的原始指针，而字段的声明类型强制非空。`ServerCtx` 结构体本身必须是 `static`（或堆分配的），以便其地址在设置函数返回后仍然有效。

---

## 5. 设计 `_Safe` / `_Unsafe` 边界

### 在设计时选择边界，而不是在调试时

在编写代码之前，列出哪些函数将是 `_Safe` 哪些是 `_Unsafe`。常见的库结构：

```
公开 API（.hbs）
  ├── _Safe 占 90% 的表面
  └── _Unsafe 用于：
        - 接受原始 C 字符串（const char*）的构造函数
        - 与现有 C 库交互的函数
        - 关闭系统资源的析构函数

内部实现（.cbs）
  ├── _Safe 在模型允许的地方
  └── _Unsafe 块（不是整个函数）用于：
        - 指针算术
        - 检查器过度拒绝的别名
```

### 何时在 `_Safe` 函数内部使用 `_Unsafe` 块

以下情况完全合法：

- 你需要 `printf`/`puts`（格式化输出在 BSC 中被视为不安全）
- 你在已知安全的缓冲区上进行指针算术（`string->get(i)` 返回原始 char 指针）
- 你在 `_Owned` 和原始之间进行 `__take_from_raw` / `__move_to_raw` 的转换（C FFI）

以下情况不合法：

- 因为"烦人"而跳过借用检查器
- 绕过你不理解的诊断——先弄清楚它为什么触发

---

## 6. 反模式

### 反模式：一路 `_Unsafe` 到底
症状：每个函数都是 `_Unsafe`。你在用 .cbs 扩展名写 C。
修复：将公开 API 标记为 `_Safe`，并将 `_Unsafe` 部分推入每个函数内部的块中。检查器仍然在边界帮助你。

### 反模式：上帝结构体
症状：一个 `_Owned struct` 有 15 个字段，大多数在大多数代码路径中未被使用。
修复：拆分为通过 `_Owned` 字段组成的更小结构体。析构函数可以组合；借用可以组合；设计保持模块化。

### 反模式：返回原始 `T*` 而不是 `T *_Borrow`
症状：查找返回 `T*`（原始）而不是 `T *_Borrow`，强制调用者仅仅为了使用它而进入 `_Unsafe`。
修复：如果函数总是成功，返回 `T *_Borrow`。如果可能失败，返回 `T *_Borrow _Nullable`——编译时可空性追踪使调用者保持 `_Safe`，同时表示未命中的情况。

### 反模式：析构函数做得太多
症状：析构函数深入到不相关的子系统中，调用日志记录，修改全局状态。
修复：析构函数只应释放 `this` 拥有的资源。副作用属于调用者调用的显式方法。

### 反模式：先为 C 设计，然后"移植"
症状：你写了一个 C 风格的 API（`JSON_Value* parse(const char*)`），现在正在与借用检查器作斗争。
修复：从 BSC 类型开始（`_Owned JSON_Value parse(String)`），让签名驱动实现。

---

## 7. 一个实例

一个简单的键值存储，BSC 优先设计：

```c
// 在 kvstore.hbs 中
#include "string.hbs"
#include "vec.hbs"

_Owned struct KVEntry {
    String key;
    String value;
};

_Owned struct KVStore {
_Public:
    Vec<KVEntry> entries;

    ~KVStore(KVStore this) {
        // 空——Vec<KVEntry> 自动析构，它会自动析构每个 KVEntry，
        // 它会自动析构每个 String。
    }
};

_Safe KVStore KVStore::new(void);
_Safe void KVStore::set(KVStore* _Borrow this, String key, String value);
_Safe const String* _Borrow KVStore::get(const KVStore* _Borrow this, const String* _Borrow key);
_Safe size_t KVStore::len(const KVStore* _Borrow this);
```

设计说明：
- **规则 1：** `new()` 按值返回，修改方法使用 `_Borrow this`，读取方法使用 `const _Borrow this`。
- **规则 3：** `KVStore` 是值类型——API 不需要 `*_Owned`。
- **规则 5：** 析构函数为空；字段析构函数处理一切。
- **规则 7：** `_Unsafe` 接缝仅需在 `set` 内部进行去重（在线性扫描上进行查找然后插入在 `_Safe` 中没问题；哈希探测需要它）。

---

## 8. 快速参考

| 问题 | 答案 |
|---|---|
| 应该返回 `T` 还是 `T *_Owned`？ | `T` 用于小 + 非空 + 局部使用；`*_Owned` 用于大、可空或容器存储 |
| 应该接受 `T`、`T *_Owned` 还是 `T *_Borrow`？ | 读取用 `_Borrow`，修改用 mut-`_Borrow`，消耗用 value/`_Owned` |
| 这应该是 `_Safe` 还是 `_Unsafe`？ | `_Safe` 除非你不能——然后包装 `_Unsafe` 块在里面 |
| 我需要析构函数体吗？ | 只有在有原始指针、非内存资源或自定义清理逻辑时才需要 |
| 这应该是成员还是自由函数？ | 如果它在逻辑上属于该类型或你想要点语法，则为成员；否则为自由函数 |
| 联合体还是 trait 和类型？ | 小变体用标签结构体；大小差异很大的变体用 trait |
| 我可以共享所有权吗？ | 使用 stdlib-advanced 的 `Rc<T>`；不要发明共享方案 |

在写一行 BSC 代码之前：先草拟类型，注释谁拥有什么，决定 `_Unsafe` 接缝放在哪里。在那之后，代码几乎自己就能写出来。
