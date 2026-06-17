# cc_httpd —— BiSheng C HTTP 服务器 —— 设计规范（仅所有权版）

**日期：** 2026-05-25（在 2026-05-23 初版基础上改造为「仅所有权」）
**状态：** 架构已批准；待重写实现计划

## 1. 概述

一个用 BiSheng C（BSC）编写的轻量级 HTTP/1.1 服务器，提供静态文件与简单动态路由，
通过 BSC 的**所有权系统**实现内存安全，使用 pthread 线程池实现并发。

这是同一产品规范的 Claude Code 实现（`cc_httpd`），与 Trae 工具实现的姊妹项目 `httpx`
形成**公平的工具对比**：cc_httpd **仅依据 PRD 需求**独立实现——Claude 不阅读 httpx 的
`.cbs`/`.hbs` 源码。（共享的需求产物——PRD、Makefile 约定、示例 HTML——可以参考；实现源码不可。）

### 1.1 「仅所有权」约束（本版本的核心约束）

本变体刻意将语言特性收敛到 **BSC 的所有权系统本身**，用以考察在没有标准库便利设施时
所有权原语的表达力。

- **禁用：** libcbs 容器类型（`String`、`Vec`、`HashMap`、`Rc`、`Option`、`Result` 等，
  即不 `#include "string.hbs"` / `"vec.hbs"` 等）、成员函数（`Type::method(...)`、
  `obj.method()` 点语法）、trait（`_Trait`/`_Impl`）、以及为本项目自定义类型引入的泛型。
- **保留（这些属于所有权系统，不算「库类型」）：** `_Owned` / `_Borrow` / `_Safe` /
  `_Unsafe`、析构函数与 RAII、移动语义、可空性（`_Nullable`/`_Nonnull`），以及
  `bishengc_safety.hbs` 提供的分配器 `safe_malloc` / `safe_free` /
  `safe_malloc_array` / `safe_free_array` / `safe_swap`。
- 现有设计本就未使用 trait，因此「禁用 trait」仅是「不引入」约束。

## 2. 目标

- 完整的 HTTP/1.1 请求解析（方法、路径、版本、首部、消息体）。
- GET 静态文件服务，含正确的 MIME 类型与路径穿越防护。
- 简单的路由注册 + 处理器回调。
- 标准状态码：200、400、404、500。
- 通过固定 pthread 线程池实现并发。
- 通过 BSC 所有权/RAII 实现内存安全；`_Unsafe` 暴露面最小化。
- 通过 `config.ini` 配置端口与文档根目录。

## 3. 非目标

HTTPS/TLS、HTTP/2、异步/协程 IO、超出基础处理的 keep-alive 流水线、CGI/FastCGI、
负载均衡、目录列举。**额外（本版本）：** 不使用 libcbs 容器、不使用成员函数、不使用
trait、不为自定义类型使用泛型。

## 4. 架构

### 4.1 构建模型

**单一翻译单元（single TU）。** 每个关注点是一对 `.hbs`（声明）+ `.cbs`（定义）。
`src/main.cbs` 按依赖顺序 `#include` 各 `.cbs` 并作为一个单元编译：

```
/home/zly/bsc/llvm-project/build/bin/clang -Wall -Wextra -Wno-nullability-completeness -g \
    -I/home/zly/bsc/llvm-project/install/include/libcbs \
    src/main.cbs -o bin/httpd \
    -L/home/zly/bsc/llvm-project/install/lib -lstdcbs -lpthread
```

**工具链要点（相对初版仅一处变化）：** `bishengc_safety.hbs` 位于 `libcbs/` 头文件目录，
其分配器实现在 `libstdcbs`（不是宏）。因此 **include 路径与链接行不变**——仅停止
`#include "string.hbs"`/`"vec.hbs"`，改为各文件按需 `#include "bishengc_safety.hbs"`。
头文件 include guard 防止重复声明。

理由：BSC 跨 TU 的泛型实例化与 `Type::method` 定义较脆弱；单 TU 是经验证的低风险模型。

### 4.2 分层（函数式内核 / 命令式外壳）

按项目约束：**业务代码零 `_Unsafe`；所有 `_Unsafe` 都在适配层，封装在 `_Safe` 接口之后。**
一个含 `_Unsafe { }` 块的 `_Safe` 函数仍是 `_Safe`、可被业务代码调用（块是局部逃逸；
把整个函数标 `_Unsafe` 会传染给每个调用者）。

