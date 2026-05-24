# BSC 语言改进建议

> 来源:在本仓库(cc_httpd —— 一个用 BiSheng C 写的 HTTP server)的开发与优化过程中
> 实际踩到的痛点,而非泛泛的愿望清单。每条都附本项目里的具体证据。
> 按"对『BSC 是否值得用』这一判断的影响"排序。

---

## 第一梯队:健全性 —— 修复"编译通过 ≠ 安全"

### 1. 修掉 `return f(&_Const local)` 的 codegen UAF(最高优先级)

> **上游状态:已登记为 [IJC66K](https://gitee.com/bisheng_c_language_dep/llvm-project/issues/IJC66K)
> —— "owned struct 析构函数插入位置不对"(open,2026-04-21,截至 2026-05 仍未修复)。**
> 本节是独立复现与佐证,**不是新发现**。IJC66K 的复现(`return a->equals(&_Const b)` vs
> bind-then-return)与本仓库 `tests/uaf_repro.*` 是同一个 bug 的同一个机制:析构的 `IfStmt`
> 被插到了 `ReturnStmt` 的 `CallExpr` **之前**(IJC66K 附了 AST dump 直接证明这一点)。
> 该 issue 仍 open,说明这个 soundness 漏洞已被官方知晓但尚未修复 —— 这恰好印证了
> "编译过 ⇒ 安全"目前还不可全信。

- **现象/证据**:`return h(&_Const req)` 这类写法会在被调用方读完借用之前就析构
  局部量 `req`,导致 use-after-free。它**编译通过、测试通过,只有 valgrind 抓得到**。
  本仓库的 `src/handler.cbs` 里有 bind-then-return 的注释绕过。
- **可复现(已验证)**:`tests/uaf_repro.cbs` + `tests/uaf_repro.sh`。同一份源码用
  `-DREPRO_BAD` 编出坏写法(`return f(&_Const h)`),不带宏编出 bind-then-return 修复版;
  **两者都零借用检查器报错**。`bash tests/uaf_repro.sh` 跑 valgrind:坏写法 rc=1(UAF),
  修复版 rc=0(干净)。坏写法的关键 trace —— 析构链在 `count_a` 读完之前就跑完了:

  ```
  Invalid read of size 1
     at struct_String_at
     by count_a (tests/uaf_repro.cbs:30)
     by run     (tests/uaf_repro.cbs:39)     <- return count_a(&_Const h)
  Address ... is 0 bytes inside a block of size 64 free'd
     by struct_RawVec_char_D (raw_vec.hbs:15)
     by struct_Vec_char_D    (vec.hbs:21)
     by struct_String_D      (string.hbs:17)
     by struct_Holder_D      (tests/uaf_repro.cbs:22)   <- h 已析构
     by run                  (tests/uaf_repro.cbs:0)
  ```
  注意:程序仍打印出正确的 `count=64` —— "靠运气通过"(freed page 未被复用)由此坐实。
- **影响**:借用检查器的根本承诺是"编译过 ⇒ 内存安全"。一个让 UAF 漏过去的检查器,
  其存在意义被严重削弱 —— 你仍然离不开 valgrind,"它编译过了"还不可信。
- **建议**:
  - 修复析构顺序的代码生成,让"借用被传入调用"的临时量/局部量活到被调用方读完为止。
  - 在修复前,**至少让检查器对 `return f(&_Const local)` 模式发警告**(零成本,
    可阻止他人重新踩坑)。

---

## 第二梯队:消灭本项目里最丑陋模式的根因

### 2. 给 `String` 一个保证 NUL 结尾、生命周期绑定的视图(`c_str()`)

> **上游状态:未见登记 —— 新提议。** 全量对照仓库 782 条 issue(标题 + 正文),没有任何一条
> 提议 `String::c_str()` 或 NUL 结尾的借用视图(正文里出现的 `c_str` 都是测试用例里的变量名)。
> 最接近的 [IDM1B2](https://gitee.com/bisheng_c_language_dep/llvm-project/issues/IDM1B2)
> (closed,"安全区内支持字符串字面量到 const char* 赋值")是另一回事。

- **现象/证据**:反复出现的 `String → char[N] → 函数` 压扁模式
  (`src/file_server.cbs` 的 `pathbuf[2048]`、历史上的 `relbuf[1024]`、
  `src/main.cbs` 历史上的 `cbuf[4096]`),以及为扫描 String 不得不写的 `at_or_nul`
  桥接(`src/config.cbs`)。`String::as_str` 虽然返回内部缓冲区指针,但 String
  不保证 NUL 结尾,无法直接喂给 `open()`/`strstr` 这类按 NUL 终止的 C API。
- **影响**:这个模式不只是低效 —— 它**制造了真实的截断 bug**。本次优化把 `path_is_safe`
  从 `const char*` + `relbuf[1024]` 改成 `const String* _Borrow`,正是消除了一个安全漏洞:
  攻击者路径里若 `..` 出现在第 1023 字节之后,旧代码根本看不到。
- **建议**:提供 `String::c_str()`,返回 NUL 保证、生命周期绑定到 String 的
  `const char* _Borrow`(类比 Rust 的 `CString` / C++ 的 `std::string::c_str`)。
  一举消除整个"压扁进定长缓冲区"的模式及其截断风险。

### 3. 提供跨线程的安全共享所有权原语(`Arc<T>` 等价物)

> **上游状态:未见登记 —— 新提议。** 782 条 issue 的标题 + 正文里,`Arc<`、`线程安全`、
> `多线程`、`跨线程`、`thread-safe` **零命中**。`Rc<T>` 已存在但是单线程的
> ([IB2D9R](https://gitee.com/bisheng_c_language_dep/llvm-project/issues/IB2D9R)、IB53FV),
> 没有原子引用计数 / 跨线程共享所有权的对应物。

- **现象/证据**:Config/Router 跨工作线程只读共享(进程级生命周期)是本项目**唯一真正困难**
  的所有权问题,现在靠 `__move_to_raw` + `static` 上下文里的裸 `const T*`(永不释放)解决
  (见 `src/platform/runtime.cbs` 与 `/bsc-design` 中的 "Sharing long-lived read-only
  state with worker threads" 模式)。现有 `Rc<T>` 是单线程的,没有原子引用计数版本。
- **影响**:在最该有保证的地方,checker 让位给了裸指针 —— 最难的那部分其实没被检查。
- **建议**:加一个线程安全的共享所有权类型,让工作线程能持有"被检查的"长生命周期
  只读共享借用,而不是逃逸到裸指针。这恰恰是语言现在没能提供价值、却最该提供的地方。

---

## 第三梯队:降低采用税的人体工学 / 诊断

### 4. 针对 `_Owned T*` 写法给精准诊断

> **上游状态:未见登记 —— 新提议。** 没有 issue 涉及改进 `unknown type name '_Owned'` 这条诊断。
> [IA8CWH](https://gitee.com/bisheng_c_language_dep/llvm-project/issues/IA8CWH)(open,
> "从 fast qualifiers 中移除 owned, borrow")是限定符的**实现机制**(sizeof 不变),不是报错改进。

- **现象/证据**:限定符后置语法(`T *_Owned`)是已知陷阱 —— CLAUDE.md 把它列为 CRITICAL,
  因为写成 `_Owned T*` 会得到**误导性**的 `unknown type name '_Owned'` 报错。
- **影响**:新手最常见的第一个坑,报错信息把人引向错误方向。
- **建议**:解析器看到 `_Owned T*` / `_Borrow T*` 时,直接报
  `did you mean 'T *_Owned'?`。极低成本,极高收益。

### 5. 给 `String` 一个 `_Safe` 的字面量构造

> **上游状态:部分已登记(相邻 open issue)。**
> [IBFUB1](https://gitee.com/bisheng_c_language_dep/llvm-project/issues/IBFUB1)(open,
> 2025-01-06)正是这个痛点:`String::from` 只能在 `unsafe` 里用,且在安全区调用会段错误;
> [IDM1B2](https://gitee.com/bisheng_c_language_dep/llvm-project/issues/IDM1B2)(closed)
> 已支持安全区字面量 → `const char*`。本条是更具体的方案 —— 字面量编译期已知长度 ⇒ 可提供
> `_Safe` 构造,不必走 `String::from` 的 unsafe 路径。

- **现象/证据**:本次把 `Config::parse` 改成收 `String` 后,3 个测试文件
  (`tests/test_config.cbs`、`test_handler.cbs`、`test_file_server.cbs`)都被迫
  `String::new()` + `str_append_cstr` 手工搭字符串,因为 `String::from` 是 `_Unsafe`。
- **影响**:字符串字面量在编译期长度已知、静态 NUL 结尾,从字面量构造 String 本可以安全。
  现状逼得人为了省事把 API 设计成收 `const char*`,进而又回到压扁模式。
- **建议**:提供 `_Safe` 的字面量构造,让"收 `String` 的 API"在字面量调用点也好用。

### 6. 借用检查器错误信息带上 live-range 跨度

> **上游状态:大体已登记(两块都在活跃推进)。** 报错质量:`[errormsg]` 是反复出现的主题 ——
> [IDPHA4](https://gitee.com/bisheng_c_language_dep/llvm-project/issues/IDPHA4)(open)、
> IDQXFK、IHMFUN、IJMGPA、IAM8JD、IAXFJR 等一批。clangd/LSP 稳定性:
> [I7Q4OO](https://gitee.com/bisheng_c_language_dep/llvm-project/issues/I7Q4OO)(open)、
> ICQQUC(open,"clangd 单元测试有 4 个失败")、ICVKFA(open,"clangd 输入代码过程 crash")
> 等一批(多为 coredump/crash)。本条特有的"Rust 三段式 live-range 定位"无独立 issue,
> 但归属于既有的报错质量主题。

- **现象/证据**:存在 `bsc-errors` + `bsc-common-mistakes` 两个专门的 skill,
  本身说明报错不够自解释;`bsc-lsp` 提到 clangd 在 ownership flow 上的已知局限。
- **建议**:借鉴 Rust 的 "first borrow here / second borrow here / first borrow later
  used here" 三段式定位,指出冲突借用在哪还活着;并提升 LSP 对 move/borrow/live-range
  的可靠性 —— ownership 推理本来就是最难的部分。

---

## 第四梯队:标准库分配器(libcbs)

> 两条都在本会话用项目工具链实测过,且**修复就在手里**(见附录中的 `safe_calloc` 实现)。

### 7. 补一个 `safe_calloc<T>`(清零的任意类型堆分配)

- **现象/证据**:本 libcbs 的分配器只有 `safe_malloc` / `safe_malloc_array` / `safe_free` /
  `safe_free_array` / `safe_swap` —— **没有清零分配的对应物**(`grep` 全 libcbs 头零命中
  `safe_calloc`/`calloc`)。要"清零的任意类型"只能绕 `safe_malloc<T>((T){0})`,而那要求 `T` 能被
  `{0}` 聚合初始化(含 `_Nonnull _Owned` 字段的类型就不行)。
- **建议(已实现 + 实测)**:加一个 5 行的 `safe_calloc<T>`,仿 `safe_malloc` 但用 `calloc` 按字节清零、
  不收初值:

  ```c
  _Safe T *_Owned safe_calloc<T>(void) {
      _Unsafe {
          T *addr = (T *)calloc(1, sizeof(T));
          if (!addr) { bsc_bad_alloc_handler(sizeof(T)); }
          return __take_from_raw(addr);
      }
  }
  ```
  实测:漏释放被借用检查器抓到(`memory leak of value`);运行打印 `x=0 y=0`(确为清零)。

### 8. `safe_malloc<T>(T t)` 的按值入参是大类型的爆栈隐患

- **现象/证据**:`safe_malloc<T>` 按**值**接收初值,整个 `T` 会落到栈上。用
  `-Wframe-larger-than` 实测一个 1 MB 的 `T`:`SAFE_MALLOC` 的调用方栈帧约 **2 MB**、
  `safe_malloc<T>` 自身栈帧约 **1 MB**;而无值入参的 `safe_calloc<T>` 调用方约 **8 B**、自身约 **24 B**。
  即单次大类型 `safe_malloc` 就能在两层栈帧上压掉约 3 MB,几次或更大的 `T` 即爆默认 8 MB 栈。
- **影响**:这是个**潜伏的爆栈 footgun** —— 编译期不报错,运行期才崩;用户很难预料"分配一个堆对象"
  会把对象先放栈上。
- **建议**:为大类型提供**不经栈**的构造路径 —— 例如 `safe_malloc_uninit<T>()`(分配未初始化、
  返回 `T *_Owned`,由调用方就地填字段)或就地构造变体;并在文档里明确 `safe_malloc(T t)` 的按值
  代价。当前可用 `safe_calloc<T>()`(零栈)或裸 `malloc` + `_Unsafe` 里逐字段初始化规避。

---

## 优先级总结

| # | 建议 | 类别 | 上游状态(对照 782 条 issue) | 影响 |
|---|------|------|------|------|
| 1 | 修 `return f(&_Const local)` codegen UAF | 编译器健全性 bug | **重复**:[IJC66K](https://gitee.com/bisheng_c_language_dep/llvm-project/issues/IJC66K)(open) | 不修则核心卖点站不住 |
| 2 | `String::c_str()`(NUL 保证 + 生命周期绑定) | 标准库 | **新**(未见登记) | 消灭压扁模式 + 截断 bug |
| 3 | `Arc<T>` 等价物(线程安全共享所有权) | 语言/标准库 | **新**(Arc/线程 零命中) | 跨线程共享不必逃逸到裸指针 |
| 4 | `_Owned T*` 精准诊断 | 诊断 | **新**(未见登记) | 新手第一坑,零成本 |
| 5 | `_Safe` String 字面量构造 | 标准库 | **部分**:相邻 [IBFUB1](https://gitee.com/bisheng_c_language_dep/llvm-project/issues/IBFUB1)(open) | 减少 API 退回 `const char*` |
| 6 | 借用错误带 live-range / LSP 可靠性 | 诊断/工具 | **大体已登记**:errormsg + clangd 两簇 | 降低 ownership 推理门槛 |
| 7 | 补 `safe_calloc<T>`(清零任意类型) | 标准库 | **新**(libcbs 无,已实现 5 行) | 清零分配,免 `(T){0}` 限制 |
| 8 | `safe_malloc(T t)` 按值入参的大类型爆栈隐患 | 标准库 | **新**(已实测 1MB→~2MB+1MB 栈帧) | 修掉潜伏 footgun;大类型不经栈 |

**值得反馈的"新"提议:#2、#3、#4**(#3 最有分量 —— 跨线程安全共享是当前完全空白)。
#1 已有 IJC66K,只需补充我们的独立复现;#5 可在 IBFUB1 下追评;#6 归入既有 errormsg 主题。

前三条做掉,BSC 在这类项目上的价值判断会从"温和"明显往上走。

---

## 附录 A:所有权设计要点(值类型 `_Owned struct` vs `T *_Owned`)

> 这一节不是"改进建议",而是用 BSC 写**新项目**时的设计取舍记录,源于本项目实践。
> 与 `bsc-design` 技能的 Rule 3 同源,这里用本项目的具体例子收敛成可操作的判断。

### 核心认识:它们是同一套模型,不是两套

`_Owned struct`(值类型)和 `T *_Owned`(owned 指针)**不是两套追踪系统**,是同一套
所有权模型(move 语义 + 自动析构)作用在两种载体上。所以选择不是"值之外要不要再加指针追踪",
而是:**这份资源用值承载,还是用堆指针承载。**

- 值类型 `_Owned struct` 已经给你资源管理要的两样东西:① 自动析构(RAII),② move 语义的所有权流转。**不用手动 free。**
- `T *_Owned` 不增加追踪能力,只是额外带来值类型给不了的「堆分配 + 稳定地址 + 可空 + 无限大小递归」。

### 默认:值类型 `_Owned struct`,通常就够

本项目佐证:`Request`、`Response`、`Config`、`Router`、`Route`、`Header` 全是值类型
`_Owned struct`,全程 RAII、valgrind 零泄漏 —— 业务层一个 `*_Owned` 都没用。

### 必须用 `T *_Owned` 的四类(值语义在「形状」或「身份」上表达不了)

| 场景 | 为什么值类型不行 |
|------|------------------|
| **1. 自引用 / 递归的「拥有」结构**(链表、树、图) | `struct Node { Node next; }` 大小无限;递归边只能是指针。析构仍自动级联。 |
| **2. 开放集合 / 异构 / 运行时类型的多态拥有**(`Trait *_Owned`) | 值类型装不下编译期未知大小的实现者。**闭合小变体集**用 tagged struct 仍可值类型化。 |
| **3. 需稳定堆地址、且被外部长期持有**(交 C / 跨线程 / 回调注册) | 值的地址 move 后变、栈值出作用域即亡;外部裸引用要长期有效就得堆 + 固定地址。 |
| **4. 与 C 的 malloc/free 所有权互操作** | `*_Owned` 是唯一能给 C 堆块建模所有权的类型,经 `__take_from_raw`/`__move_to_raw` 过桥。 |

**本项目的唯一 `*_Owned` 命中第 3 类**:`runtime_serve(Config *_Owned, Router *_Owned)` ——
Config/Router 须 `safe_malloc` 拿固定堆地址才能 `__move_to_raw` 共享给所有 worker 线程。
> 推论:若改成单线程(epoll 事件循环),`main` 持有值类型 Config/Router 借用进 handler,
> **整个项目会一个 `*_Owned` 都没有。**

### 看似需要、其实不必须(有值类型替代)

| 看似要 `*_Owned` | 值类型替代 |
|------|-----------|
| 可空 / 可选的所有权 | `Option<T>` 包值类型 |
| 大对象省 move 开销 | 纯性能;含 String/Vec 的结构体顶层很小,move 只拷指针+长度 |
| 递归**数据结构**(非递归拥有) | arena:`Vec<T>` + 下标(放弃边上的所有权追踪) |
| 容器元素要稳定指针 | 存下标,用时 `get()` 重取 |

### 一条实践规则

即便确实要用 `*_Owned`,也尽量让它做某个值类型 `_Owned struct` 的**字段**,靠外层析构自动
级联释放(Rule 5),而不是裸着一个 `*_Owned` 让你记着 `safe_free`。把"必须手动管"的面积压到最小
—— libcbs 的 `String { Vec<char> vec; }` 正是这么做的:`*_Owned` 埋在底层,用户全程只见值类型。

### 归纳

新项目以值类型 `_Owned struct` 为默认,资源跟踪基本够用;`*_Owned` 只在上述四类(递归拥有、
开放多态、稳定外部身份、C 堆互操作)出现。其中 1/2 属**数据建模**,3/4 几乎只出现在**边界**
(FFI、线程、C 内存)。`*_Owned` 用得越少,所有权推理越简单(更多纯 RAII、更少显式 free)。

---

## 附录 B:存量代码迁移策略(所有权标注优先 vs 重写成高级类型)

> 面向"已有、含大量堆分配的 C 代码库做安全增强"的策略选择。与 `c-to-bsc` 技能同源
> ——该技能第一原则:**"keep the file structure as-is, only ADD ownership annotations,
> change as little as you can"**(连 `.c`/`.h` 扩展名都不改,用 `clang -x bsc file.c` 编译)。

### 结论

**默认走「堆内存所有权标注」(原地加 `_Owned`/`_Borrow`);「改造成高级数据结构」是局部、
后置、外科手术式的动作,不是迁移策略本身。**

### 为什么存量代码默认是标注

1. **风险 / diff 面**:标注只加限定符,**行为不变**(仍能当纯 C 编)。重写成 `Vec`/`String`
   会动数据表示、调用点、内存布局、性能 —— 大库上风险面巨大,且无法"重写一半"。
2. **可增量 + 双编译**:同一份源码**既能当 C 编、又能当 BSC 编**,逐文件迁、测试常绿、
   review 聚焦所有权。重写是"每个结构全有或全无"。
3. **安全收益来自借用检查器,标注就已拿到**:`*_Owned`/`*_Borrow` 一标就买到 UAF /
   double-free / 泄漏检查 —— 不需要 `Vec`/`String` 才有安全。
4. **测试契约**:标注保行为不变 → 既有测试仍有意义;重写打破"行为不变"前提,要重新验证。

### 高级类型重写只用在这几处(边界清晰、一次一个)

- **新代码 / 叶子模块** —— greenfield,直接用 `Vec`/`String`/值类型。
- **C 在"手搓烂 Vec/String"的地方** —— 手动 `realloc`+len+cap 的动态数组、手动字符串缓冲
  (即 #2 那种 flatten+截断 bug)。换 `Vec<T>`/`String` 消掉整类手工记账 bug,但要藏在稳定接口后。
- **递归 / 共享所有权** —— Pattern A(裸指针 + `_Unsafe` + 自定义析构,stdlib `LinkedList` 即此);
  真共享才上 `Rc`/`Weak`。注意这里技能反而**保留裸指针**,不强求高级容器。

### 顺序:先标注,后(选择性)重写

先把全库纳入借用检查器(原地标注);标注过程会**暴露所有权最烂的热点**(成堆 `_Unsafe`、
条件所有权、flag 决定 free)—— 那些才是后续局部重写成高级类型的候选。**反过来先重写,是在
"改写程序"而非"加固程序"**,把巨大成本前置到任何安全收益之前。

### 决策速查

| C 里的形态 | 建议 |
|------------|------|
| 单个 struct 的 `malloc`/`free` | 原地标 `*_Owned`(最省) |
| struct 有干净的 `free_X()` | 提升为 `_Owned struct` + 析构(仍以标注为主) |
| 手搓动态数组 / 增长缓冲(`realloc`) | 局部重写 `Vec<T>`/`String`(有界) |
| 递归 / 图 / 回指针 | 裸指针 + `_Unsafe` + 自定义析构;真共享才 `Rc`/`Weak` |
| 自引用必须拥有的边 | 必须 `*_Owned`(见附录 A 四类之一) |

### 一句话

**高级类型解决「手工记账」,所有权标注解决「谁负责释放」;存量加固要的主要是后者。**
即便已用高级类型,到 C/syscall 边界照样要 flatten(见 #2)—— 换容器并不能消除 FFI 边界拷贝,
而单纯 `*_Owned` 标注就足以拿到安全。

---

## 附录 C:worked example —— OpenSSL/TLS 配对资源(ownership 价值最强的证明场景)

> **说明:TLS 本身尚未接入(具体 OpenSSL 封装为设想);但其核心机制 —— 用「重声明」给外部 C
> 配对接口贴所有权 —— 已用 libc `fopen`/`fclose` 实测验证(见下"机制验证")。** 用来说明 ownership
> 的价值在哪类场景最可证明:本项目当前负载(请求作用域的 `String`/`Vec`)是 ownership 的"简单题",
> 属**温和**展示;而外部配对资源(OpenSSL)是手工 C 管理最难、RAII 收益最大的地方,会把价值判断
> 从"温和"明显提上来。

### 为什么是最强场景

OpenSSL 是**外部的、配对申请/释放**的 API(`SSL_CTX_new`/`SSL_CTX_free`、`SSL_new`/`SSL_free`、
`BIO_new`/`BIO_free`…)。libcbs 的 `String`/`Vec` 析构是"白送"的;OpenSSL 的析构是**你自己写**、
再由编译器在每条路径上强制配对 —— 这才是所有权系统对一个它一无所知的资源真正在干活。而泄漏 /
double-free / 错误路径漏 free,正是 C TLS 代码的 bug 高发区(也是 CVE 高发区)。

### 首选做法:重声明(给外部 C 接口贴所有权,无需 wrapper)

BSC 支持 mixed-mode 重声明 —— 给一个 C 函数补一个加了所有权 / `_Safe` 的声明(上游 IDP08L)。
对**指针型**外部资源(`FILE*`、`SSL_CTX*`、`SSL*`…),直接重声明 acquire 返回 `T *_Owned`、
release 接收 `T *_Owned`(consume)即可 —— **不需要 wrapper struct,调用点还变 `_Safe`**:

```c
// OpenSSL(设想):
_Safe SSL_CTX *_Owned _Nullable SSL_CTX_new(const SSL_METHOD *_Borrow method);
_Safe void                      SSL_CTX_free(SSL_CTX *_Owned ctx);   // consume = 释放
_Safe SSL     *_Owned _Nullable SSL_new(SSL_CTX *_Borrow ctx);
_Safe void                      SSL_free(SSL *_Owned ssl);
```

之后 `SSL *_Owned _Nullable s = SSL_new(ctx);` 的所有权由编译器**直接在那个裸指针上**跟踪:
漏 `SSL_free` → 编译期 leak;`SSL_free` 后再用 → 编译期 moved error。无 wrapper、无 `goto err` 梯。

#### 机制验证(已实测 —— 用 libc `fopen`/`fclose` 代替尚未接入的 OpenSSL)

重声明对**已在系统头文件里声明过**的 libc 函数同样成立。项目编译器实测三例:

```c
_Safe FILE *_Owned _Nullable fopen(const char *_Nonnull, const char *_Nonnull);
_Safe int                    fclose(FILE *_Owned stream);   // consume = 释放
```

| 用例 | 结果 |
|------|------|
| 正常 `fopen` → 判空 → `fclose(f)` | ✅ 编译干净,**调用点 `_Safe`(无 `_Unsafe`)** |
| 漏掉 `fclose` | ❌ `error: memory leak of value: 'f'` |
| `fclose(f)` 后再用 `f` | ❌ `error: use of moved value: 'f'` |

→ 仅靠声明就把 泄漏 / double-free / use-after-free 全移到**编译期**,**零 wrapper、零 `_Unsafe`
调用点**。`fs_read_file` 现有的 `_Unsafe { fopen … fclose }` 里这部分即可去掉。

### 何时仍需 wrapper(`_Owned struct` + 析构)

1. **非指针资源**:`int` fd(net.cbs 的 `accept`/`close`)。`_Owned` 是指针限定符,`int` 带不了,
   重声明无能为力 —— 需 `_Owned struct Fd { int fd; ~Fd(){…close…} }`。
2. **多步 / 带逻辑的拆卸**:如 `SSL_shutdown` 再 `SSL_free`、或释放多个子资源 —— 单个 consume 重声明
   装不下,用析构装拆卸逻辑。
3. **想"自动每路径释放"而不写 free 调用**:重声明强制你**显式**调 free(漏则报错);wrapper 析构
   则**自动**触发。多 early-return 的函数(如 `runtime_handle_conn`)用 wrapper 更省心。

### 能证明什么(精确)

> **"配对 free 在每条控制流路径上恰好调用一次,且释放后句柄不可再用 —— 编译期(UAF)+ 结构性
> (RAII)保证,不靠手写 goto 清理纪律。"**

对比经典 C 错误处理梯(漏 free / 错序 / 某分支忘 free)塌缩成作用域析构。这一类(次数/时机错)
正是 ownership 消灭的。

### 一个特别漂亮的点:`_Owned`/`_Borrow` ↔ OpenSSL set0/set1(同样靠重声明,无需 wrapper)

OpenSSL 的所有权契约现在只写在文档里、且常被搞错:`*_set0_*` = 接管所有权,`*_set1_*` = 借用/加引用。
重声明能把它直接编码进类型:

```c
_Safe int SSL_CTX_use_PrivateKey      (SSL_CTX *_Borrow ctx, EVP_PKEY *_Owned key);        // set0 风格:消费 key
_Safe int SSL_CTX_use_PrivateKey_borrow(SSL_CTX *_Borrow ctx, const EVP_PKEY *_Borrow key); // set1 风格:借用 key
```

**把"只存在于文档里的所有权约定"变成"编译器检查的类型"** —— C 永远给不了的增量价值。

### 诚实的边界

1. **保证的是"次数 / 时机",不是"语义正确"。** ownership 保证 free「每条路径恰好一次、之后不可用」,
   **不保证**那个 C 函数内部真的释放了对的东西 —— 那是你重声明里 `_Owned` 承诺的,编译器信你这条声明。
   好在 bug 绝大多数属前者(次数/时机)。
2. **OpenSSL 内部引用计数 BSC 看不见。** 必须让 BSC 单一所有者 **1:1 镜像** OpenSSL 文档化契约;镜像错了拦不住。
3. **缝从"调用点的 `_Unsafe`"变成"重声明这条断言"。** 重声明后调用点是 `_Safe`、不再有 `_Unsafe` 块;
   但你在**断言**"`fclose`/`SSL_free` 确按 `_Owned` 契约消费/释放"。这和 wrapper 里 `_Unsafe { fclose }`
   是同一道信任边界,只是更轻、更集中(集中在头文件的几行声明里)。

### 结论

**这是本项目里 ownership 价值最可证明的一处** —— 正因为它是外部配对资源。精确的价值主张:
**仅靠重声明就把配对释放从"靠程序员每条路径记得对"变成"类型系统编译期保证",外加把 set0/set1
所有权契约从文档提升为编译期检查,且零 wrapper、零调用点 `_Unsafe`。** 核心机制已用 libc
`fopen`/`fclose` 实测坐实(上表);接入 TLS 后照此重声明 OpenSSL 接口,再用 valgrind(零泄漏/
零 double-free)端到端验证,即可把"BSC 是否值得用"这个判断坐实 —— 比当前请求作用域的设计更能打。

---

## 附录 D:存量 C 项目安全增强 · 端到端 playbook

> 把附录 A(新项目设计)、B(标注 vs 高级类型)、C(外部资源重声明)+ `c-to-bsc` 流程收口成一份
> 可执行的迁移流程。

### 定调

**渐进式加固,不是重写。** 全程让代码**既能当 C 编、又能当 BSC 编**(同源、双编译器),
按 **边界 → 内部 → 选择性升级** 推进,每步过测试、valgrind 兜底,任何时刻都有可回退的工作基线。

### 贯穿全程的原则

1. **改得越少越好** —— 保留 `.c`/`.h`,只**加**标注(`clang -x bsc` 编),不动平台代码。
2. **`_Safe` 默认,`_Unsafe` 由编译器驱动** —— 只包"编译器逼出来的那一行",`_Unsafe` 会传染,务必收窄。
3. **值类型 `_Owned struct` 为默认**;`*_Owned` 只在四类用(见附录 A)。
4. **缝放边界**(FFI / 线程 / syscall),绝不放业务逻辑中间。
5. **测试是契约** —— 标注保行为不变;行为变了说明所有权标错了。

### 分阶段(每阶段一个验证门)

| 阶段 | 做什么 | 验证门 |
|------|--------|--------|
| **0 立基线** | 零标注下让 BSC 编过(BSC 是 C 超集,通常只修个别构造);固定 verify 命令、配双编译 | BSC 能编 + 既有测试全过(行为 = C) |
| **1 边界优先(最高 ROI)** | 外部配对资源贴所有权:`malloc/free`→`safe_malloc/safe_free`;`fopen/fclose`、OpenSSL 等**指针型**→**重声明**(附录 C,无 wrapper、调用点 `_Safe`);`int` fd 等非指针→`_Owned struct` wrapper | 边界模块编过 + 测试过 + valgrind 该资源零泄漏 |
| **2 自底向上标注内部**(主体) | 逐文件:每个签名里每个指针标 `_Owned`/`_Borrow`/带理由 raw;有 `free_X()` 的 struct→`_Owned struct`+析构;`&`→`&_Const`/`&_Mut`;只读 getter 加 `const`。**叶子模块先做,每文件编完即测,不攒批** | 逐文件编过 + 测试过;每处 `_Unsafe` 都对应一个编译器诊断 |
| **3 选择性升级高级类型**(最后、有界) | **只**在 C 手搓容器处(手动 `realloc` 数组、手动字符串缓冲)换 `Vec`/`String`,藏接口后、一次一个(附录 B)。不全面重写 | 接口不变 + 测试过 + valgrind 干净 |
| **4 验证与守护**(持续) | **必须跑 valgrind**(因 IJC66K 仍 open,借用检查器过 ≠ 内存安全);加分层守护(`check_no_unsafe.sh` 式 CI:核心零 `_Unsafe`);保留双编译以 diff 行为 | valgrind 干净 + 分层 CI 绿 |

### 关键现实(坦诚)

- **不可消除的 `_Unsafe`**:FFI / 线程 / syscall 边界一定有,目标是收进适配器、最小化,不是清零。
- **跨线程共享最难**:无 `Arc`,长生命周期只读共享走"`__move_to_raw` + 进程级 + 优雅退出收回"(issue #3),或单线程化让问题消失。
- **valgrind 不能省**(见阶段 4)。
- **收益集中在资源生命周期**(泄漏 / UAF / double-free,C 最痛处);纯计算/算术密集代码 BSC 帮助有限。

### 现成参照

**`cc_httpd` 本身就是"这套迁移做对了的终态"**:业务核心零 `_Unsafe`、`_Unsafe` 全在
`src/platform/` 适配器、业务类型全是值类型 `_Owned struct`、`*_Owned` 只在跨线程边界出现一次、
外部资源(FILE*/fd)正是阶段 1 重声明/wrapper 的下一批候选。拿它当模板比抽象描述更直观。

---

## 附录 E:关于 BSC 出处/归属的说明(勘误)

> 讨论中我曾把 BSC 称作"华为的",并进一步说它瞄准"鸿蒙 / 欧拉 / 电信固件"、动机是"自主可控"。
> 这里如实区分**可核实** / **推断** / **应收回**,避免把猜测当事实。

**本环境可核实的(直接证据):**
- 名称 **"BiSheng C / 毕昇 C"**;工具链是 LLVM/Clang 15.0.4 的 fork。
- 仓库 `gitee.com/bisheng_c_language_dep/llvm-project`;issue 为中文;有标准库 `libcbs`。
- 在 `install/` 树、libcbs 头文件、两个 README 里**搜不到任何 "Huawei"/"华为" 字样**,头文件也无版权署名。

**推断(不是本环境证明的):**
- **"毕昇/BiSheng" 是华为编译器工具链的公开品牌**(华为"毕昇编译器",基于 LLVM,用于鲲鹏/openEuler)
  —— 据此推断 BSC 很可能同源于华为。这是**从品牌名做的推断**,辅以 Gitee + 中文生态信号;
  不能据眼前产物证明"BiSheng C 这个具体项目"就归华为(也不能排除是相关但独立的研究/社区项目)。

**应收回(此前过度断言的猜测):**
- "瞄准鸿蒙 / 欧拉 / 电信固件" 与 "自主可控动机" —— 手里**没有任何证据**支撑这些具体战场/动机,
  当作"*若*确系华为系,*则*可能……"的猜想,不作结论。

**为何不影响本文档结论:** 本文的技术论断与"杀手级应用 = 不重写地给存量 C 装借用检查器"的判断,
立足于实测的技术能力(C 超集、双编译、重声明),**与出资方/目标代码库无关**;归属即便有误,论证不变。
