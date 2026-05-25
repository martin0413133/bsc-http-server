# cc_httpd —— BiSheng C HTTP 服务器实现计划（仅所有权版）

> **致执行者（agentic worker）：** 必需子技能：用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 按任务逐个实现本计划。步骤用复选框（`- [ ]`）语法跟踪。

**目标:** 用 BiSheng C 构建一个内存安全的 HTTP/1.1 服务器，提供静态文件与动态路由，含 pthread 线程池；**仅使用所有权系统**——不使用 libcbs 容器、不使用成员函数、不使用 trait、不为自定义类型使用泛型。

**架构:** 模块化单一翻译单元——每个关注点一对 `.hbs`/`.cbs`；`src/main.cbs` 按依赖顺序 `#include` 各 `.cbs` 并作为一个单元编译。业务模块（str_util/mime/http_request/http_response/config/router/file_server/handler）100% `_Safe`、无 `_Unsafe`，由独立单测程序验证。IO/线程模块（platform/net、fs、log、thread_pool、runtime）用最小 `_Unsafe` FFI 接缝，由 curl 集成测试验证。

**技术栈:** BiSheng C（clang BSC 构建）；所有权运行时 `bishengc_safety.hbs`（`safe_malloc` / `safe_free` / `safe_malloc_array` / `safe_free_array` / `safe_swap`，链接 `libstdcbs`）；POSIX sockets + pthread 经 `_Unsafe` FFI。**不使用** libcbs 容器（`String`/`Vec`）。

---

## 已验证工具链（精确使用）

```makefile
CC    := /home/zly/bsc/llvm-project/build/bin/clang
INC   := -I/home/zly/bsc/llvm-project/install/include/libcbs
LIB   := -L/home/zly/bsc/llvm-project/install/lib -lstdcbs -lpthread
FLAGS := -Wall -Wextra -Wno-nullability-completeness -g
```

构建：`$(CC) $(FLAGS) $(INC) src/main.cbs -o bin/httpd $(LIB)`
单文件语法检查：`$(CC) $(FLAGS) $(INC) -fsyntax-only <file>`（文件须含其依赖）
构建单测：`$(CC) $(FLAGS) $(INC) tests/test_X.cbs -o bin/test_X $(LIB)` 然后 `./bin/test_X`

**注意：** 仍需 `-I…/libcbs`（用于 `bishengc_safety.hbs`）与 `-lstdcbs`（`safe_malloc*` 等的实现）。
这些是所有权运行时，**不是** libcbs 容器，保留它们不违反「禁用 libcbs」约束。**不要** `#include "string.hbs"`/`"vec.hbs"`。

## 已验证 BSC 事实（已用编译器实测，勿重新踩坑）

1. **不 include `string.hbs`/`vec.hbs`；每个用到所有权原语的文件 `#include "bishengc_safety.hbs"`。**
2. **`_Safe` 中原始 `char*` 的下标 `p[i]`（读写）合法；指针算术 `p + n` 合法；但 `*p` / `p->field`
   解引用被禁止。** 取地址 `&` 禁止——用 `&_Const` / `&_Mut`。`NULL` 禁止——用 `nullptr`。
3. **借用穿透访问数组字段合法**：`r->method[i]`、`r->headers[k].name[i]`（`r` 为 `_Borrow`）。
   返回内部数组指针 `return r->headers[k].value;` 合法。
4. **owned 缓冲：** `char *_Owned _ArrayElem b = safe_malloc_array(n, (char)0);` 分配，`b[i]` 下标
   读写（`_Safe`），`safe_free_array(b)` 释放。
5. **裸 `_Owned` / `_Owned _ArrayElem` 局部变量不自动析构**——必须显式 `safe_free*` / move /
   return，否则报 `memory leak`。只有 `_Owned struct` 才自动调析构。
6. **`_Owned struct` 的 owned 字段构造后不可重新赋值**——用 `safe_swap(&_Mut field, &_Mut tmp)`
   并随后 `safe_free*(tmp)` 释放换出的旧值。
7. **借用 owned 缓冲内容**：`&_Mut b[0]` / `&_Const b[0]` 得到 `char *_Borrow _ArrayElem`（支持 `[]`），
   传给取 `char *_Borrow _ArrayElem` 形参的辅助函数。**不可**直接传 `b`（owned→borrow 被禁）或
   `&_Mut b`（取的是指针变量地址）。
8. **把 owned 指针本身传给非借用形参（含 `printf`）= 移动**，之后不能再 `safe_free*`；读取时
   改传借用 `&_Const b[0]`。
9. **聚合初始化 `_Owned struct` 必须完整**；定长数组字段可用 `{0}` / `{}`，之后用下标填充；owned
   字段须在初始化时给定（无体时给占位 `safe_malloc_array((size_t)1,(char)0)`）。
10. **局部 `char tmp[N]`：** 若后续按下标读取，须完整初始化（`char tmp[24] = {0};`），否则报
    `use of uninitialized value`。
11. **跨线程：** worker 从原始 `ServerCtx*` 重建借用——`const T* _Borrow x = _Unsafe(&_Const ctx->field);`。
12. **`printf`/`fprintf` 及所有 syscall 必须在 `_Unsafe { ... }` 内。**
13. **普通 `struct` 引用须带 `struct` 标签**（`struct Header`、`struct Config`）；`_Owned struct`
    可省略标签（`Request`、`Response`）。

## 约定

- 头 `include/*.hbs` 声明；源 `src/*.cbs` 定义并 `#include` 自己的 `.hbs`。
- 每个 `.cbs` 按需 `#include` `bishengc_safety.hbs` 与项目头（include guard 保证幂等）。
- `src/main.cbs` 按依赖顺序 `#include` 其余每个 `.cbs`，然后定义 `main`。
- 每个任务完成后用所示信息提交。
- 辅助函数命名 `module_verb`（无成员函数）。

## 文件结构

```
cc_httpd/
├── Makefile
├── config.ini
├── include/   str_util.hbs http_request.hbs http_response.hbs mime.hbs
│              config.hbs router.hbs file_server.hbs handler.hbs
│   platform/  net.hbs fs.hbs log.hbs thread_pool.hbs runtime.hbs
├── src/       （同名）.cbs + main.cbs
│   platform/  net.cbs fs.cbs log.cbs thread_pool.cbs runtime.cbs
├── www/       index.html style.css
└── tests/     test_str_util.cbs test_mime.cbs test_http_request.cbs
              test_http_response.cbs test_config.cbs test_router.cbs
              test_path_guard.cbs test_handler.cbs
              smoke.cbs check_no_unsafe.sh run_integration.sh
```

---

## Task 0：脚手架 + 工具链冒烟测试

**文件:**
- 创建: `Makefile`, `config.ini`, `www/index.html`, `www/style.css`, `tests/smoke.cbs`

- [ ] **步骤 1: 写冒烟测试** `tests/smoke.cbs`（用所有权原语，不用 String/Vec）

```c
#include "bishengc_safety.hbs"
#include <stddef.h>
#include <stdio.h>

_Owned struct Buf {
_Public:
    char *_Owned _ArrayElem data;
    size_t len;
    ~Buf(Buf this) { safe_free_array(this.data); }
};

_Safe Buf make(void) {
    Buf b = { .data = safe_malloc_array((size_t)4, (char)0), .len = 2 };
    b.data[0] = 'h'; b.data[1] = 'i';
    return b;
}

_Safe int main(void) {
    Buf b = make();
    char c0 = b.data[0];
    char c1 = b.data[1];
    _Unsafe { printf("smoke len=%zu data=%c%c\n", b.len, c0, c1); }
    return 0;
}
```

- [ ] **步骤 2: 写 Makefile**

```makefile
CC    := /home/zly/bsc/llvm-project/build/bin/clang
INC   := -I/home/zly/bsc/llvm-project/install/include/libcbs
LIB   := -L/home/zly/bsc/llvm-project/install/lib -lstdcbs -lpthread
FLAGS := -Wall -Wextra -Wno-nullability-completeness -g
BINDIR := bin

all: $(BINDIR)/httpd

$(BINDIR)/httpd: src/main.cbs $(wildcard src/*.cbs) $(wildcard src/platform/*.cbs) $(wildcard include/*.hbs) $(wildcard include/platform/*.hbs) | $(BINDIR)
	$(CC) $(FLAGS) $(INC) src/main.cbs -o $@ $(LIB)

$(BINDIR):
	mkdir -p $(BINDIR)

# 构建任意单测：make test-X （编译 tests/test_X.cbs）
test-%: tests/test_%.cbs | $(BINDIR)
	$(CC) $(FLAGS) $(INC) $< -o $(BINDIR)/test_$* $(LIB)
	./$(BINDIR)/test_$*

smoke: tests/smoke.cbs | $(BINDIR)
	$(CC) $(FLAGS) $(INC) $< -o $(BINDIR)/smoke $(LIB)
	./$(BINDIR)/smoke

run: $(BINDIR)/httpd
	./$(BINDIR)/httpd

clean:
	rm -rf $(BINDIR)

.PHONY: all clean run smoke
```

- [ ] **步骤 3: 运行冒烟** — `make smoke`
预期：构建成功并打印 `smoke len=2 data=hi`。

- [ ] **步骤 4: 创建资产**

`config.ini`:
```ini
# cc_httpd configuration
port = 8080
document_root = www
threads = 4
```