**业务内核（`src/*.cbs`）—— 100% `_Safe`，无 `_Unsafe`：**

| 模块 | 职责 | 关键类型 / 所有权 |
|---|---|---|
| `str_util` | 定长 `char[]` 缓冲操作（拷贝/比较/前缀）+ 构建 owned 缓冲的辅助 | `_Safe` 自由函数；用下标实现，无 libc |
| `mime` | 文件扩展名 → Content-Type（纯 `_Safe` 字符匹配，无 libc、无 `_Unsafe`） | `_Safe`，返回 `const char*` 字面量 |
| `http_request` | 从原始字节解析方法/路径/版本/首部/体 | `_Owned struct Request`；`request_parse(const char* _Nonnull raw)` |
| `http_response` | 构建状态/首部/体；`ok`/`not_found`/`bad_request`/`server_error`；序列化 | `_Owned struct Response`；`response_serialize(...)` → owned 报文缓冲 |
| `config` | 解析 `config.ini` 文本（`key=value`）→ 端口/线程/文档根；默认值 | 纯 `struct Config`；`config_parse(text)`、`config_default()`（无文件 IO） |
| `router` | 注册 `(method, path) → handler`；精确匹配；`path_is_safe` 穿越防护 | 纯 `struct Router { Route routes[MAX_ROUTES]; size_t n; }` |
| `file_server` | 在文档根下解析路径、穿越防护、经 `fs` 适配器读取 → `Response` | `_Safe serve_static(cfg, req)` |
| `handler` | 纯 `handle_request(cfg, router, raw_bytes) → Response`——函数式内核 | `_Safe`，无 socket，可单测 |

**适配层（`src/platform/*.cbs`）—— `_Safe` 接口，内部最小 `_Unsafe`：**

| 模块 | 职责 | 接缝 |
|---|---|---|
| `net` | `_Safe` socket 监听/接受；recv → 定长缓冲；send 按长度发送；关闭 | sockets/syscalls |
| `fs` | `_Safe` 读整个文件 → owned `char *_Owned _ArrayElem` + 长度 | `fopen`/`fread` |
| `log` | `_Safe` 请求/诊断日志 | `fprintf` |
| `thread_pool` | `_Safe` 启动/提交/关闭；固定 N 个 worker；mutex+condvar 的客户端 **fd** 队列 | `pthread`；worker 是适配层内部 C-ABI |
| `runtime` | 命令式外壳：构建 `ServerCtx`、accept 循环、recv→`handle_request`→send、启动线程池 | 跨线程的原始 `ServerCtx*`/`void*` |
| `main` | 经 `fs` 读配置文本、解析、注册示例路由、运行 `runtime` | 入口；`#include` 所有 `.cbs` |

强制约束：`tests/check_no_unsafe.sh` grep 业务 `src/*.cbs`，若出现任何 `_Unsafe` token
则构建失败（适配目录 `src/platform/` 豁免）。

### 4.3 字符串与集合的表示（核心决策）

没有 libcbs 的 `String`/`Vec`，用如下两类原语：

| 数据 | 表示 | 原因 |
|---|---|---|
| **有界 token**——method、path、version、首部名/值、状态文本、content-type、文档根 | 定长 `char[N]`，NUL 结尾，纯数据 | 首部/路由存于定长数组，而 BSC **禁止数组元素含 `_Owned` 成员**；故其字符串字段必须是定长缓冲。在 `_Safe` 中可自由下标访问。 |
| **无界负载**——请求体、文件内容、响应报文 | `char *_Owned _ArrayElem buf` + `size_t len` | 堆上拥有、`_Safe` 中可下标、析构里 `safe_free_array` 释放。所有权/RAII 在此发挥作用。采用「先算长度→一次 `safe_malloc_array`→填充」构建。 |

**经编译器实测确认的关键安全区事实：**

1. `_Safe` 中对原始 `char*` 的**下标 `p[i]`（读与写）合法**；仅 `*p` / `p->field` 解引用被禁止。
   故有界缓冲区在业务内核中无需 `_Unsafe`。
2. 借用穿透访问 `r->method[i]`、`r->headers[k].name[i]` 在 `_Safe` 中合法。
3. `char *_Owned _ArrayElem` 作为 `_Owned struct` 字段、析构中 `safe_free_array`
   释放——整套通过编译并 valgrind 零泄漏。
