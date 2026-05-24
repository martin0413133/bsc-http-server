# cc_httpd HTTPS 支持

为 cc_httpd 项目添加了基于 OpenSSL 的 HTTPS 支持，HTTP/HTTPS 双端口同时监听。

## 变更概览 (12 files, +298/-44)

### 配置层

**`include/config.hbs`** — Config 结构新增 3 个字段：
```c
int ssl_port;       // HTTPS 端口，0 表示禁用
String ssl_cert;    // PEM 证书路径
String ssl_key;     // PEM 私钥路径
```

**`src/config.cbs`** — 解析 `ssl_port`、`ssl_cert`、`ssl_key`，默认值均为 0/空（SSL 禁用）。

**`config.ini`** — SSL 示例配置（默认注释）：
```ini
# ssl_port = 8443
# ssl_cert = cert.pem
# ssl_key = key.pem
```

### SSL 适配层 (`src/platform/net.*`)

**`net.hbs`** — 新增 SSL 类型和接口：
```c
// 前向声明 OpenSSL 不透明类型
struct ssl_ctx_st;   // SSL_CTX
struct ssl_st;       // SSL

// 包装结构体 — _Owned 字段持有内部 OpenSSL 资源
typedef struct SSLCtx  { struct ssl_ctx_st *_Owned _Nonnull raw_ctx; } SSLCtx;
typedef struct SSLConn { struct ssl_st     *_Owned _Nonnull raw_ssl; int fd; } SSLConn;

// _Safe 接口
_Safe SSLCtx  *_Owned _Nullable net_ssl_ctx_new(const char* cert, const char* key);
_Safe SSLConn *_Owned _Nullable net_ssl_accept(const SSLCtx* _Borrow ctx, int fd);
_Safe _Bool net_ssl_recv_string(SSLConn* _Borrow ssl, String* _Borrow out);
_Safe _Bool net_ssl_send_string(SSLConn* _Borrow ssl, const String* _Borrow data);
_Safe void net_ssl_conn_free(SSLConn *_Owned _Nullable ssl);
_Safe void net_ssl_ctx_free(SSLCtx *_Owned _Nullable ctx);
```

**`net.cbs`** — BSC 外部函数二次安全声明 + 实现。

对外部 C 函数添加 `_Safe`、`_Owned`、`_Borrow` 声明：
```c
// _Owned: 分配新资源 / 消费资源
_Safe SSL_CTX *_Owned SSL_CTX_new(const SSL_METHOD* method);
_Safe void     SSL_CTX_free(SSL_CTX *_Owned ctx);
_Safe SSL     *_Owned SSL_new(SSL_CTX* _Borrow ctx);
_Safe void     SSL_free(SSL *_Owned ssl);

// _Borrow: 借用不消费
_Safe int  SSL_CTX_use_certificate_file(SSL_CTX* _Borrow ctx, ...);
_Safe int  SSL_accept(SSL* _Borrow ssl);
_Safe int  SSL_read(SSL* _Borrow ssl, void* buf, int num);
// ...

// POSIX — 仅声明参数类型简单、无需 &/cast 的函数
_Safe int  accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
_Safe int  close(int fd);
```

所有权流：
- `SSL_CTX_new` / `SSL_new` 返回 `_Owned` → 直接赋值给包装结构体的 `_Owned` 字段，无需 `__move_to_raw`
- `SSL_CTX_free` / `SSL_free` 接收 `_Owned` → 从包装结构体移动 `_Owned` 字段出，直接传入
- 借用类函数（`_Borrow` 参数）→ 通过 `&_Mut *owned` 从 `_Owned` 创建借用，无需 `_Unsafe`
- `accept` 用 `nullptr` 替代 `NULL`，避免 `((void*)0)` → 指针的隐式转换
- 仅在以下情况保留 `_Unsafe`：POSIX 调用（需 `&` / 指针转换）、OpenSSL 宏（`SSL_CTX_set_mode`）、`void*` 缓冲区参数、从 const borrow 创建 mutable borrow

### 线程池层 (`src/platform/thread_pool.*`)

**`thread_pool.hbs`** — `ConnJob` 替代裸 `int`：
```c
struct ConnJob { int fd; _Bool is_ssl; };  // is_ssl 标记 SSL 连接
```

队列满时丢弃连接仅关闭 fd，无 SSL 资源泄漏（SSL 握手由 worker 执行）。

### 运行时 (`src/platform/runtime.*`)

**`runtime.hbs`** — `ServerCtx` 添加 SSL 上下文：
```c
struct ServerCtx {
    const Config* _Nonnull config;
    const Router* _Nonnull router;
    void* _Nullable ssl_ctx;   // SSLCtx*，SSL 禁用时为 NULL
};
```

**`runtime.cbs`** — 核心变更：
- SSL 初始化（`_Safe` 上下文调用 `net_ssl_ctx_new`）
- `select()` 同时监听 HTTP/HTTPS 两个 listen socket
- Worker 内执行 TLS 握手（`net_ssl_accept`），`SSLConn *_Owned` 生命周期由 worker 管理
- SSL 连接通过 `_Bool is_ssl` 标记，不经过 `void*` 传递所有权
- SSL 禁用时行为与之前完全一致（向后兼容）

### 构建

**`Makefile`** — 编译器路径 + 链接 `-lssl -lcrypto`。

**`cert.pem` / `key.pem`** — 测试用自签名证书（`openssl req -x509 -newkey rsa:2048 -nodes -subj "/CN=localhost"`）。

## `_Safe` 外部声明模式