`www/index.html`:
```html
<!DOCTYPE html>
<html><head><title>cc_httpd</title><link rel="stylesheet" href="/style.css"></head>
<body><h1>Hello from cc_httpd (BiSheng C, ownership-only)</h1><p>Static file + dynamic routing, memory-safe.</p></body></html>
```

`www/style.css`:
```css
body { font-family: sans-serif; margin: 40px; } h1 { color: #225; }
```

- [ ] **步骤 5: 提交**
```bash
git add Makefile config.ini www tests/smoke.cbs
git commit -m "chore: scaffold cc_httpd (ownership-only) build + smoke test"
```

---

## Task 1：str_util —— 字符串辅助（TDD）

**文件:** 创建 `include/str_util.hbs`, `src/str_util.cbs`, `tests/test_str_util.cbs`

- [ ] **步骤 1: 写失败测试** `tests/test_str_util.cbs`

```c
#include "bishengc_safety.hbs"
#include <stdio.h>
#include "../src/str_util.cbs"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

_Safe int main(void) {
    char dst[16] = {0};
    cbuf_copy(dst, 16, "GET /x");
    CHECK(cstr_len(dst) == 6, "copy length");
    CHECK(cbuf_eq(dst, "GET /x"), "eq true");
    CHECK(!cbuf_eq(dst, "GET /y"), "eq false");
    CHECK(cbuf_starts_with(dst, "GET"), "starts_with true");
    CHECK(!cbuf_starts_with(dst, "POST"), "starts_with false");
    CHECK(uint_digits(0) == 1 && uint_digits(7) == 1 && uint_digits(404) == 3, "uint_digits");

    char *_Owned _ArrayElem d = cbuf_dup_range("abcdef", 2, 3);   // "cde"
    CHECK(d[0]=='c' && d[1]=='d' && d[2]=='e' && d[3]=='\0', "dup_range");
    safe_free_array(d);

    char buf[32] = {0};
    size_t pos = 0;
    put_cstr(&_Mut buf[0], &_Mut pos, "n=");
    put_uint(&_Mut buf[0], &_Mut pos, 42);
    buf[pos] = '\0';
    CHECK(cbuf_eq(buf, "n=42"), "put_cstr+put_uint");

    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_str_util OK\n");}
    return 0;
}
```

- [ ] **步骤 2: 验证失败** — `make test-str_util`
预期：FAIL —— 找不到 `str_util.cbs` / 函数未定义。

- [ ] **步骤 3: 写 `include/str_util.hbs`**

```c
#ifndef CC_STR_UTIL_HBS
#define CC_STR_UTIL_HBS
#include <stddef.h>
#include "bishengc_safety.hbs"

_Safe size_t cstr_len(const char* _Nonnull s);
// 拷贝 src 到 dst（最多 cap-1 字节）并 NUL 结尾
_Safe void   cbuf_copy(char* _Nonnull dst, size_t cap, const char* _Nonnull src);
_Safe _Bool  cbuf_eq(const char* _Nonnull a, const char* _Nonnull b);
_Safe _Bool  cbuf_starts_with(const char* _Nonnull s, const char* _Nonnull prefix);
// 复制 s[start .. start+len) 到一个新 owned 缓冲（末尾追加 '\0'）；调用方负责 safe_free_array
_Safe char *_Owned _ArrayElem cbuf_dup_range(const char* _Nonnull s, size_t start, size_t len);
// 十进制位数（0 -> 1）
_Safe size_t uint_digits(size_t n);
// 往 buf[*pos..] 写入并推进 *pos（buf 须为 _Borrow _ArrayElem：调用方传 &_Mut owned[0]）
_Safe void   put_cstr(char *_Borrow _ArrayElem buf, size_t* _Borrow pos, const char* _Nonnull s);
_Safe void   put_uint(char *_Borrow _ArrayElem buf, size_t* _Borrow pos, size_t n);
#endif
```

- [ ] **步骤 4: 写 `src/str_util.cbs`**

```c
#include "../include/str_util.hbs"

_Safe size_t cstr_len(const char* _Nonnull s) {
    size_t n = 0;
    while (s[n] != '\0') { n++; }
    return n;
}

_Safe void cbuf_copy(char* _Nonnull dst, size_t cap, const char* _Nonnull src) {
    if (cap == 0) { return; }
    size_t i = 0;
    while (src[i] != '\0' && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

_Safe _Bool cbuf_eq(const char* _Nonnull a, const char* _Nonnull b) {
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) { return 0; }
        i++;
    }
    return a[i] == b[i];   // 都到 '\0' 才相等
}

_Safe _Bool cbuf_starts_with(const char* _Nonnull s, const char* _Nonnull prefix) {
    size_t i = 0;
    while (prefix[i] != '\0') {
        if (s[i] != prefix[i]) { return 0; }
        i++;
    }
    return 1;
}

_Safe char *_Owned _ArrayElem cbuf_dup_range(const char* _Nonnull s, size_t start, size_t len) {
    char *_Owned _ArrayElem out = safe_malloc_array(len + 1, (char)0);
    for (size_t i = 0; i < len; i++) { out[i] = s[start + i]; }
    out[len] = '\0';
    return out;
}

_Safe size_t uint_digits(size_t n) {
    if (n == 0) { return 1; }
    size_t d = 0;
    while (n > 0) { d++; n /= 10; }
    return d;
}

_Safe void put_cstr(char *_Borrow _ArrayElem buf, size_t* _Borrow pos, const char* _Nonnull s) {
    size_t p = *pos;
    for (size_t i = 0; s[i] != '\0'; i++) { buf[p] = s[i]; p++; }
    *pos = p;
}

_Safe void put_uint(char *_Borrow _ArrayElem buf, size_t* _Borrow pos, size_t n) {
    char tmp[24] = {0};
    size_t k = 0;
    if (n == 0) { tmp[0] = '0'; k = 1; }
    else { while (n > 0) { tmp[k] = (char)('0' + (int)(n % 10)); n /= 10; k++; } }
    size_t p = *pos;
    while (k > 0) { k--; buf[p] = tmp[k]; p++; }
    *pos = p;
}
```

- [ ] **步骤 5: 验证通过** — `make test-str_util`
预期：PASS —— 打印 `test_str_util OK`。

- [ ] **步骤 6: 提交**
```bash
git add include/str_util.hbs src/str_util.cbs tests/test_str_util.cbs
git commit -m "feat: str_util (cbuf copy/eq/starts_with, dup_range, put_cstr/uint)"
```

---

## Task 2：mime —— 扩展名 → Content-Type（TDD，纯 `_Safe`）

**文件:** 创建 `include/mime.hbs`, `src/mime.cbs`, `tests/test_mime.cbs`

- [ ] **步骤 1: 写失败测试** `tests/test_mime.cbs`

```c
#include "bishengc_safety.hbs"
#include <stdio.h>
#include "../src/str_util.cbs"
#include "../src/mime.cbs"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

_Safe int main(void) {
    CHECK(cbuf_eq(mime_for_path("a/index.html"), "text/html"), "html");
    CHECK(cbuf_eq(mime_for_path("style.css"), "text/css"), "css");
    CHECK(cbuf_eq(mime_for_path("app.js"), "application/javascript"), "js");
    CHECK(cbuf_eq(mime_for_path("pic.png"), "image/png"), "png");
    CHECK(cbuf_eq(mime_for_path("noext"), "application/octet-stream"), "default");
    CHECK(cbuf_eq(mime_for_path("weird.xyz"), "application/octet-stream"), "unknown");
    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_mime OK\n");}
    return 0;
}
```

- [ ] **步骤 2: 验证失败** — `make test-mime` → FAIL（`mime_for_path` 未定义）。

- [ ] **步骤 3: 写 `include/mime.hbs`**

```c
#ifndef CC_MIME_HBS
#define CC_MIME_HBS
// 返回 path 扩展名对应的静态 Content-Type 字符串。
_Safe const char* mime_for_path(const char* _Nonnull path);
#endif
```

- [ ] **步骤 4: 写 `src/mime.cbs`**（用下标找扩展名 + `cbuf_eq` 比较，无 `_Unsafe`）

```c
#include "../include/mime.hbs"
#include "../include/str_util.hbs"

_Safe const char* mime_for_path(const char* _Nonnull path) {
    size_t n = cstr_len(path);
    size_t dot = n;                       // n 表示「未找到」
    for (size_t i = 0; i < n; i++) { if (path[i] == '.') { dot = i; } }
    if (dot == n) { return "application/octet-stream"; }
    const char* ext = path + dot + 1;     // 指针算术在 _Safe 中合法
    if (cbuf_eq(ext, "html") || cbuf_eq(ext, "htm")) { return "text/html"; }
    if (cbuf_eq(ext, "css"))  { return "text/css"; }
    if (cbuf_eq(ext, "js"))   { return "application/javascript"; }
    if (cbuf_eq(ext, "json")) { return "application/json"; }
    if (cbuf_eq(ext, "png"))  { return "image/png"; }
    if (cbuf_eq(ext, "jpg") || cbuf_eq(ext, "jpeg")) { return "image/jpeg"; }
    if (cbuf_eq(ext, "gif"))  { return "image/gif"; }
    if (cbuf_eq(ext, "svg"))  { return "image/svg+xml"; }
    if (cbuf_eq(ext, "txt"))  { return "text/plain"; }
    return "application/octet-stream";
}
```

- [ ] **步骤 5: 验证通过** — `make test-mime` → PASS（`test_mime OK`）。