4. `safe_free(void* _Nullable _Owned)` 接受空指针；`safe_free_array<T>` 接受 `T* _Owned _ArrayElem`。
5. **裸 `_Owned` / `_Owned _ArrayElem` 局部变量不会自动析构**——必须显式 `safe_free*` / move /
   return；只有 `_Owned struct` 才自动调析构。
6. `_Owned struct` 的 owned 字段**构造后不可重新赋值**；用 `safe_swap(&_Mut field, &_Mut tmp)`，
   并显式释放换出到 `tmp` 的旧值。
7. **借用一个 owned 缓冲的内容**：对元素取址 `&_Mut buf[0]` / `&_Const buf[0]` 得到
   `char *_Borrow _ArrayElem`（支持 `[]`），可传给取 `_Borrow _ArrayElem` 形参的辅助函数；
   **不可**直接传 owned 指针或 `&_Mut buf`（前者 owned→borrow 被禁，后者取到指针变量地址）。
8. **把 owned 指针本身传给非借用形参（含可变参数 `printf`）= 移动**，之后不能再 `safe_free*`；
   读取时改传借用 `&_Const buf[0]`，不消费所有权。

### 4.4 组件接口（草图）

```c
// ---- 公共类型 ----
typedef Response (*Handler)(const Request* _Borrow req);

struct Header { char name[64]; char value[256]; };          // 纯数据 → 可放入数组
struct Route  { char method[8]; char path[256]; Handler handler; };
struct Config { int port; int threads; char document_root[256]; };  // 纯结构（无 owned）
struct Router { struct Route routes[MAX_ROUTES]; size_t n_routes; }; // 纯结构（无 owned）

// ServerCtx：交给每个 worker 的只读共享上下文。因 Config/Router 无 owned 数据，
// **按值持有**；存于 static 变量，worker 经原始指针重建借用（见第 5 节）。
struct ServerCtx { struct Config config; struct Router router; };

// ---- str_util.hbs（业务）----
_Safe size_t cstr_len(const char* _Nonnull s);
_Safe void   cbuf_copy(char* _Nonnull dst, size_t cap, const char* _Nonnull src);   // 拷贝并 NUL 结尾
_Safe _Bool  cbuf_eq(const char* _Nonnull a, const char* _Nonnull b);
_Safe _Bool  cbuf_starts_with(const char* _Nonnull s, const char* _Nonnull prefix);
// 复制 [s, s+len) 到新的 owned 缓冲（用于把局部数据移交所有权）
_Safe char *_Owned _ArrayElem cbuf_dup(const char* _Nonnull s, size_t len);

// ---- mime.hbs（业务，纯 _Safe）----
_Safe const char* mime_for_path(const char* _Nonnull path);

// ---- http_request.hbs ----
_Owned struct Request {
    char method[8];                 // "GET"
    char path[1024];                // "/index.html"
    char version[16];               // "HTTP/1.1"
    struct Header headers[MAX_HEADERS];
    size_t n_headers;
    char *_Owned _ArrayElem _Nullable body;   // 可空：无体时为 nullptr
    size_t body_len;
    _Bool ok;
    ~Request(Request this) { if (this.body != nullptr) { safe_free_array(this.body); } }
};
_Safe Request request_parse(const char* _Nonnull raw);
// 大小写敏感首部查找；返回指向 r 内部值缓冲的指针，nullptr 表示缺失（借用绑定 r）
_Safe const char* _Nullable request_get_header(const Request* _Borrow r, const char* _Nonnull name);

// ---- http_response.hbs ----
_Owned struct Response {
    int status;
    const char* status_text;        // 字面量，如 "OK"
    const char* content_type;       // 字面量，如 "text/html"
    char *_Owned _ArrayElem _Nullable body;
    size_t body_len;
    ~Response(Response this) { if (this.body != nullptr) { safe_free_array(this.body); } }
};
_Safe Response response_make(int status, const char* _Nonnull status_text, const char* _Nonnull content_type);
// 以 C 字符串构建体（内部复制到 owned 缓冲）
_Safe Response response_with_cstr(int status, const char* _Nonnull status_text,
                                  const char* _Nonnull content_type, const char* _Nonnull body);
// 消费一个已拥有的体缓冲
_Safe Response response_with_owned(int status, const char* _Nonnull status_text,
                                   const char* _Nonnull content_type,
                                   char *_Owned _ArrayElem body, size_t body_len);
_Safe Response response_not_found(void);
_Safe Response response_bad_request(void);
_Safe Response response_server_error(void);
// 序列化完整报文（状态行+首部+CRLF+体）到 owned 缓冲；长度经 out_len 返回
_Safe char *_Owned _ArrayElem response_serialize(const Response* _Borrow r, size_t* _Borrow out_len);

// ---- router.hbs ----
_Safe struct Router router_new(void);
_Safe void   router_add(struct Router* _Borrow r, const char* _Nonnull method,
                        const char* _Nonnull path, Handler h);
_Safe Handler _Nullable router_match(const struct Router* _Borrow r, const Request* _Borrow req);
_Safe _Bool  path_is_safe(const char* _Nonnull path);     // 拒绝含 ".." 的路径

// ---- config.hbs（业务——仅解析；文件文本由 runtime 经 fs 适配器读取）----
_Safe struct Config config_default(void);
_Safe struct Config config_parse(const char* _Nonnull text);

// ---- handler.hbs（纯函数式内核，_Safe，无 socket）----
_Safe Response handle_request(const struct Config* _Borrow cfg, const struct Router* _Borrow router,
                              const char* _Nonnull raw);

// ---- platform/net.hbs（适配器：_Safe 接口，内部 _Unsafe；fd 为 int）----
_Safe int     net_listen(int port, int backlog);                 // 监听 fd，失败 -1
_Safe int     net_accept(int listen_fd);                         // 客户端 fd，失败 -1
_Safe ssize_t net_recv(int fd, char* _Nonnull buf, size_t cap);  // 收入定长缓冲并 NUL 结尾
_Safe int     net_send_all(int fd, const char* _Nonnull buf, size_t len);  // 按长度全发
_Safe void    net_close(int fd);

// ---- platform/fs.hbs（适配器）----
// 读整个文件到 owned 缓冲；失败返回 nullptr。
_Safe char *_Owned _ArrayElem _Nullable fs_read_file(const char* _Nonnull path, size_t* _Borrow out_len);

// ---- platform/thread_pool.hbs（适配器）----
typedef void (*ConnHandler)(int client_fd, void* _Nonnull ctx);
_Safe int  thread_pool_start(struct ThreadPool* _Nonnull tp, int n, ConnHandler h, void* _Nonnull ctx);
_Safe void thread_pool_submit(struct ThreadPool* _Nonnull tp, int client_fd);
_Safe void thread_pool_shutdown(struct ThreadPool* _Nonnull tp);
```