### 声明规则

| C 函数特征 | BSC 声明 |
|---|---|
| 分配新对象，调用方负责释放 | `_Safe T *_Owned fn(...)` |
| 通过指针读取/配置，不释放 | `_Safe int fn(T* _Borrow p, ...)` |
| 通过 const 指针读取 | `_Safe int fn(const T* _Borrow p, ...)` |
| 消费并释放指针 | `_Safe void fn(T *_Owned p)` |
| 返回静态内部指针（无参数绑定生命周期） | `_Safe const T* fn(void)` — 不加 `_Borrow` |

### 硬性约束

1. **参数类型必须与原 C 声明精确匹配**。不能改 `const`、不能把 `void*` 改成 `char*`、不能把 `struct sockaddr*` 改成其他类型。BSC 按类型精确匹配来选取 `_Safe` 声明，不匹配则回退到原始 `_Unsafe` 声明。

2. **`_Borrow` 返回值必须有 `_Borrow` 参数绑定生命周期**。`_Safe const char *_Borrow fn(void)` 编译错误——没有 borrow 参数供返回值关联生命周期。

3. **宏不能声明 `_Safe`**。`SSL_CTX_set_mode` 是宏，只能在 `_Unsafe` 中调用。

### `_Unsafe(expr)` 陷阱

`_Unsafe(fn_call)` 会擦除 `_Safe` 声明的返回类型注解（`_Owned` 等），因为它回退到原始 C 声明。解决方式——两步法：

```c
// WRONG: _Unsafe() 返回 raw SSL*，丢失 _Owned
SSL *_Owned ssl = _Unsafe(SSL_new(ctx->raw_ctx));

// CORRECT: 在 _Unsafe 中准备参数，在 _Safe 中调用
SSL_CTX* _Borrow b_ctx;
_Unsafe { b_ctx = &_Mut *ctx->raw_ctx; }
SSL *_Owned raw_ssl = SSL_new(b_ctx);  // _Safe 调用，保留 _Owned
```

### `&_Const` / `&_Mut` 产生 borrow

```c
SSL_CTX *_Owned raw = ...;
&_Mut *raw    // SSL_CTX* _Borrow — 可变借用，_Safe 允许
&_Const *raw  // const SSL_CTX* _Borrow — 不可变借用，_Safe 允许

const SSLCtx* _Borrow ctx = ...;
&_Mut *ctx->raw_ctx    // _Safe 禁止！const borrow 下 _Owned 字段被冻结
                       // → 必须在 _Unsafe 中穿越 const
```

### 哪些 C API 适合 `_Safe` 声明

| 适合（typed-pointer API） | 不适合（POSIX socket/IO） | 不适合原因 |
|---|---|---|
| OpenSSL (`SSL_CTX*`, `SSL*`) | `recv`, `send` | 参数 `void*`，调用点需 `char[]` → `void*` 衰减 |
| libcurl (`CURL*`) | `setsockopt`, `bind` | 需 `&var` 取地址，`_Safe` 禁止对局部变量取 `&` |
| SQLite (`sqlite3_stmt*`) | `accept(..., NULL, NULL)` | `NULL` = `((void*)0)` → 指针转换禁止；改用 `nullptr` 可解决 |
| — | `memset`, `memcpy` | 参数 `void*`，需 `&struct` → `void*` 转换 |
| — | `socket`, `listen` | 纯 int 参数，理论上可以但仅在 `_Unsafe` 块内调用，无收益 |

## `_Unsafe` 块分布

| 位置 | `_Unsafe` 块 | 理由 |
|---|---|---|
| `net_listen` | 1 | `setsockopt(&opt)`, `bind(&addr)`, `memset(&addr)` — 需 `&` 取地址 |
| `net_accept` | 1 | `setsockopt(&tv)` — 需 `&` 取地址；`accept(..., nullptr, nullptr)` 已无 `_Unsafe` |
| `net_recv_string` | 1 | `recv(fd, buf, ...)` — `char[]` → `void*` 衰减 |
| `net_send_string` | 1 | `send(fd, chunk+sent, ...)` — 指针算术 + `const char*` → `const void*` |
| `net_ssl_ctx_new` | 1 | `SSL_CTX_set_mode` 是宏 |
| `net_ssl_accept` | 1 | `&_Mut *ctx->raw_ctx` — const borrow 下创建 mutable borrow |
| `net_ssl_recv_string` | 1 | `SSL_read` — `_Borrow` → raw `SSL*` + `char[]` → `void*` |
| `net_ssl_send_string` | 1 | `SSL_write` — 同上 |
| `net_ssl_conn_free` | 1 | `SSL_shutdown(&_Mut *ssl->raw_ssl)` — `_Owned` 指针下取 `&_Mut` |
| `runtime_handle_conn` | 4 表达式 | 原始指针访问 (`ctx->config`, `ctx->ssl_ctx`) |
| `runtime_serve` | 1 大块 | POSIX 信号、pthread、select、`__move_to_raw` |
| `thread_pool_*` | 3 | pthread + 原始结构体字段访问 |

## 使用方式

### HTTP only（默认，向后兼容）
```ini
port = 8080
document_root = www
threads = 4
```

### HTTP + HTTPS 双端口
```ini
port = 8080
ssl_port = 8443
ssl_cert = cert.pem
ssl_key = key.pem
document_root = www
threads = 4
```

### 生成测试证书
```bash
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout key.pem -out cert.pem -days 365 \
  -subj "/CN=localhost"
```

### 测试
```bash
curl -sk https://localhost:8443/
```
