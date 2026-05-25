#include "net.h"
#include "bishengc_safety.hbs"
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

// --- _Safe declarations for external C functions ---

// OpenSSL: annotated with _Borrow for borrows, _Owned for ownership transfer
_Safe const SSL_METHOD* TLS_server_method(void);
_Safe SSL_CTX *_Owned SSL_CTX_new(const SSL_METHOD* method);
_Safe int      SSL_CTX_use_certificate_file(SSL_CTX* _Borrow ctx, const char* _Nonnull file, int type);
_Safe int      SSL_CTX_use_PrivateKey_file(SSL_CTX* _Borrow ctx, const char* _Nonnull file, int type);
_Safe int      SSL_CTX_check_private_key(const SSL_CTX* _Borrow ctx);
_Safe SSL     *_Owned SSL_new(SSL_CTX* _Borrow ctx);
_Safe int      SSL_set_fd(SSL* _Borrow ssl, int fd);
_Safe int      SSL_accept(SSL* _Borrow ssl);
_Safe int      SSL_read(SSL* _Borrow ssl, void* _Borrow buf, int num);
_Safe int      SSL_write(SSL* _Borrow ssl, const void* _Borrow buf, int num);
_Safe int      SSL_shutdown(SSL* _Borrow ssl);
_Safe void     SSL_CTX_free(SSL_CTX *_Owned ctx);
_Safe void     SSL_free(SSL *_Owned ssl);

// POSIX sockets: annotated with _Borrow/_Nullable per resource semantics
_Safe int      socket(int domain, int type, int protocol);
_Safe int      setsockopt(int fd, int level, int optname, const void* _Borrow optval, socklen_t optlen);
_Safe int      listen(int fd, int backlog);
_Safe int      accept(int fd, struct sockaddr* _Nullable addr, socklen_t* _Nullable addrlen);
_Safe ssize_t  recv(int fd, void* _Borrow buf, size_t len, int flags);
_Safe ssize_t  send(int fd, const void* _Borrow buf, size_t len, int flags);
_Safe int      close(int fd);
_Safe void*    memset(void* _Borrow s, int c, size_t n);

// POSIX byte-swap (pure functions, no pointers)
_Safe uint32_t htonl(uint32_t hostlong);
_Safe uint16_t htons(uint16_t hostshort);

#define NET_RECV_BUF 8192
#define NET_SEND_CHUNK 4096
#define NET_TIMEOUT_SEC 10

_Safe int net_listen(int port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { return -1; }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &_Const opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    // _Unsafe: sockaddr_in→sockaddr struct cast + void* intermediate (BSC forbids cross-struct borrow casts)
    _Unsafe { if (bind(fd, (struct sockaddr*)(void*)&_Mut addr, sizeof(addr)) < 0) { close(fd); return -1; } }
    if (listen(fd, backlog) < 0) { close(fd); return -1; }
    return fd;
}

_Safe int net_accept(int listen_fd) {
    int fd = accept(listen_fd, nullptr, nullptr);
    if (fd >= 0) {
        struct timeval tv;
        tv.tv_sec = NET_TIMEOUT_SEC;
        tv.tv_usec = 0;
        // _Unsafe: struct timeval*→const void* _Borrow conversion forbidden across struct types
        _Unsafe {
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }
    }
    return fd;
}

_Safe _Bool net_recv_string(int fd, cstring* _Borrow out) {
    char buf[NET_RECV_BUF] = {0};
    ssize_t n = recv(fd, &_Mut buf, NET_RECV_BUF - 1, 0);
    if (n <= 0) { return 0; }
    size_t got = (size_t)n;
    for (size_t i = 0; i < got; i++) { cstring_push(out, buf[i]); }
    return 1;
}

