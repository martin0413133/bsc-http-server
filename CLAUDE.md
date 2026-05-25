# cc_httpd — BiSheng C HTTP 服务器（仅所有权版）

内存安全的 HTTP/1.1 服务器：静态文件 + 动态路由 + pthread 线程池。

## 已验证编译工具链（精确使用）

```makefile
CC    := /home/zly/bsc/llvm-project/build/bin/clang
INC   := -I/home/zly/bsc/llvm-project/install/include/libcbs
LIB   := -L/home/zly/bsc/llvm-project/install/lib -lstdcbs -lpthread
FLAGS := -Wall -Wextra -Wno-nullability-completeness -g
```

- `make`           构建 `bin/httpd`（`src/main.cbs` 为唯一 TU，按依赖顺序 `#include` 各 `.cbs`）
- `make test-X`    构建并运行 `tests/test_X.cbs`（X ∈ str_util mime http_request http_response config router path_guard handler）
- `make smoke`     编译并运行 `tests/smoke.cbs`
- `bash tests/check_no_unsafe.sh`   断言业务核心 `src/*.cbs`（不含 `src/platform/`）无 `_Unsafe`
- `bash tests/run_integration.sh`   起服务跑 curl 验收（AC-2..AC-5 + 路径穿越）

仍需 `-I…/libcbs`（`bishengc_safety.hbs`）与 `-lstdcbs`（`safe_malloc*` 实现）——这是所有权运行时，
**不是** libcbs 容器。**不要** `#include "string.hbs"`/`"vec.hbs"`。

## 仅所有权约束（本项目特有）

无 libcbs 容器（String/Vec）、无成员函数、无 trait、不为自定义类型用泛型，**且不使用 `_Owned struct`/析构函数**。

持有堆内存的类型用**普通 `struct` + `char *_Owned _ArrayElem` 字段 + 显式 `*_free(struct T r)` 函数**
（按值移入后 `safe_free_array` 字段）。**无自动 RAII**——每条返回路径都要显式 `*_free`/move/return。
见 `handle_request`（每条路径 `request_free(req)`）与 `handle_conn`（`response_free(resp)`）。

## 踩过的 BSC 坑（实测）

1. **安全区不能把借用结构里的指针字段拷成独立指针**（`const char *const` → `const char *` 被禁）。
   故 `Response.status_text`/`content_type` 用定长 `char[]` 而非 `const char*`——数组字段经借用穿透传参是允许的。
2. **函数指针须带 `_Safe` 才能在安全区调用**：`typedef _Safe struct Response (*Handler)(...)`。
3. **借用不能转裸 `const char*` 形参**：`main` 把 config 文本从 owned 缓冲拷到栈 `char[]` 再 `config_parse`。
4. 返回字面量的函数标 `_Nonnull`（如 `mime_for_path`），否则传给 `_Nonnull` 形参报 nullable。
5. `include/platform/*.hbs` 引用 `include/` 同级头用 `../x.hbs`（不是 `../include/x.hbs`）。
6. 集成脚本对并发请求只能 `wait $cpids`（裸 `wait` 会等永不退出的服务器进程）。

## 分层

- 业务核心（`src/*.cbs`）100% `_Safe`、无 `_Unsafe`，单测验证。
- IO/线程（`src/platform/*.cbs`）用最小 `_Unsafe` FFI 接缝，集成测试验证。