- [ ] **步骤 6: 提交**
```bash
git add include/mime.hbs src/mime.cbs tests/test_mime.cbs
git commit -m "feat: mime type lookup (pure _Safe, no _Unsafe)"
```

---

## Task 3：http_request —— 请求解析器（TDD）

**文件:** 创建 `include/http_request.hbs`, `src/http_request.cbs`, `tests/test_http_request.cbs`

从 NUL 结尾缓冲解析（服务器在 recv 后 NUL 结尾）。method/path/version 存入定长缓冲；
首部存入 `struct Header headers[MAX_HEADERS]`；body 是空行后的剩余字节，复制到 owned 缓冲。

- [ ] **步骤 1: 写失败测试** `tests/test_http_request.cbs`

```c
#include "bishengc_safety.hbs"
#include <stdio.h>
#include "../src/str_util.cbs"
#include "../src/http_request.cbs"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

_Safe int main(void) {
    const char* raw =
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "User-Agent: test\r\n"
        "\r\n";
    Request req = request_parse(raw);
    CHECK(cbuf_eq(req.method, "GET"), "method");
    CHECK(cbuf_eq(req.path, "/index.html"), "path");
    CHECK(cbuf_eq(req.version, "HTTP/1.1"), "version");
    CHECK(req.ok, "ok flag");

    const char* host = request_get_header(&_Const req, "Host");
    CHECK(host != nullptr && cbuf_eq(host, "localhost"), "host header");
    CHECK(request_get_header(&_Const req, "Nope") == nullptr, "missing header");

    Request bad = request_parse("garbage-no-crlf");
    CHECK(!bad.ok, "malformed not ok");

    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_http_request OK\n");}
    return 0;
}
```

- [ ] **步骤 2: 验证失败** — `make test-http_request` → FAIL。

- [ ] **步骤 3: 写 `include/http_request.hbs`**

```c
#ifndef CC_HTTP_REQUEST_HBS
#define CC_HTTP_REQUEST_HBS
#include <stddef.h>
#include "bishengc_safety.hbs"

#define MAX_HEADERS 64

struct Header { char name[64]; char value[256]; };

_Owned struct Request {
_Public:
    char method[8];
    char path[1024];
    char version[16];
    struct Header headers[MAX_HEADERS];
    size_t n_headers;
    char *_Owned _ArrayElem body;     // 非空：始终指向有效缓冲（无体时为长度 0 的占位）
    size_t body_len;
    _Bool ok;
    ~Request(Request this) { safe_free_array(this.body); }
};

// 解析 NUL 结尾的原始请求；畸形时 ok=false。
_Safe Request request_parse(const char* _Nonnull raw);
// 大小写敏感首部查找；返回指向内部值缓冲的指针（借用绑定 r），缺失返回 nullptr。
_Safe const char* _Nullable request_get_header(const Request* _Borrow r, const char* _Nonnull name);
#endif
```

- [ ] **步骤 4: 写 `src/http_request.cbs`**

```c
#include "../include/http_request.hbs"
#include "../include/str_util.hbs"

_Safe Request request_parse(const char* _Nonnull raw) {
    Request r = {
        .method = {0}, .path = {0}, .version = {0},
        .headers = {}, .n_headers = 0,
        .body = safe_malloc_array((size_t)1, (char)0),   // 占位
        .body_len = 0, .ok = 0
    };
    size_t i = 0;
    size_t j = 0;
    // method（容量 8）
    while (raw[i] != '\0' && raw[i] != ' ' && raw[i] != '\r' && raw[i] != '\n') {
        if (j < 7) { r.method[j] = raw[i]; j++; }
        i++;
    }
    r.method[j] = '\0';
    if (raw[i] != ' ') { return r; }    // 畸形：body=占位，ok=0
    i++;
    // path（容量 1024）
    j = 0;
    while (raw[i] != '\0' && raw[i] != ' ' && raw[i] != '\r' && raw[i] != '\n') {
        if (j < 1023) { r.path[j] = raw[i]; j++; }
        i++;
    }
    r.path[j] = '\0';
    if (raw[i] != ' ') { return r; }
    i++;
    // version（容量 16）
    j = 0;
    while (raw[i] != '\0' && raw[i] != '\r' && raw[i] != '\n') {
        if (j < 15) { r.version[j] = raw[i]; j++; }
        i++;
    }
    r.version[j] = '\0';
    if (raw[i] != '\r' && raw[i] != '\n') { return r; }
    if (raw[i] == '\r') { i++; }
    if (raw[i] == '\n') { i++; }
    // 首部直到空行
    while (raw[i] != '\0' && raw[i] != '\r' && raw[i] != '\n') {
        if (r.n_headers < MAX_HEADERS) {
            size_t k = r.n_headers;
            size_t hn = 0;
            while (raw[i] != '\0' && raw[i] != ':' && raw[i] != '\r' && raw[i] != '\n') {
                if (hn < 63) { r.headers[k].name[hn] = raw[i]; hn++; }
                i++;
            }
            r.headers[k].name[hn] = '\0';
            if (raw[i] == ':') { i++; }
            while (raw[i] == ' ') { i++; }              // 跳过前导空白
            size_t hv = 0;
            while (raw[i] != '\0' && raw[i] != '\r' && raw[i] != '\n') {
                if (hv < 255) { r.headers[k].value[hv] = raw[i]; hv++; }
                i++;
            }
            r.headers[k].value[hv] = '\0';
            r.n_headers = k + 1;
        } else {
            while (raw[i] != '\0' && raw[i] != '\n') { i++; }   // 超上限：丢弃整行
        }
        if (raw[i] == '\r') { i++; }
        if (raw[i] == '\n') { i++; }
    }
    // 跳过空行
    if (raw[i] == '\r') { i++; }
    if (raw[i] == '\n') { i++; }
    // body = 剩余
    size_t total = i;
    while (raw[total] != '\0') { total++; }
    size_t blen = total - i;
    if (blen > 0) {
        char *_Owned _ArrayElem nb = cbuf_dup_range(raw, i, blen);
        safe_swap(&_Mut r.body, &_Mut nb);   // r.body=真体；nb=旧占位
        safe_free_array(nb);                 // 释放换出的占位（事实 5/6）
        r.body_len = blen;
    }
    r.ok = (cstr_len(r.method) > 0 && cstr_len(r.path) > 0);
    return r;
}

_Safe const char* _Nullable request_get_header(const Request* _Borrow r, const char* _Nonnull name) {
    for (size_t k = 0; k < r->n_headers; k++) {
        if (cbuf_eq(r->headers[k].name, name)) { return r->headers[k].value; }
    }
    return nullptr;
}
```

- [ ] **步骤 5: 验证通过** — `make test-http_request` → PASS（`test_http_request OK`）。

- [ ] **步骤 6: 提交**
```bash
git add include/http_request.hbs src/http_request.cbs tests/test_http_request.cbs
git commit -m "feat: HTTP request parser (fixed bufs + owned body)"
```

---

## Task 4：http_response —— 响应构建 + 序列化（TDD）

**文件:** 创建 `include/http_response.hbs`, `src/http_response.cbs`, `tests/test_http_response.cbs`

`Response` 持有 status、status_text/content_type（字面量 `const char*`）、owned body。
`response_serialize` 用「先量长度→一次分配→借用写入」生成完整报文。

- [ ] **步骤 1: 写失败测试** `tests/test_http_response.cbs`

```c
#include "bishengc_safety.hbs"
#include <stdio.h>
#include "../src/str_util.cbs"
#include "../src/http_response.cbs"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

// 把 owned 报文缓冲拷到栈再用 strstr 检查（借用读取 wire[i]，不消费所有权）
static _Bool wire_contains(char *_Owned _ArrayElem wire, size_t len, const char* _Nonnull needle) {
    char buf[4096] = {0};
    for (size_t i = 0; i < len && i < 4095; i++) { buf[i] = wire[i]; }
    _Unsafe { return strstr(buf, needle) != NULL; }
}

_Safe int main(void) {
    Response ok = response_with_cstr(200, "OK", "text/plain", "hello");
    size_t n1 = 0;
    char *_Owned _ArrayElem w1 = response_serialize(&_Const ok, &_Mut n1);
    CHECK(wire_contains(w1, n1, "HTTP/1.1 200 OK\r\n"), "status line");
    CHECK(wire_contains(w1, n1, "Content-Type: text/plain\r\n"), "ctype");
    CHECK(wire_contains(w1, n1, "Content-Length: 5\r\n"), "clen");
    CHECK(wire_contains(w1, n1, "\r\n\r\nhello"), "body sep");
    safe_free_array(w1);

    Response nf = response_not_found();
    size_t n2 = 0;
    char *_Owned _ArrayElem w2 = response_serialize(&_Const nf, &_Mut n2);
    CHECK(wire_contains(w2, n2, "HTTP/1.1 404 Not Found\r\n"), "404 line");
    safe_free_array(w2);

    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_http_response OK\n");}
    return 0;
}
```
（顶部需 `#include <string.h>` 供 `strstr`——加在第一行后。）

- [ ] **步骤 2: 验证失败** — `make test-http_response` → FAIL。

- [ ] **步骤 3: 写 `include/http_response.hbs`**

