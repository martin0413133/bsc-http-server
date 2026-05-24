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

**`net.cbs`** — BSC 外部函数二次安全声明 + 实现：

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
```

所有权流：
- `SSL_CTX_new` / `SSL_new` 返回 `_Owned` → 直接赋值给包装结构体的 `_Owned` 字段，无需 `__move_to_raw`
- `SSL_CTX_free` / `SSL_free` 接收 `_Owned` → 从包装结构体移动 `_Owned` 字段出，直接传入
- 借用类函数（`_Borrow` 参数）→ 通过 `&_Mut *owned` 从 `_Owned` 创建借用，无需 `_Unsafe`
- 仅在以下情况保留 `_Unsafe`：POSIX 调用（需 `&` / 指针转换）、OpenSSL 宏（`SSL_CTX_set_mode`）、从原始字段创建借用（`ssl->raw_ssl`）

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

## _Unsafe 块分布

| 位置 | _Unsafe 块 | 理由 |
|---|---|---|
| `net_listen/accept/recv/send` | 5 | POSIX 系统调用需 `&` / `NULL`→指针 / 数组→void* 转换 |
| `net_ssl_ctx_new` | 1 | `SSL_CTX_set_mode` 是宏 |
| `net_ssl_accept` | 1 | 借用准备：`&_Mut *ctx->raw_ctx` 访问原始字段 |
| `net_ssl_recv/send_string` | 1 each | `SSL_read/SSL_write` + 缓冲区 void* 参数 |
| `net_ssl_conn_free` | 1 | `SSL_shutdown` 借用 from `_Owned` 字段 |
| `runtime_handle_conn` | 4 表达式 | 原始指针访问 (`ctx->config`, `ctx->ssl_ctx`) |
| `runtime_serve` | 1 大块 | POSIX 信号、pthread、select |
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