（确切签名在实现期定稿；此处为形状。）

## 5. 所有权设计

- **`Request` / `Response` 是 `_Owned struct`**，仅 `body` 字段为 `char *_Owned _ArrayElem`
  （可空），析构中以 `safe_free_array` 释放；其余字段是定长 `char[]` 纯数据。请求处理路径
  对体缓冲**不做手动释放**，由 RAII 在作用域结束时回收，零泄漏。
- **`Config` / `Router` 是纯 `struct`（无 owned 成员）**——`document_root`、`Route.method/path`
  皆为定长缓冲，`Route.handler` 是函数指针。它们可按值复制、无析构。
- **有界字符串一律定长 `char[N]`**：这是 BSC 的硬性要求（数组元素不得含 `_Owned`），故
  `Header`/`Route` 中的字符串必须如此；为一致性，请求行字段（method/path/version）亦用定长缓冲。
- **无界负载用 owned 缓冲构建一次**：响应序列化先遍历计算总长度，`safe_malloc_array` 一次分配，
  再用游标逐字节写入，返回 `char *_Owned _ArrayElem` + 长度。
- **跨线程仅 `int` fd 穿过任务队列。** 不在线程间共享任何堆上拥有数据，故无跨线程所有权转移。
- **Router 与 Config 在启动时构建一次，随后只读。** 因二者是无 owned 数据的纯结构，跨线程方案
  简化为一个 `static struct ServerCtx { struct Config config; struct Router router; }`，启动时填好，
  以 `const struct ServerCtx*` 交给 worker——**无需** `safe_malloc`/`__move_to_raw`。
- **Handler 回调** 取 `const Request* _Borrow`，按值返回 `_Owned Response`（规则 1：返回拥有、参数借用）。

## 6. `_Unsafe` 边界

**所有 `_Unsafe` 都在 `src/platform/`（适配层）；业务 `src/*.cbs` 无 `_Unsafe`。** 各适配函数
声明为 `_Safe`，把最少的不安全行包进 `_Unsafe { }`，使业务调用方保持 `_Safe`。接缝：