```c
#ifndef CC_HTTP_RESPONSE_HBS
#define CC_HTTP_RESPONSE_HBS
#include <stddef.h>
#include "bishengc_safety.hbs"

_Owned struct Response {
_Public:
    int status;
    const char* status_text;     // 字面量，如 "OK"
    const char* content_type;    // 字面量，如 "text/html"
    char *_Owned _ArrayElem body;
    size_t body_len;
    ~Response(Response this) { safe_free_array(this.body); }
};

// 消费一个已拥有的体缓冲
_Safe Response response_with_owned(int status, const char* _Nonnull status_text,
                                   const char* _Nonnull content_type,
                                   char *_Owned _ArrayElem body, size_t body_len);
// 以 C 字符串构建体（内部复制到 owned 缓冲）
_Safe Response response_with_cstr(int status, const char* _Nonnull status_text,
                                  const char* _Nonnull content_type, const char* _Nonnull body);
_Safe Response response_not_found(void);
_Safe Response response_bad_request(void);
_Safe Response response_server_error(void);
// 序列化完整报文到 owned 缓冲；长度经 out_len 返回。调用方负责 safe_free_array。
_Safe char *_Owned _ArrayElem response_serialize(const Response* _Borrow r, size_t* _Borrow out_len);
#endif
```

- [ ] **步骤 4: 写 `src/http_response.cbs`**

```c
#include "../include/http_response.hbs"
#include "../include/str_util.hbs"

_Safe Response response_with_owned(int status, const char* _Nonnull status_text,
                                   const char* _Nonnull content_type,
                                   char *_Owned _ArrayElem body, size_t body_len) {
    Response r = {
        .status = status, .status_text = status_text, .content_type = content_type,
        .body = body, .body_len = body_len      // body 在此移入
    };
    return r;
}

_Safe Response response_with_cstr(int status, const char* _Nonnull status_text,
                                  const char* _Nonnull content_type, const char* _Nonnull body) {
    size_t len = cstr_len(body);
    char *_Owned _ArrayElem buf = cbuf_dup_range(body, 0, len);
    return response_with_owned(status, status_text, content_type, buf, len);
}

_Safe Response response_not_found(void) {
    return response_with_cstr(404, "Not Found", "text/html", "<h1>404 Not Found</h1>");
}
_Safe Response response_bad_request(void) {
    return response_with_cstr(400, "Bad Request", "text/html", "<h1>400 Bad Request</h1>");
}
_Safe Response response_server_error(void) {
    return response_with_cstr(500, "Internal Server Error", "text/html", "<h1>500 Internal Server Error</h1>");
}

_Safe char *_Owned _ArrayElem response_serialize(const Response* _Borrow r, size_t* _Borrow out_len) {
    // 第一遍：量长度
    size_t total = 0;
    total += cstr_len("HTTP/1.1 ");
    total += uint_digits((size_t)r->status);
    total += 1;                                  // 空格
    total += cstr_len(r->status_text);
    total += cstr_len("\r\nContent-Type: ");
    total += cstr_len(r->content_type);
    total += cstr_len("\r\nContent-Length: ");
    total += uint_digits(r->body_len);
    total += cstr_len("\r\nConnection: close\r\n\r\n");
    total += r->body_len;
    // 第二遍：一次分配 + 借用写入（&_Mut out[0] -> _Borrow _ArrayElem）
    char *_Owned _ArrayElem out = safe_malloc_array(total + 1, (char)0);
    size_t pos = 0;
    put_cstr(&_Mut out[0], &_Mut pos, "HTTP/1.1 ");
    put_uint(&_Mut out[0], &_Mut pos, (size_t)r->status);
    out[pos] = ' '; pos++;
    put_cstr(&_Mut out[0], &_Mut pos, r->status_text);
    put_cstr(&_Mut out[0], &_Mut pos, "\r\nContent-Type: ");
    put_cstr(&_Mut out[0], &_Mut pos, r->content_type);
    put_cstr(&_Mut out[0], &_Mut pos, "\r\nContent-Length: ");
    put_uint(&_Mut out[0], &_Mut pos, r->body_len);
    put_cstr(&_Mut out[0], &_Mut pos, "\r\nConnection: close\r\n\r\n");
    for (size_t i = 0; i < r->body_len; i++) { out[pos] = r->body[i]; pos++; }
    out[pos] = '\0';
    *out_len = pos;
    return out;
}
```

- [ ] **步骤 5: 验证通过** — `make test-http_response` → PASS。

- [ ] **步骤 6: 提交**
```bash
git add include/http_response.hbs src/http_response.cbs tests/test_http_response.cbs
git commit -m "feat: HTTP response builder + serialization (borrowed _ArrayElem writes)"
```

---

## Task 5：config —— config.ini 解析（TDD）

**文件:** 创建 `include/config.hbs`, `src/config.cbs`, `tests/test_config.cbs`

从缓冲解析 `key = value`（跳过空行与 `#` 注释）。文件读取在 runtime 经 `fs` 适配器完成；
**解析是 `_Safe` 的、在字符串上单测。** `Config` 是纯 `struct`（无 owned），按值传递。

- [ ] **步骤 1: 写失败测试** `tests/test_config.cbs`

```c
#include "bishengc_safety.hbs"
#include <stdio.h>
#include "../src/str_util.cbs"
#include "../src/config.cbs"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

_Safe int main(void) {
    const char* text = "# comment\nport = 9090\ndocument_root = public\nthreads=8\n";
    struct Config c = config_parse(text);
    CHECK(c.port == 9090, "port");
    CHECK(c.threads == 8, "threads");
    CHECK(cbuf_eq(c.document_root, "public"), "doc root");

    struct Config d = config_default();
    CHECK(d.port == 8080, "default port");
    CHECK(cbuf_eq(d.document_root, "www"), "default root");

    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_config OK\n");}
    return 0;
}
```

- [ ] **步骤 2: 验证失败** — `make test-config` → FAIL。

- [ ] **步骤 3: 写 `include/config.hbs`**

```c
#ifndef CC_CONFIG_HBS
#define CC_CONFIG_HBS
#include <stddef.h>

struct Config {
    int port;
    int threads;
    char document_root[256];
};

_Safe struct Config config_default(void);
_Safe struct Config config_parse(const char* _Nonnull text);   // 从缓冲解析
#endif
```

- [ ] **步骤 4: 写 `src/config.cbs`**

```c
#include "../include/config.hbs"
#include "../include/str_util.hbs"

_Safe struct Config config_default(void) {
    struct Config c = { .port = 8080, .threads = 4, .document_root = {0} };
    cbuf_copy(c.document_root, 256, "www");
    return c;
}

// 从只含数字的 char 缓冲解析十进制 int
_Safe int parse_int_buf(const char* _Nonnull s) {
    int v = 0;
    for (size_t i = 0; s[i] != '\0'; i++) {
        char ch = s[i];
        if (ch >= '0' && ch <= '9') { v = v * 10 + (int)(ch - '0'); }
    }
    return v;
}

_Safe struct Config config_parse(const char* _Nonnull text) {
    struct Config c = config_default();
    size_t i = 0;
    while (text[i] != '\0') {
        while (text[i] == ' ' || text[i] == '\t') { i++; }
        if (text[i] == '#' || text[i] == '\n' || text[i] == '\r') {
            while (text[i] != '\0' && text[i] != '\n') { i++; }
            if (text[i] == '\n') { i++; }
            continue;
        }
        if (text[i] == '\0') { break; }
        char key[64] = {0};
        char val[256] = {0};
        size_t k = 0;
        while (text[i] != '\0' && text[i] != '=' && text[i] != '\n'
               && text[i] != ' ' && text[i] != '\t') {
            if (k < 63) { key[k] = text[i]; k++; }
            i++;
        }
        key[k] = '\0';
        while (text[i] == ' ' || text[i] == '\t') { i++; }
        if (text[i] == '=') { i++; }
        while (text[i] == ' ' || text[i] == '\t') { i++; }
        size_t m = 0;
        while (text[i] != '\0' && text[i] != '\n' && text[i] != '\r' && text[i] != ' ') {
            if (m < 255) { val[m] = text[i]; m++; }
            i++;
        }
        val[m] = '\0';
        while (text[i] != '\0' && text[i] != '\n') { i++; }
        if (text[i] == '\n') { i++; }

        if (cbuf_eq(key, "port")) { c.port = parse_int_buf(val); }
        else if (cbuf_eq(key, "threads")) { c.threads = parse_int_buf(val); }
        else if (cbuf_eq(key, "document_root")) { cbuf_copy(c.document_root, 256, val); }
    }
    return c;
}
```

- [ ] **步骤 5: 验证通过** — `make test-config` → PASS。

- [ ] **步骤 6: 提交**
```bash
git add include/config.hbs src/config.cbs tests/test_config.cbs
git commit -m "feat: config.ini parser (plain struct, fixed buffers)"
```

---

## Task 6：router + path guard（TDD）

**文件:** 创建 `include/router.hbs`, `src/router.cbs`, `tests/test_router.cbs`, `tests/test_path_guard.cbs`

`Router` 是纯 `struct`，存 `Route routes[MAX_ROUTES]`；`router_match` 精确匹配 method+path。
`path_is_safe` 拒绝含 `..` 的路径。

- [ ] **步骤 1: 写失败测试** `tests/test_router.cbs`