_Safe _Bool net_send_string(int fd, const cstring* _Borrow data) {
    size_t len = cstring_len(data);
    size_t off = 0;
    while (off < len) {
        char chunk[NET_SEND_CHUNK] = {0};
        size_t m = 0;
        while (off < len && m < NET_SEND_CHUNK) { chunk[m] = cstring_at(data, off); m++; off++; }
        size_t sent = 0;
        while (sent < m) {
            ssize_t s = send(fd, &_Const (chunk[sent]), m - sent, 0);
            if (s <= 0) { return 0; }
            sent += (size_t)s;
        }
    }
    return 1;
}

_Safe void net_close(int fd) { close(fd); }

// --- SSL support ---

_Safe SSLCtx *_Owned _Nullable net_ssl_ctx_new(const char* _Nonnull cert_file,
                                                const char* _Nonnull key_file) {
    SSL_CTX *_Owned raw = SSL_CTX_new(TLS_server_method());
    if (!raw) { return nullptr; }

    // SSL_CTX_set_mode is a macro — must stay _Unsafe
    // _Unsafe: SSL_CTX_set_mode is a macro expanding to SSL_CTX_ctrl — cannot be declared _Safe
    _Unsafe { SSL_CTX_set_mode((SSL_CTX *)&_Mut *raw, SSL_MODE_AUTO_RETRY); }
    if (SSL_CTX_use_certificate_file(&_Mut *raw, cert_file, SSL_FILETYPE_PEM) <= 0
        || SSL_CTX_use_PrivateKey_file(&_Mut *raw, key_file, SSL_FILETYPE_PEM) <= 0
        || SSL_CTX_check_private_key(&_Const *raw) <= 0) {
        SSL_CTX_free(raw);
        return nullptr;
    }

    SSLCtx val = { .raw_ctx = raw };
    SSLCtx *_Owned p = safe_malloc(val);
    return p;
}

_Safe SSLConn *_Owned _Nullable net_ssl_accept(const SSLCtx* _Borrow ctx, int fd) {
    SSL_CTX* _Borrow b_ctx = &_Mut *ctx->raw_ctx;
    SSL *_Owned raw_ssl = SSL_new(b_ctx);
    if (!raw_ssl) { return nullptr; }

    SSL_set_fd(&_Mut *raw_ssl, fd);
    if (SSL_accept(&_Mut *raw_ssl) <= 0) {
        SSL_free(raw_ssl);
        return nullptr;
    }

    SSLConn val = { .raw_ssl = raw_ssl, .fd = fd };
    SSLConn *_Owned p = safe_malloc(val);
    return p;
}

_Safe _Bool net_ssl_recv_string(SSLConn* _Borrow ssl, cstring* _Borrow out) {
    char buf[NET_RECV_BUF] = {0};
    int n = SSL_read(&_Mut *ssl->raw_ssl, &_Mut buf, NET_RECV_BUF - 1);
    if (n <= 0) { return 0; }
    size_t got = (size_t)n;
    for (size_t i = 0; i < got; i++) { cstring_push(out, buf[i]); }
    return 1;
}

_Safe _Bool net_ssl_send_string(SSLConn* _Borrow ssl, const cstring* _Borrow data) {
    size_t len = cstring_len(data);
    size_t off = 0;
    while (off < len) {
        char chunk[NET_SEND_CHUNK] = {0};
        size_t m = 0;
        while (off < len && m < NET_SEND_CHUNK) { chunk[m] = cstring_at(data, off); m++; off++; }
        if (SSL_write(&_Mut *ssl->raw_ssl, &_Const chunk, (int)m) <= 0) { return 0; }
    }
    return 1;
}

_Safe void net_ssl_conn_free(SSLConn *_Owned _Nullable ssl) {
    if (!ssl) { return; }
    SSL_shutdown(&_Mut *ssl->raw_ssl);
    SSL *_Owned raw = ssl->raw_ssl;
    SSL_free(raw);
    close(ssl->fd);
    safe_free((void *_Owned)ssl);
}

_Safe void net_ssl_ctx_free(SSLCtx *_Owned _Nullable ctx) {
    if (!ctx) { return; }
    SSL_CTX *_Owned raw = ctx->raw_ctx;
    SSL_CTX_free(raw);
    safe_free((void *_Owned)ctx);
}