1. **`net`** —— `socket`、`bind`、`listen`、`accept`、`recv`、`send`、`close`。recv 拷入定长缓冲；
   send 按长度发送。
2. **`fs`** —— `fopen`/`fread`/`fclose`；以 owned `char *_Owned _ArrayElem` 返回文件内容。
3. **`log`** —— `fprintf` 到 stderr。
4. **`thread_pool` / `runtime`** —— `pthread_*`；C-ABI worker；以及跨线程的原始
   `ServerCtx*`/`void*`。其最终调用的业务 `handle_request` 是 `_Safe`。

解析、路由、响应构建、MIME 查找、配置解析、文件服务逻辑与 `handle_request` 内核全部
`_Safe` 且**无 `_Unsafe`**。

**发送 owned 缓冲的注意点：** `_Borrow`/`_Owned` 指针**不能**转成原始指针交给 `send()`
（BSC 全局禁止 `_Borrow → raw`）。因此在适配层 `handle_conn` 中，用下标 `wire[i]` 从 owned
报文缓冲逐段拷入局部栈数组（局部数组 decay 为 raw `char*` 合法），再 `net_send_all(stack, m)`。

## 7. 错误处理

- 系统调用失败 → 记录到 stderr，返回 `-1` / 关闭连接；accept 循环继续。
- 畸形请求 → `400 Bad Request`。
- 文件未找到 / 不在文档根下 → `404 Not Found`。
- 路径穿越尝试（`..` 逃出文档根）→ `404`（视为未找到；绝不服务根外文件）。
- 处理器或文件读取错误 → `500 Internal Server Error`。
- 无异常；使用 C 风格状态 int 与 `_Nullable` 指针。

## 8. 测试与验证

| AC | 测试 |
|---|---|
| AC-1 服务器启动 | 绑定配置端口；`curl` 可连接 |
| AC-2 静态 200 | `curl -s localhost:8080/` 返回 `index.html` 体、200、正确 Content-Type |
| AC-3 404 | `curl -i localhost:8080/nope` 返回 `404` |
| AC-4 动态路由 | `curl -s localhost:8080/hello` 返回处理器自定义体 |
| AC-5 并发 | 并行 `curl`（或 `ab -c 10 -n 100`）全部成功 |
| AC-6 内存安全 | 可选 `valgrind` 在一批请求上干净运行 |

- 开发期单文件语法检查：`clang -fsyntax-only -x bsc <file>`（带正确 include 路径与
  `bishengc_safety.hbs`）。
- `tests/run_integration.sh` 编排：构建 → 启动服务器 → 运行 curl 断言 → 停止服务器。
- 示例资产：`www/index.html`、`config.ini`（`port=8080`、`document_root=www`）。
- 每个业务模块有独立单测程序（断言并在失败时非零退出）。

## 9. 文件树（目标）

```
cc_httpd/
├── Makefile
├── config.ini
├── include/   str_util.hbs http_request.hbs http_response.hbs mime.hbs
│              file_server.hbs router.hbs config.hbs handler.hbs
│   platform/  net.hbs fs.hbs log.hbs thread_pool.hbs runtime.hbs
├── src/        同名 .cbs  + main.cbs（聚合器）
│   platform/  net.cbs fs.cbs log.cbs thread_pool.cbs runtime.cbs
├── www/        index.html  style.css
└── tests/      test_str_util.cbs test_mime.cbs test_http_request.cbs
               test_http_response.cbs test_config.cbs test_router.cbs
               test_path_guard.cbs test_handler.cbs
               check_no_unsafe.sh run_integration.sh
```

## 10. 已解决的开放问题

- **POST 处理：** 通用解析体（存入 `Request.body`）；演示处理器以 GET 为主。满足规范。
- **日志：** 简单 stderr 请求日志（方法 + 路径 + 状态）。在范围内，最小化。
- **Keep-alive：** 每连接处理一个请求（`Connection: close`）；流水线为非目标。
- **集合容量：** `MAX_HEADERS`（如 64）、`MAX_ROUTES`（如 32）为编译期上限；超出则丢弃多余
  首部 / 拒绝注册（启动期断言）。这是「定长数组」选型的已知取舍。
- **为何仍链接 `-lstdcbs`：** `safe_malloc*`/`safe_free*`/`safe_swap` 是所有权运行时的实现，
  位于 `libstdcbs`，并非 libcbs 容器；保留它不违反「禁用 libcbs（容器）」约束。