```c
#include "bishengc_safety.hbs"
#include <stdio.h>
#include "../src/str_util.cbs"
#include "../src/http_request.cbs"
#include "../src/http_response.cbs"
#include "../src/router.cbs"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

_Safe Response hello(const Request* _Borrow req) {
    return response_with_cstr(200, "OK", "text/plain", "hi");
}

_Safe int main(void) {
    struct Router rt = router_new();
    router_add(&_Mut rt, "GET", "/hello", hello);

    Request a = request_parse("GET /hello HTTP/1.1\r\n\r\n");
    Handler h = router_match(&_Const rt, &_Const a);
    CHECK(h != nullptr, "match found");

    Request b = request_parse("GET /nope HTTP/1.1\r\n\r\n");
    CHECK(router_match(&_Const rt, &_Const b) == nullptr, "no match");

    Request c = request_parse("POST /hello HTTP/1.1\r\n\r\n");
    CHECK(router_match(&_Const rt, &_Const c) == nullptr, "method mismatch");

    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_router OK\n");}
    return 0;
}
```

`tests/test_path_guard.cbs`

```c
#include "bishengc_safety.hbs"
#include <stdio.h>
#include "../src/str_util.cbs"
#include "../src/http_request.cbs"
#include "../src/http_response.cbs"
#include "../src/router.cbs"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

_Safe int main(void) {
    CHECK(path_is_safe("/index.html"), "normal ok");
    CHECK(path_is_safe("/css/style.css"), "subdir ok");
    CHECK(!path_is_safe("/../etc/passwd"), "dotdot rejected");
    CHECK(!path_is_safe("/a/../../b"), "embedded dotdot rejected");
    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_path_guard OK\n");}
    return 0;
}
```

- [ ] **步骤 2: 验证两者失败** — `make test-router`、`make test-path_guard` → FAIL。

- [ ] **步骤 3: 写 `include/router.hbs`**

```c
#ifndef CC_ROUTER_HBS
#define CC_ROUTER_HBS
#include <stddef.h>
#include "../include/http_request.hbs"
#include "../include/http_response.hbs"

#define MAX_ROUTES 32

typedef Response (*Handler)(const Request* _Borrow req);

struct Route { char method[8]; char path[256]; Handler handler; };

struct Router { struct Route routes[MAX_ROUTES]; size_t n_routes; };

_Safe struct Router router_new(void);
_Safe void   router_add(struct Router* _Borrow r, const char* _Nonnull method,
                        const char* _Nonnull path, Handler h);
_Safe Handler _Nullable router_match(const struct Router* _Borrow r, const Request* _Borrow req);

// 拒绝含 ".." 段的路径（穿越）。true = 安全。
_Safe _Bool path_is_safe(const char* _Nonnull path);
#endif
```

- [ ] **步骤 4: 写 `src/router.cbs`**

```c
#include "../include/router.hbs"
#include "../include/str_util.hbs"

_Safe struct Router router_new(void) {
    struct Router r = { .routes = {}, .n_routes = 0 };
    return r;
}

_Safe void router_add(struct Router* _Borrow r, const char* _Nonnull method,
                      const char* _Nonnull path, Handler h) {
    if (r->n_routes >= MAX_ROUTES) { return; }
    size_t k = r->n_routes;
    cbuf_copy(r->routes[k].method, 8, method);
    cbuf_copy(r->routes[k].path, 256, path);
    r->routes[k].handler = h;
    r->n_routes = k + 1;
}

_Safe Handler _Nullable router_match(const struct Router* _Borrow r, const Request* _Borrow req) {
    for (size_t k = 0; k < r->n_routes; k++) {
        if (cbuf_eq(r->routes[k].method, req->method)
            && cbuf_eq(r->routes[k].path, req->path)) {
            return r->routes[k].handler;
        }
    }
    return nullptr;
}

_Safe _Bool path_is_safe(const char* _Nonnull path) {
    for (size_t i = 0; path[i] != '\0'; i++) {
        if (path[i] == '.' && path[i + 1] == '.') { return 0; }
    }
    return 1;
}
```

- [ ] **步骤 5: 验证两者通过** — `make test-router`、`make test-path_guard` → PASS。

- [ ] **步骤 6: 提交**
```bash
git add include/router.hbs src/router.cbs tests/test_router.cbs tests/test_path_guard.cbs
git commit -m "feat: router (exact match) + path traversal guard"
```

---

## Task 7：platform/net —— POSIX socket 包装（`_Unsafe` FFI 接缝）

**文件:** 创建 `include/platform/net.hbs`, `src/platform/net.cbs`

无单测（纯 syscall）；由 Task 13 集成测试间接验证。每个函数极小、接缝可审计。

- [ ] **步骤 1: 写 `include/platform/net.hbs`**

```c
#ifndef CC_NET_HBS
#define CC_NET_HBS
#include <stddef.h>
#include <sys/types.h>
// 创建+绑定+监听 port；返回监听 fd 或 -1。
_Safe int net_listen(int port, int backlog);
// 接受一个连接；返回客户端 fd 或 -1。
_Safe int net_accept(int listen_fd);
// recv 最多 cap-1 字节到 buf 并 NUL 结尾；返回读到的字节数（>=0）或 -1。
_Safe ssize_t net_recv(int fd, char* _Nonnull buf, size_t cap);
// 完整发送 len 字节（部分写循环）；成功返回 0，错误 -1。
_Safe int net_send_all(int fd, const char* _Nonnull buf, size_t len);
_Safe void net_close(int fd);
#endif
```

- [ ] **步骤 2: 写 `src/platform/net.cbs`**（每个 `_Safe` 接口内含最小 `_Unsafe`）

```c
#include "../../include/platform/net.hbs"
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

_Safe int net_listen(int port, int backlog) {
    _Unsafe {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { return -1; }
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons((unsigned short)port);
        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
        if (listen(fd, backlog) < 0) { close(fd); return -1; }
        return fd;
    }
}

_Safe int net_accept(int listen_fd) {
    _Unsafe { return accept(listen_fd, NULL, NULL); }
}

_Safe ssize_t net_recv(int fd, char* _Nonnull buf, size_t cap) {
    _Unsafe {
        if (cap == 0) { return -1; }
        ssize_t n = recv(fd, buf, cap - 1, 0);
        if (n < 0) { return -1; }
        buf[n] = '\0';
        return n;
    }
}

_Safe int net_send_all(int fd, const char* _Nonnull buf, size_t len) {
    _Unsafe {
        size_t sent = 0;
        while (sent < len) {
            ssize_t n = send(fd, buf + sent, len - sent, 0);
            if (n <= 0) { return -1; }
            sent += (size_t)n;
        }
        return 0;
    }
}

_Safe void net_close(int fd) { _Unsafe { close(fd); } }
```

- [ ] **步骤 3: 语法检查** —
`/home/zly/bsc/llvm-project/build/bin/clang -Wall -Wextra -Wno-nullability-completeness -I/home/zly/bsc/llvm-project/install/include/libcbs -fsyntax-only src/platform/net.cbs`
预期：无错误。

- [ ] **步骤 4: 提交**
```bash
git add include/platform/net.hbs src/platform/net.cbs
git commit -m "feat: POSIX socket wrappers (_Unsafe FFI seam)"
```

---

## Task 8：platform/fs —— 读文件到 owned 缓冲（`_Unsafe` FFI 接缝）

**文件:** 创建 `include/platform/fs.hbs`, `src/platform/fs.cbs`

- [ ] **步骤 1: 写 `include/platform/fs.hbs`**

```c
#ifndef CC_FS_HBS
#define CC_FS_HBS
#include <stddef.h>
#include "bishengc_safety.hbs"
// 读整个文件到 owned 缓冲（NUL 结尾）；长度经 out_len 返回；失败返回 nullptr。
// 调用方负责 safe_free_array。
_Safe char *_Owned _ArrayElem _Nullable fs_read_file(const char* _Nonnull path, size_t* _Borrow out_len);
#endif
```

- [ ] **步骤 2: 写 `src/platform/fs.cbs`**

```c
#include "../../include/platform/fs.hbs"
#include <stdio.h>

_Safe char *_Owned _ArrayElem _Nullable fs_read_file(const char* _Nonnull path, size_t* _Borrow out_len) {
    *out_len = 0;
    char *_Owned _ArrayElem _Nullable result = nullptr;
    _Unsafe {
        FILE* f = fopen(path, "rb");
        if (f == NULL) { return nullptr; }       // result 仍为 nullptr，无泄漏
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0) { fclose(f); return nullptr; }
        // safe_malloc_array 是 _Safe 函数，可在 _Unsafe 块内调用
        char *_Owned _ArrayElem buf = safe_malloc_array((size_t)sz + 1, (char)0);
        size_t pos = 0;
        char chunk[4096];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
            for (size_t i = 0; i < n && pos < (size_t)sz; i++) { buf[pos] = chunk[i]; pos++; }
        }
        fclose(f);
        buf[pos] = '\0';
        *out_len = pos;
        result = buf;        // 把 buf 移入 result
    }
    return result;
}
```

注意（执行者）：`buf` 在 `_Unsafe` 块内分配并移入块外声明的 `result`。若编译器对此 move 报错，
改为在 `_Unsafe` 块外声明 `result = nullptr`、块内 `result = buf;`（如上）。`buf[pos]` 下标写
owned `_ArrayElem` 在 `_Unsafe` 内合法；`chunk` 是栈 raw 数组、`fread` 写它合法。

- [ ] **步骤 3: 语法检查** — `-fsyntax-only src/platform/fs.cbs`（带 `$(INC)`）→ 无错误。

- [ ] **步骤 4: 写临时验证程序并运行**（确认读取+无泄漏，然后删除）

```bash
cat > /tmp/fs_check.cbs <<'EOF'
#include "bishengc_safety.hbs"
#include <stdio.h>
#include "src/platform/fs.cbs"
_Safe int main(void) {
    size_t n = 0;
    char *_Owned _ArrayElem _Nullable d = fs_read_file("config.ini", &_Mut n);
    if (d == nullptr) { _Unsafe{printf("read failed\n");} return 1; }
    char c0 = d[0];
    safe_free_array(d);
    _Unsafe { printf("read %zu bytes, first=%c\n", n, c0); }
    return 0;
}
EOF
/home/zly/bsc/llvm-project/build/bin/clang -Wall -Wno-nullability-completeness -I/home/zly/bsc/llvm-project/install/include/libcbs /tmp/fs_check.cbs -o /tmp/fs_check -L/home/zly/bsc/llvm-project/install/lib -lstdcbs && /tmp/fs_check
rm -f /tmp/fs_check.cbs /tmp/fs_check
```
预期：`read <N> bytes, first=#`（config.ini 以 `#` 开头）。

- [ ] **步骤 5: 提交**
```bash
git add include/platform/fs.hbs src/platform/fs.cbs
git commit -m "feat: fs_read_file -> owned _ArrayElem buffer (_Unsafe FFI seam)"
```

---

## Task 9：file_server —— 静态文件服务（业务 `_Safe`）

**文件:** 创建 `include/file_server.hbs`, `src/file_server.cbs`

构建磁盘路径 = `document_root` + 请求路径（`/` → `/index.html`），拒绝不安全路径（→404），
经 `fs` 读取（→200 带 MIME），缺失 →404。

- [ ] **步骤 1: 写 `include/file_server.hbs`**

```c
#ifndef CC_FILE_SERVER_HBS
#define CC_FILE_SERVER_HBS
#include "../include/http_request.hbs"
#include "../include/http_response.hbs"
#include "../include/config.hbs"
// 为 req 在 config->document_root 下提供静态文件。总是返回 Response。
_Safe Response serve_static(const struct Config* _Borrow config, const Request* _Borrow req);
#endif
```

- [ ] **步骤 2: 写 `src/file_server.cbs`**

```c
#include "../include/file_server.hbs"
#include "../include/str_util.hbs"
#include "../include/router.hbs"      // path_is_safe
#include "../include/mime.hbs"
#include "../include/platform/fs.hbs"

_Safe Response serve_static(const struct Config* _Borrow config, const Request* _Borrow req) {
    // 相对路径：'/' -> '/index.html'
    char rel[1100] = {0};
    if (cbuf_eq(req->path, "/")) { cbuf_copy(rel, sizeof(rel), "/index.html"); }
    else { cbuf_copy(rel, sizeof(rel), req->path); }

    if (!path_is_safe(rel)) { return response_not_found(); }

    // 完整路径 = document_root + rel
    char full[1400] = {0};
    cbuf_copy(full, sizeof(full), config->document_root);
    size_t fl = cstr_len(full);
    for (size_t i = 0; rel[i] != '\0' && fl < sizeof(full) - 1; i++) { full[fl] = rel[i]; fl++; }
    full[fl] = '\0';

    size_t flen = 0;
    char *_Owned _ArrayElem _Nullable data = fs_read_file(full, &_Mut flen);
    if (data == nullptr) { return response_not_found(); }
    // data 已收窄为非空；移入 Response
    return response_with_owned(200, "OK", mime_for_path(full), data, flen);
}
```

注意（执行者）：`fs_read_file` 返回 `_Nullable`；`if (data == nullptr) return ...;` 之后
nullability 收窄为非空，可移交 `response_with_owned`（取非空 `_Owned _ArrayElem`）。若收窄未被
接受，显式 `char *_Owned _ArrayElem nn = data;`（在 null 检查之后）再传 `nn`。

- [ ] **步骤 3: 语法检查** — `-fsyntax-only src/file_server.cbs`（带 `$(INC)`）→ 无错误。
按 `bsc-ownership`/`bsc-borrowing` 修正 `data` 的 move（这是有意移动；确保 move 后不再使用 `data`）。

- [ ] **步骤 4: 提交**
```bash
git add include/file_server.hbs src/file_server.cbs
git commit -m "feat: static file serving (traversal guard + MIME + owned body)"
```

---

## Task 10：handler —— 纯函数式内核（TDD）

**文件:** 创建 `include/handler.hbs`, `src/handler.cbs`, `tests/test_handler.cbs`

`handle_request(cfg, router, raw) → Response`：解析→若畸形 400→匹配动态路由→否则静态文件。
无 socket、可单测。

- [ ] **步骤 1: 写失败测试** `tests/test_handler.cbs`

```c
#include "bishengc_safety.hbs"
#include <stdio.h>
#include <string.h>
#include "../src/str_util.cbs"
#include "../src/mime.cbs"
#include "../src/http_request.cbs"
#include "../src/http_response.cbs"
#include "../src/config.cbs"
#include "../src/router.cbs"
#include "../src/platform/fs.cbs"
#include "../src/file_server.cbs"
#include "../src/handler.cbs"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){_Unsafe{printf("FAIL: %s\n",m);}fails++;} }while(0)

static _Bool wire_contains(char *_Owned _ArrayElem wire, size_t len, const char* _Nonnull needle) {
    char buf[8192] = {0};
    for (size_t i = 0; i < len && i < 8191; i++) { buf[i] = wire[i]; }
    _Unsafe { return strstr(buf, needle) != NULL; }
}

_Safe Response route_hello(const Request* _Borrow req) {
    return response_with_cstr(200, "OK", "text/html", "<h1>hello route</h1>");
}

_Safe int main(void) {
    struct Config cfg = config_default();
    struct Router rt = router_new();
    router_add(&_Mut rt, "GET", "/hello", route_hello);

    // 动态路由命中
    Response r1 = handle_request(&_Const cfg, &_Const rt, "GET /hello HTTP/1.1\r\n\r\n");
    size_t n1 = 0;
    char *_Owned _ArrayElem w1 = response_serialize(&_Const r1, &_Mut n1);
    CHECK(wire_contains(w1, n1, "hello route"), "dynamic route body");
    safe_free_array(w1);

    // 畸形 -> 400
    Response r2 = handle_request(&_Const cfg, &_Const rt, "garbage");
    CHECK(r2.status == 400, "malformed -> 400");

    if (fails) { _Unsafe{printf("%d failures\n", fails);} return 1; }
    _Unsafe{printf("test_handler OK\n");}
    return 0;
}
```

- [ ] **步骤 2: 验证失败** — `make test-handler` → FAIL。

- [ ] **步骤 3: 写 `include/handler.hbs`**

```c
#ifndef CC_HANDLER_HBS
#define CC_HANDLER_HBS
#include "../include/http_response.hbs"
#include "../include/config.hbs"
#include "../include/router.hbs"
// 纯请求处理：解析 raw、路由、生成 Response。无 socket。
_Safe Response handle_request(const struct Config* _Borrow cfg, const struct Router* _Borrow router,
                              const char* _Nonnull raw);
#endif
```

- [ ] **步骤 4: 写 `src/handler.cbs`**

```c
#include "../include/handler.hbs"
#include "../include/http_request.hbs"
#include "../include/file_server.hbs"

_Safe Response handle_request(const struct Config* _Borrow cfg, const struct Router* _Borrow router,
                              const char* _Nonnull raw) {
    Request req = request_parse(raw);
    if (!req.ok) { return response_bad_request(); }       // req 析构释放 body
    Handler h = router_match(router, &_Const req);
    if (h != nullptr) { return h(&_Const req); }          // 命中动态路由
    return serve_static(cfg, &_Const req);                // 否则静态文件
}
```

注意（执行者）：不要裸声明 `Response resp;`（`_Owned struct` 安全区禁止未初始化声明）——直接
`return` 各分支结果。`req` 在每条 `return` 路径上于函数返回时自动析构。

- [ ] **步骤 5: 验证通过** — `make test-handler` → PASS。

- [ ] **步骤 6: 提交**
```bash
git add include/handler.hbs src/handler.cbs tests/test_handler.cbs
git commit -m "feat: handle_request pure functional core"
```

---

## Task 11：platform/log + thread_pool（`_Unsafe` 接缝）

**文件:** 创建 `include/platform/log.hbs`, `src/platform/log.cbs`, `include/platform/thread_pool.hbs`, `src/platform/thread_pool.cbs`

固定 N 个 worker 从 mutex+condvar 队列取客户端 fd（纯 `int`）。只有 `int` 跨线程边界。
`ThreadPool` 是**纯 `struct`**（POD + pthread 原语），整体在 `_Unsafe` 接缝下审计。

- [ ] **步骤 1: 写 `include/platform/log.hbs`**

```c
#ifndef CC_LOG_HBS
#define CC_LOG_HBS
// 记录一次请求行（方法 路径 -> 状态）到 stderr。
_Safe void log_request(const char* _Nonnull method, const char* _Nonnull path, int status);
// 记录一条诊断信息到 stderr。
_Safe void log_msg(const char* _Nonnull msg);
#endif
```

- [ ] **步骤 2: 写 `src/platform/log.cbs`**

```c
#include "../../include/platform/log.hbs"
#include <stdio.h>

_Safe void log_request(const char* _Nonnull method, const char* _Nonnull path, int status) {
    _Unsafe { fprintf(stderr, "%s %s -> %d\n", method, path, status); }
}
_Safe void log_msg(const char* _Nonnull msg) {
    _Unsafe { fprintf(stderr, "%s\n", msg); }
}
```

- [ ] **步骤 3: 写 `include/platform/thread_pool.hbs`**

```c
#ifndef CC_THREAD_POOL_HBS
#define CC_THREAD_POOL_HBS
#include <stddef.h>
#include <pthread.h>

typedef void (*ConnHandler)(int client_fd, void* _Nonnull ctx);

#define TP_QUEUE_CAP 256
#define TP_MAX_WORKERS 64

struct ThreadPool {
    pthread_t workers[TP_MAX_WORKERS];
    int n_workers;
    int queue[TP_QUEUE_CAP];
    int head, tail, count;
    int stop;
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    ConnHandler handler;
    void* ctx;
};

// 初始化并启动 worker。成功返回 0。
_Safe int  thread_pool_start(struct ThreadPool* _Nonnull tp, int n_workers, ConnHandler handler, void* _Nonnull ctx);
// 入队一个客户端 fd（队满则关闭并丢弃，避免阻塞 accept 循环）。
_Safe void thread_pool_submit(struct ThreadPool* _Nonnull tp, int client_fd);
// 置位 stop 并 join 所有 worker。
_Safe void thread_pool_shutdown(struct ThreadPool* _Nonnull tp);
#endif
```

- [ ] **步骤 4: 写 `src/platform/thread_pool.cbs`**

```c
#include "../../include/platform/thread_pool.hbs"
#include <unistd.h>

static void* tp_worker(void* arg) {
    _Unsafe {
        struct ThreadPool* tp = (struct ThreadPool*)arg;
        for (;;) {
            pthread_mutex_lock(&tp->mtx);
            while (tp->count == 0 && !tp->stop) {
                pthread_cond_wait(&tp->not_empty, &tp->mtx);
            }
            if (tp->stop && tp->count == 0) { pthread_mutex_unlock(&tp->mtx); break; }
            int fd = tp->queue[tp->head];
            tp->head = (tp->head + 1) % TP_QUEUE_CAP;
            tp->count--;
            pthread_mutex_unlock(&tp->mtx);
            tp->handler(fd, tp->ctx);
        }
        return NULL;
    }
}

_Safe int thread_pool_start(struct ThreadPool* _Nonnull tp, int n_workers, ConnHandler handler, void* _Nonnull ctx) {
    _Unsafe {
        if (n_workers > TP_MAX_WORKERS) { n_workers = TP_MAX_WORKERS; }
        tp->n_workers = n_workers;
        tp->head = 0; tp->tail = 0; tp->count = 0; tp->stop = 0;
        tp->handler = handler; tp->ctx = ctx;
        pthread_mutex_init(&tp->mtx, NULL);
        pthread_cond_init(&tp->not_empty, NULL);
        for (int i = 0; i < n_workers; i++) {
            if (pthread_create(&tp->workers[i], NULL, tp_worker, tp) != 0) { return -1; }
        }
        return 0;
    }
}

_Safe void thread_pool_submit(struct ThreadPool* _Nonnull tp, int client_fd) {
    _Unsafe {
        pthread_mutex_lock(&tp->mtx);
        if (tp->count == TP_QUEUE_CAP) {            // 队满：关闭并丢弃以免阻塞 accept
            pthread_mutex_unlock(&tp->mtx);
            close(client_fd);
            return;
        }
        tp->queue[tp->tail] = client_fd;
        tp->tail = (tp->tail + 1) % TP_QUEUE_CAP;
        tp->count++;
        pthread_cond_signal(&tp->not_empty);
        pthread_mutex_unlock(&tp->mtx);
    }
}

_Safe void thread_pool_shutdown(struct ThreadPool* _Nonnull tp) {
    _Unsafe {
        pthread_mutex_lock(&tp->mtx);
        tp->stop = 1;
        pthread_cond_broadcast(&tp->not_empty);
        pthread_mutex_unlock(&tp->mtx);
        for (int i = 0; i < tp->n_workers; i++) { pthread_join(tp->workers[i], NULL); }
        pthread_mutex_destroy(&tp->mtx);
        pthread_cond_destroy(&tp->not_empty);
    }
}
```

- [ ] **步骤 5: 语法检查** — `-fsyntax-only src/platform/log.cbs` 与 `src/platform/thread_pool.cbs` → 无错误。

- [ ] **步骤 6: 提交**
```bash
git add include/platform/log.hbs src/platform/log.cbs include/platform/thread_pool.hbs src/platform/thread_pool.cbs
git commit -m "feat: log + pthread thread pool (int fd queue)"
```

---

## Task 12：platform/runtime + main —— 接线（accept 循环 + 连接处理 + 入口）

**文件:** 创建 `include/platform/runtime.hbs`, `src/platform/runtime.cbs`, `src/main.cbs`

`runtime` 持有 `ServerCtx`（config + router，启动后只读）、连接处理器（recv → handle_request →
serialize → send → close）、accept 循环。`main` 是聚合器。

- [ ] **步骤 1: 写 `include/platform/runtime.hbs`**

```c
#ifndef CC_RUNTIME_HBS
#define CC_RUNTIME_HBS
#include "../include/config.hbs"
#include "../include/router.hbs"

struct ServerCtx { struct Config config; struct Router router; };

// 运行 accept 循环：绑定端口、启动线程池、accept→submit。绑定失败返回 -1。
// config/router 按值拷入进程级 static 上下文，供 worker 只读。
_Safe int server_run(const struct Config* _Borrow config, const struct Router* _Borrow router);
#endif
```

- [ ] **步骤 2: 写 `src/platform/runtime.cbs`**

```c
#include "../../include/platform/runtime.hbs"
#include "../../include/platform/net.hbs"
#include "../../include/platform/log.hbs"
#include "../../include/platform/thread_pool.hbs"
#include "../../include/handler.hbs"
#include "../../include/http_request.hbs"
#include "../../include/http_response.hbs"
#include "../../include/str_util.hbs"

#define REQ_BUF 8192

// 进程级共享只读上下文（按值持有；无 owned 数据）。
static struct ServerCtx g_ctx;

// 处理一个连接：读请求、处理、响应、关闭。运行在 worker 线程。
_Safe void handle_conn(int fd, void* _Nonnull ctx_raw) {
    struct ServerCtx* ctx = _Unsafe((struct ServerCtx*)ctx_raw);
    char buf[REQ_BUF] = {0};
    ssize_t n = net_recv(fd, buf, REQ_BUF);
    if (n <= 0) { net_close(fd); return; }

    // 从原始 ctx 重建借用（事实 11）
    const struct Config* _Borrow cfg = _Unsafe(&_Const ctx->config);
    const struct Router* _Borrow rt  = _Unsafe(&_Const ctx->router);

    Response resp = handle_request(cfg, rt, buf);
    size_t wlen = 0;
    char *_Owned _ArrayElem wire = response_serialize(&_Const resp, &_Mut wlen);

    // 分段发送：用下标从 owned 报文读入栈缓冲（避免 _Borrow/_Owned -> raw 转换）
    size_t off = 0;
    while (off < wlen) {
        char out[4096] = {0};
        size_t m = 0;
        while (off < wlen && m < sizeof(out)) { out[m] = wire[off]; m++; off++; }
        if (net_send_all(fd, out, m) != 0) { break; }
    }
    safe_free_array(wire);
    log_request("conn", "", resp.status);   // 简单请求日志；状态码足够诊断
    net_close(fd);
}   // resp 在此析构（释放 resp.body）

_Safe int server_run(const struct Config* _Borrow config, const struct Router* _Borrow router) {
    int listen_fd = net_listen(config->port, 128);
    if (listen_fd < 0) { return -1; }
    // 把 config/router 拷入进程级 static 上下文（纯 struct，按值拷贝）
    g_ctx.config = *config;
    g_ctx.router = *router;
    void* _Nonnull ctxp = _Unsafe((void* _Nonnull)&g_ctx);

    struct ThreadPool pool;
    if (thread_pool_start(&pool, config->threads, handle_conn, ctxp) != 0) { return -1; }
    log_msg("cc_httpd listening");
    for (;;) {
        int client = net_accept(listen_fd);
        if (client < 0) { continue; }
        thread_pool_submit(&pool, client);
    }
    return 0;
}
```

注意（执行者）：
1. **请求日志（可选增强）**：上面用 `log_request("conn", "", resp.status)` 仅记录状态码，可编译且
   够诊断。若想记录 method/path，在 `net_recv` 之后另行 `Request rq = request_parse(buf);` 取
   `rq.method`/`rq.path` 传入 `log_request`（注意 `rq` 也会析构其 body）。日志内容不在验收范围。
2. `g_ctx.config = *config;` —— `*config` 是对 `_Borrow` 的解引用读取（纯 struct 可复制），
   赋给 static。`config`/`router` 为 `_Borrow`，解引用读取在 `_Safe` 合法（事实 2 仅禁止裸 `*rawptr`，
   `_Borrow` 解引用允许）。若 static 赋值在 `_Safe` 被拒，用 `_Unsafe { g_ctx.config = *config; }`。

- [ ] **步骤 3: 写 `src/main.cbs`**（唯一编译的 TU —— 按依赖顺序 include 所有 `.cbs`）

```c
// cc_httpd 入口 —— 单翻译单元聚合器（仅所有权版）。
#include "str_util.cbs"
#include "mime.cbs"
#include "http_request.cbs"
#include "http_response.cbs"
#include "config.cbs"
#include "router.cbs"
#include "platform/net.cbs"
#include "platform/fs.cbs"
#include "file_server.cbs"
#include "handler.cbs"
#include "platform/log.cbs"
#include "platform/thread_pool.cbs"
#include "platform/runtime.cbs"

// 演示动态路由。
_Safe Response route_hello(const Request* _Borrow req) {
    return response_with_cstr(200, "OK", "text/html", "<h1>Hello from a cc_httpd route!</h1>");
}

_Safe int main(void) {
    // 经 fs 适配器读取配置文本，再解析
    size_t clen = 0;
    char *_Owned _ArrayElem _Nullable ctext = fs_read_file("config.ini", &_Mut clen);
    struct Config config;
    if (ctext != nullptr) {
        // ctext 收窄为非空；config_parse 取 const char*（owned _ArrayElem 下标读取首元素借用）
        config = config_parse(&_Const ctext[0]);
        safe_free_array(ctext);
    } else {
        config = config_default();
    }

    struct Router router = router_new();
    router_add(&_Mut router, "GET", "/hello", route_hello);
    return server_run(&_Const config, &_Const router);
}
```

注意（执行者）：`config_parse(&_Const ctext[0])` —— `config_parse` 形参是 `const char* _Nonnull`；
`&_Const ctext[0]` 取 owned 缓冲首元素的借用（`const char *_Borrow _ArrayElem`，隐式降级为
`const char *_Borrow`，再隐式适配到 `const char*` 形参）。若该隐式适配被拒，改为先把 `ctext`
逐字节拷到栈 `char cbuf[8192]` 再 `config_parse(cbuf)`。`struct Config config;` 是纯 struct
（无 owned/指针字段），可裸声明后赋值（事实 9 仅要求含指针/owned 字段的结构完整初始化）。

- [ ] **步骤 4: 构建整个服务器** — `make`
预期：`bin/httpd` 无错误构建。按相关 BSC 技能修复任何 borrow/init/move 诊断。常见修复：确保
move 后不再使用被移动值（`wire`、`ctext`、`data`）；确保 `_Owned struct` 在使用前完整初始化。

- [ ] **步骤 5: 冒烟运行** — `./bin/httpd &` 然后 `curl -s localhost:8080/` → 返回 index.html；`kill %1`。

- [ ] **步骤 6: 提交**
```bash
git add include/platform/runtime.hbs src/platform/runtime.cbs src/main.cbs
git commit -m "feat: runtime accept loop + conn handler + main entry"
```

---

## Task 13：no-_Unsafe 守卫 + 集成测试（全部验收标准）

**文件:** 创建 `tests/check_no_unsafe.sh`, `tests/run_integration.sh`

- [ ] **步骤 1: 写 `tests/check_no_unsafe.sh`**（业务 `src/*.cbs` 不得含 `_Unsafe`）

```bash
#!/usr/bin/env bash
# 业务核心 src/*.cbs（不含 src/platform/）禁止出现 _Unsafe。
set -u
viol=0
for f in src/*.cbs; do
    if grep -n '_Unsafe' "$f" >/dev/null 2>&1; then
        echo "FAIL: _Unsafe found in business core: $f"
        grep -n '_Unsafe' "$f"
        viol=1
    fi
done
if [ "$viol" = "0" ]; then echo "ok: no _Unsafe in business core"; fi
exit $viol
```

- [ ] **步骤 2: 运行守卫** — `bash tests/check_no_unsafe.sh`
预期：`ok: no _Unsafe in business core`，exit 0。（若有业务文件含 `_Unsafe`，把该不安全逻辑下沉到
`src/platform/` 适配器。）

- [ ] **步骤 3: 写 `tests/run_integration.sh`**

```bash
#!/usr/bin/env bash
set -u
make >/dev/null || { echo "BUILD FAILED"; exit 1; }
./bin/httpd >/tmp/cc_httpd.log 2>&1 &
SRV=$!
sleep 0.5
fail=0
chk() { if [ "$1" != "$2" ]; then echo "FAIL: $3 (got '$1' want '$2')"; fail=1; else echo "ok: $3"; fi; }

# AC-2 静态 200
code=$(curl -s -o /dev/null -w '%{http_code}' localhost:8080/)
chk "$code" "200" "AC-2 GET / returns 200"
ctype=$(curl -s -D - -o /dev/null localhost:8080/ | grep -i '^content-type' | tr -d '\r' | awk '{print $2}')
chk "$ctype" "text/html" "AC-2 index Content-Type"

# AC-3 404
code=$(curl -s -o /dev/null -w '%{http_code}' localhost:8080/nope.html)
chk "$code" "404" "AC-3 missing file 404"

# AC-4 动态路由
body=$(curl -s localhost:8080/hello)
case "$body" in *"cc_httpd route"*) echo "ok: AC-4 /hello body";; *) echo "FAIL: AC-4 /hello body"; fail=1;; esac

# 路径穿越 -> 404
code=$(curl -s -o /dev/null -w '%{http_code}' --path-as-is localhost:8080/../Makefile)
chk "$code" "404" "traversal blocked"

# AC-5 并发：50 个并行请求全部 200
for i in $(seq 1 50); do
  ( [ "$(curl -s -o /dev/null -w '%{http_code}' localhost:8080/)" = "200" ] && echo y ) &
done > /tmp/cc_conc.out
wait
ok=$(grep -c y /tmp/cc_conc.out 2>/dev/null || echo 0)
chk "$ok" "50" "AC-5 50 concurrent requests"

kill $SRV 2>/dev/null
exit $fail
```

- [ ] **步骤 4: 运行** — `bash tests/run_integration.sh`
预期：每行 `ok:`；exit 0。修复服务器 bug 直到全部通过。

- [ ] **步骤 5: 提交**
```bash
git add tests/check_no_unsafe.sh tests/run_integration.sh
git commit -m "test: no-_Unsafe guard + integration tests (AC-2..AC-5 + traversal)"
```

---

## Task 14：内存安全检查 + 收尾

- [ ] **步骤 1: 全部单测过一遍**
```bash
for t in str_util mime http_request http_response config router path_guard handler; do
    make test-$t || { echo "UNIT FAIL: $t"; exit 1; }
done
```
预期：每个打印 `test_X OK`。

- [ ] **步骤 2: Valgrind 单测（AC-6 的可控部分）**
```bash
make test-handler
valgrind --leak-check=full --error-exitcode=99 ./bin/test_handler
```
预期：`ERROR SUMMARY: 0 errors`，无 "definitely lost"。单测覆盖完整的 Request/Response 体生命周期
（解析→序列化→析构）——这是稳态每请求路径，是内存安全的关键证据。若有泄漏，按 `bsc-ownership` 修复
（多半是某个临时 owned 缓冲未 `safe_free_array`，或 owned struct 字段未在析构释放）。

- [ ] **步骤 3: 零警告构建** — `make clean && make`
预期：除被抑制的 libcbs nullability 警告外无警告。处理来自我们代码的任何警告。

- [ ] **步骤 4: 更新 CLAUDE.md 编译段**（若存在）—— 记录已验证工具链（CC/INC/LIB/FLAGS，
`make`、`make test-X`、`make smoke`），并注明「仅所有权：无 libcbs 容器 / 无成员函数 / 无 trait」。

- [ ] **步骤 5: 最终提交**
```bash
git add -A
git commit -m "docs: record verified ownership-only compile command + memory-safety notes"
```

---

## 自审覆盖图

- **FR-1 listen/accept** → Task 7 net + Task 12 server_run。
- **FR-2 解析请求** → Task 3。  **FR-3 GET 静态/动态** → Task 9、6、10。
- **FR-4 状态码** → Task 4（200/400/404/500）。  **FR-5 MIME** → Task 2。
- **FR-6 路由注册** → Task 6。  **NFR-1 并发** → Task 11 + AC-5 测试。
- **NFR-2 内存安全** → 全程 `_Owned` 缓冲 + RAII 析构 + Task 14 valgrind。
- **NFR-4 配置** → Task 5 + Task 12 经 fs 读取。
- **AC-1..AC-6** → Task 13 + Task 14。
- **路径穿越** → Task 6 `path_is_safe` + Task 9 + Task 13 测试。
- **仅所有权约束** → 无 `string.hbs`/`vec.hbs`（全程）；自由函数（无成员函数）；无 trait；
  定长 `char[]` + `_Owned _ArrayElem`；Task 13 `check_no_unsafe.sh` 守业务核心纯净。

## 执行者风险提示

- **裸 `_Owned _ArrayElem` 不自动析构**（事实 5）——这是最易引入泄漏处。每个临时 owned 缓冲都要
  `safe_free_array` / move / return。Valgrind（Task 14）会抓住遗漏。
- **owned 字段构造后不可重新赋值**（事实 6）——用 `safe_swap` + 释放换出值（见 `request_parse`）。
- **借用 owned 缓冲给辅助函数**：用 `&_Mut buf[0]` / `&_Const buf[0]`（事实 7），不要直接传指针。
- **owned 传 `printf` = 移动**（事实 8）——读取打印用 `&_Const buf[0]`，或先拷到栈缓冲。
- **跨线程不传 `_Borrow`/`_Owned`**——只传 `int` fd；worker 用 `_Unsafe(&_Const ctx->field)` 重建借用。
- 若 borrow 检查器报错不清晰，先用 LSP `hover`（见 `bsc-lsp`）或查 `bsc-errors`，**不要**为绕过检查器
  而扩大 `_Unsafe` 面。
