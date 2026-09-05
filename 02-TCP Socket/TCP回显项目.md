---
type: topic
status: complete
created: 2026-08-25
updated: 2026-08-31
tags:
  - TCP
  - 实践
  - C语言
---

# TCP回显项目

本章把前面学过的 `socket()`、`bind()`、`listen()`、`accept()`、`connect()`、`send()` 和 `recv()` 串成一个可以运行的小项目。

## 学习目标

完成后，你应该能够：

- 画出 TCP 回显项目的客户端和服务端流程；
- 在 Linux/WSL2 和 Windows 上分别编译、运行 C 程序；
- 解释为什么 TCP 需要应用层消息边界；
- 正确处理 `send()` 的部分发送、`recv()` 的断开和错误；
- 根据“连接被拒绝、端口占用、程序卡住”等现象排查问题。

## 1. 先明确项目要做什么

“回显”就是服务器收到客户端发送的内容后，原样发回：

~~~text
客户端输入：hello TCP\n
        ↓
服务器收到：hello TCP\n
        ↓
客户端收到：hello TCP\n
~~~

本章约定一个非常简单的应用层协议：**一行文本以 `\n` 结束，服务器收到一行后原样回显，并关闭这次客户端连接**。客户端每次运行只发送一行；服务端可以继续等待下一个客户端。

这个约定很重要。TCP 只提供有序字节流，不知道“这一行”在哪里结束。`\n` 是我们自己规定的消息分隔符。

### 1.1 项目组成

~~~text
01-tcp-echo/
├─ server_linux.c
├─ client_linux.c
├─ server_windows.c
└─ client_windows.c
~~~

Linux 和 Windows 的协议完全相同，主要区别在 Socket 句柄、初始化、错误处理和关闭函数。

## 2. 整体执行流程

### 2.1 服务端

~~~text
socket()  创建监听 Socket
   ↓
bind()    绑定 127.0.0.1:8080
   ↓
listen()  进入监听状态
   ↓
accept()  得到一个“已连接 Socket”
   ↓
recv()    读取一行（直到 \n）
   ↓
send()    把这一行原样发回
   ↓
close() / closesocket() 关闭本次客户端连接
   ↓
回到 accept()，等待下一个客户端
~~~

监听 Socket 只负责接收新连接；`accept()` 返回的客户端 Socket 才负责 `recv()` 和 `send()`。把这两个 Socket 混淆，是初学者最常见的错误之一。

### 2.2 客户端

~~~text
socket()  创建客户端 Socket
   ↓
connect() 连接 127.0.0.1:8080
   ↓
fgets()   从终端读取一行
   ↓
send_all()发送完整的一行
   ↓
recv_line()读取服务器回显
   ↓
close() / closesocket()
~~~

## 3. 先理解两个辅助函数

### 3.1 为什么需要 `send_all()`

`send()` 的第三个参数是“希望发送的最大字节数”，返回值是**本次实际发送的字节数**。返回值可能小于请求长度，因此大数据必须循环发送：

~~~c
while (sent < length) {
    n = send(socket, data + sent, length - sent, 0);
    if (n > 0) sent += n;
}
~~~

`send()` 返回成功，只表示这些字节已经交给本机内核，并不表示对方应用程序已经处理。

### 3.2 为什么需要 `recv_line()`

一次 `recv()` 可能读到半行，也可能读到多行。不能把“一次 `send()` 对应一次 `recv()`”当作规则。本项目使用循环，每次把读到的字节放入缓冲区，遇到 `\n` 才认为一行完整。

示例中的函数为了便于学习每次读取 1 个字节，效率不高；真实程序通常一次读取一大块，再在缓存中查找换行符。这里优先展示边界判断和返回值处理。

## 4. Linux 版本

下面的代码适用于 Linux 或 WSL2。将两个代码块分别保存为 `server_linux.c` 和 `client_linux.c`。

### 4.1 Linux 服务端：`server_linux.c`

~~~c
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int send_all(int fd, const char *data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, data + sent, length - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;                 // 被信号打断，重试
        } else {
            return -1;                 // 连接错误或发送失败
        }
    }
    return 0;
}

// 成功返回行长度；0 表示对端关闭；-1 表示错误；-2 表示行太长
static int recv_line(int fd, char *buffer, size_t capacity) {
    size_t used = 0;
    while (used + 1 < capacity) {       // 留一个位置给 '\0'
        char ch;
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n > 0) {
            buffer[used++] = ch;
            if (ch == '\n') {
                buffer[used] = '\0';
                return (int)used;
            }
        } else if (n == 0) {
            buffer[used] = '\0';
            return used == 0 ? 0 : (int)used;
        } else if (errno != EINTR) {
            return -1;
        }
    }
    buffer[used] = '\0';
    return -2;
}

int main(void) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof reuse) < 0) {
        perror("setsockopt");
        close(listen_fd);
        return 1;
    }

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 只允许本机连接

    if (bind(listen_fd, (struct sockaddr *)&address,
             sizeof address) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }
    if (listen(listen_fd, 16) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("listening on 127.0.0.1:8080\n");
    for (;;) {
        struct sockaddr_in client_address = {0};
        socklen_t client_length = sizeof client_address;
        int client_fd = accept(listen_fd,
                               (struct sockaddr *)&client_address,
                               &client_length);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        char client_ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_address.sin_addr,
                  client_ip, sizeof client_ip);
        printf("client connected: %s:%u\n", client_ip,
               (unsigned)ntohs(client_address.sin_port));

        char buffer[1024];
        int result = recv_line(client_fd, buffer, sizeof buffer);
        if (result > 0) {
            printf("received %d bytes: %s", result, buffer);
            if (send_all(client_fd, buffer, (size_t)result) < 0)
                perror("send");
        } else if (result == 0) {
            printf("client closed without sending a line\n");
        } else if (result == -2) {
            fprintf(stderr, "line is too long (limit: 1023 bytes)\n");
        } else {
            perror("recv");
        }
        close(client_fd);               // 只关闭当前客户端连接
    }

    close(listen_fd);
    return 0;
}
~~~

### 4.2 Linux 客户端：`client_linux.c`

~~~c
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int send_all(int fd, const char *data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, data + sent, length - sent, 0);
        if (n > 0) sent += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else return -1;
    }
    return 0;
}

static int recv_line(int fd, char *buffer, size_t capacity) {
    size_t used = 0;
    while (used + 1 < capacity) {
        char ch;
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n > 0) {
            buffer[used++] = ch;
            if (ch == '\n') { buffer[used] = '\0'; return (int)used; }
        } else if (n == 0) {
            buffer[used] = '\0';
            return used == 0 ? 0 : (int)used;
        } else if (errno != EINTR) return -1;
    }
    buffer[used] = '\0';
    return -2;
}

int main(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    if (inet_pton(AF_INET, "127.0.0.1", &server.sin_addr) != 1) {
        fprintf(stderr, "invalid server address\n");
        close(fd); return 1;
    }
    if (connect(fd, (struct sockaddr *)&server, sizeof server) < 0) {
        perror("connect"); close(fd); return 1;
    }

    char message[1024];
    printf("输入一行文字：");
    if (fgets(message, sizeof message, stdin) == NULL) {
        close(fd); return 0;
    }
    size_t length = strlen(message);
    if (length == 0 || message[length - 1] != '\n') {
        if (length + 1 >= sizeof message) {
            fprintf(stderr, "输入太长\n"); close(fd); return 1;
        }
        message[length++] = '\n';
        message[length] = '\0';
    }
    if (send_all(fd, message, length) < 0) {
        perror("send"); close(fd); return 1;
    }

    char reply[1024];
    int result = recv_line(fd, reply, sizeof reply);
    if (result > 0) printf("服务器回显：%s", reply);
    else if (result == 0) printf("服务器已关闭连接\n");
    else if (result == -2) fprintf(stderr, "回显行太长\n");
    else { perror("recv"); close(fd); return 1; }

    close(fd);
    return 0;
}
~~~

### 4.3 Linux 编译和运行

在项目目录打开两个终端：

~~~bash
gcc -Wall -Wextra -g server_linux.c -o server_linux
gcc -Wall -Wextra -g client_linux.c -o client_linux

# 终端 A：先启动服务端（保持运行）
./server_linux

# 终端 B：再启动客户端
./client_linux
~~~

输入 `hello` 后，客户端应显示 `服务器回显：hello`，服务端终端会显示客户端地址和收到的字节数。服务端回到 `accept()` 后继续等待下一次连接。

查看监听状态：

~~~bash
ss -ltnp | grep 8080
~~~

如果只想临时测试端口，也可以使用 `nc 127.0.0.1 8080`；但 `nc` 不一定遵循本章的“收到一行就关闭”行为，因此优先使用我们自己的客户端。

## 5. Windows Winsock 版本

下面的代码适用于 Windows 原生环境（MinGW GCC 或 Visual Studio）。Windows 使用 `SOCKET` 句柄；使用前必须 `WSAStartup()`，结束时调用 `WSACleanup()`。

### 5.1 Windows 服务端：`server_windows.c`

~~~c
#include <winsock2.h>
#include <stdio.h>
#include <string.h>

static int send_all(SOCKET sock, const char *data, int length) {
    int sent = 0;
    while (sent < length) {
        int n = send(sock, data + sent, length - sent, 0);
        if (n > 0) sent += n;
        else return -1;
    }
    return 0;
}

static int recv_line(SOCKET sock, char *buffer, int capacity) {
    int used = 0;
    while (used + 1 < capacity) {
        char ch;
        int n = recv(sock, &ch, 1, 0);
        if (n > 0) {
            buffer[used++] = ch;
            if (ch == '\n') { buffer[used] = '\0'; return used; }
        } else if (n == 0) {
            buffer[used] = '\0';
            return used == 0 ? 0 : used;
        } else return -1;
    }
    buffer[used] = '\0';
    return -2;
}

int main(void) {
    WSADATA wsa_data;
    int startup = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (startup != 0) {
        printf("WSAStartup failed: %d\n", startup); return 1;
    }

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        WSACleanup(); return 1;
    }

    BOOL reuse = TRUE;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&reuse, sizeof reuse);

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listen_sock, (const struct sockaddr *)&address,
             sizeof address) == SOCKET_ERROR) {
        printf("bind failed: %d\n", WSAGetLastError());
        closesocket(listen_sock); WSACleanup(); return 1;
    }
    if (listen(listen_sock, 16) == SOCKET_ERROR) {
        printf("listen failed: %d\n", WSAGetLastError());
        closesocket(listen_sock); WSACleanup(); return 1;
    }

    printf("listening on 127.0.0.1:8080\n");
    for (;;) {
        SOCKET client_sock = accept(listen_sock, NULL, NULL);
        if (client_sock == INVALID_SOCKET) {
            printf("accept failed: %d\n", WSAGetLastError()); break;
        }

        char buffer[1024];
        int result = recv_line(client_sock, buffer, (int)sizeof buffer);
        if (result > 0) {
            printf("received %d bytes: %s", result, buffer);
            if (send_all(client_sock, buffer, result) < 0)
                printf("send failed: %d\n", WSAGetLastError());
        } else if (result == 0) {
            printf("client closed without sending a line\n");
        } else if (result == -2) {
            printf("line is too long (limit: 1023 bytes)\n");
        } else {
            printf("recv failed: %d\n", WSAGetLastError());
        }
        closesocket(client_sock);
    }

    closesocket(listen_sock);
    WSACleanup();
    return 0;
}
~~~

### 5.2 Windows 服务端中三行关键代码的解释

第一次阅读 Windows 服务端时，下面三处代码通常比较难理解。它们分别负责：设置 Socket 选项、让 Socket 开始监听，以及接收一个客户端连接。

#### 5.2.1 设置地址复用选项：`setsockopt()`

~~~c
BOOL reuse = TRUE;
setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
           (const char *)&reuse, sizeof reuse);
~~~

`setsockopt()` 的作用是给一个 Socket 设置选项。这次调用可以按参数拆开理解：

| 参数 | 含义 |
| --- | --- |
| `listen_sock` | 要设置的监听 Socket |
| `SOL_SOCKET` | 这个选项属于通用 Socket 层 |
| `SO_REUSEADDR` | 设置“地址复用”选项 |
| `(const char *)&reuse` | 选项值所在的内存地址 |
| `sizeof reuse` | 选项值占用的字节数 |

`reuse` 的值为 `TRUE`，表示开启该选项。`&reuse` 是取变量地址；由于 Windows 的 `setsockopt()` 要求第四个参数是 `const char *`，所以要写成 `(const char *)&reuse`。这个转换只改变编译器看待指针的方式，不会改变 `reuse` 的值。

服务端关闭后，旧 TCP 连接可能暂时处于 `TIME_WAIT`。设置 `SO_REUSEADDR` 可以减少服务器立即重启时重新绑定地址受到的影响，但它不是“强行抢端口”：如果另一个服务器仍在监听相同地址和端口，`bind()` 通常还是会失败。

更完整的调用应检查返回值：

~~~c
if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&reuse, sizeof reuse) == SOCKET_ERROR) {
    printf("setsockopt failed: %d\n", WSAGetLastError());
    closesocket(listen_sock);
    WSACleanup();
    return 1;
}
~~~

#### 5.2.2 进入监听状态：`listen()`

~~~c
if (listen(listen_sock, 16) == SOCKET_ERROR) {
    printf("listen failed: %d\n", WSAGetLastError());
}
~~~

`listen()` 把已经完成 `bind()` 的 TCP Socket 转换为监听 Socket：

~~~text
socket() 创建普通 Socket
    ↓
bind() 绑定 127.0.0.1:8080
    ↓
listen() 开始等待连接
~~~

第一个参数 `listen_sock` 是要进入监听状态的 Socket。第二个参数 `16` 叫作 `backlog`，可以理解为“程序还没有调用 `accept()` 取走连接时，系统为待处理连接保留的队列长度提示”。它不是服务器最多只能服务 16 个客户端，也不是 `accept()` 会一次返回 16 个连接。

Windows 中 `listen()` 成功返回 `0`，失败返回 `SOCKET_ERROR`，因此：

~~~c
listen(listen_sock, 16) == SOCKET_ERROR
~~~

是在判断 `listen()` 是否失败。失败原因通过 `WSAGetLastError()` 获取。Linux 中通常写成：

~~~c
if (listen(listen_fd, 16) < 0) {
    perror("listen");
}
~~~

#### 5.2.3 接收一个客户端连接：`accept()`

~~~c
SOCKET client_sock = accept(listen_sock, NULL, NULL);
~~~

`accept()` 从监听 Socket 的连接队列中取出一个客户端连接，并返回一个新的、已经连接的 Socket：

~~~text
listen_sock                         client_sock
负责等待新客户端  -- accept() -->  负责和当前客户端收发数据
~~~

这两个 Socket 不能混用：

- `listen_sock` 继续负责接受以后的客户端；
- `client_sock` 用于当前客户端的 `recv()`、`send()` 和 `closesocket()`。

第二、第三个参数用于返回客户端地址。这里都写成 `NULL`，表示暂时不需要知道客户端的 IP 和端口。如果需要记录客户端地址，可以写成：

~~~c
struct sockaddr_in client_address = {0};
int client_length = sizeof client_address;

SOCKET client_sock = accept(
    listen_sock,
    (struct sockaddr *)&client_address,
    &client_length
);
~~~

默认阻塞模式下，如果还没有客户端连接，`accept()` 会停在这里等待。这通常不是程序卡死；在另一个终端启动客户端并调用 `connect()` 后，`accept()` 就会返回。

返回值也必须检查：

~~~c
SOCKET client_sock = accept(listen_sock, NULL, NULL);
if (client_sock == INVALID_SOCKET) {
    printf("accept failed: %d\n", WSAGetLastError());
    closesocket(listen_sock);
    WSACleanup();
    return 1;
}
~~~

可以用一句话记住两种 Socket 的区别：

~~~text
listen_sock 负责“接客”，client_sock 负责“聊天”。
~~~

### 5.3 Windows 客户端：`client_windows.c`

~~~c
#include <winsock2.h>
#include <stdio.h>
#include <string.h>

static int send_all(SOCKET sock, const char *data, int length) {
    int sent = 0;
    while (sent < length) {
        int n = send(sock, data + sent, length - sent, 0);
        if (n > 0) sent += n;
        else return -1;
    }
    return 0;
}

static int recv_line(SOCKET sock, char *buffer, int capacity) {
    int used = 0;
    while (used + 1 < capacity) {
        char ch;
        int n = recv(sock, &ch, 1, 0);
        if (n > 0) {
            buffer[used++] = ch;
            if (ch == '\n') { buffer[used] = '\0'; return used; }
        } else if (n == 0) {
            buffer[used] = '\0';
            return used == 0 ? 0 : used;
        } else return -1;
    }
    buffer[used] = '\0';
    return -2;
}

int main(void) {
    WSADATA wsa_data;
    int startup = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (startup != 0) {
        printf("WSAStartup failed: %d\n", startup); return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        WSACleanup(); return 1;
    }

    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(sock, (const struct sockaddr *)&server,
                sizeof server) == SOCKET_ERROR) {
        printf("connect failed: %d\n", WSAGetLastError());
        closesocket(sock); WSACleanup(); return 1;
    }

    char message[1024];
    printf("输入一行文字：");
    if (fgets(message, (int)sizeof message, stdin) == NULL) {
        closesocket(sock); WSACleanup(); return 0;
    }
    int length = (int)strlen(message);
    if (length == 0 || message[length - 1] != '\n') {
        if (length + 1 >= (int)sizeof message) {
            printf("输入太长\n"); closesocket(sock); WSACleanup(); return 1;
        }
        message[length++] = '\n'; message[length] = '\0';
    }
    if (send_all(sock, message, length) < 0) {
        printf("send failed: %d\n", WSAGetLastError());
        closesocket(sock); WSACleanup(); return 1;
    }

    char reply[1024];
    int result = recv_line(sock, reply, (int)sizeof reply);
    if (result > 0) printf("服务器回显：%s", reply);
    else if (result == 0) printf("服务器已关闭连接\n");
    else if (result == -2) printf("回显行太长\n");
    else printf("recv failed: %d\n", WSAGetLastError());

    closesocket(sock);
    WSACleanup();
    return result < 0 ? 1 : 0;
}
~~~

### 5.4 Windows 编译和运行

MinGW GCC：

~~~powershell
gcc -Wall -Wextra -g server_windows.c -o server_windows.exe -lws2_32
gcc -Wall -Wextra -g client_windows.c -o client_windows.exe -lws2_32

# 终端 A
.\\server_windows.exe

# 终端 B
.\\client_windows.exe
~~~

Visual Studio Developer PowerShell：

~~~powershell
cl /W4 /Zi server_windows.c ws2_32.lib
cl /W4 /Zi client_windows.c ws2_32.lib
~~~

检查端口：

~~~powershell
Test-NetConnection 127.0.0.1 -Port 8080
Get-NetTCPConnection -LocalPort 8080 -ErrorAction SilentlyContinue
~~~

### 5.5 两个平台的关键差异

| 项目 | Linux/POSIX | Windows Winsock |
| --- | --- | --- |
| 句柄 | `int` | `SOCKET` |
| 创建失败 | `< 0` | `INVALID_SOCKET` |
| 初始化 | 通常不需要 | `WSAStartup()` |
| 错误 | `errno`、`perror()` | `WSAGetLastError()` |
| 关闭 | `close()` | `closesocket()` |
| 编译链接 | 通常无需额外库 | `-lws2_32` 或 `ws2_32.lib` |

不要在 Windows 中用 `close()` 替代 `closesocket()`，也不要在 Linux 中使用 `WSAGetLastError()`。

## 6. 代码中最容易困惑的地方

### 6.1 `htons()`、`htonl()` 和 `ntohs()`

网络协议统一使用网络字节序，而 CPU 的主机字节序可能不同：

- `htons`：host to network short，给端口等 16 位数使用；
- `htonl`：host to network long，给 IPv4 地址等 32 位数使用；
- `ntohs`：network to host short，显示收到的端口时使用。

端口写入地址结构时应写 `htons(8080)`，不能只写 `8080`。

### 6.2 `buffer[count] = '\0'` 的意义

`recv()` 返回的是字节数量，不会自动补 C 字符串结尾的 `\0`。只有当数据确实是文本，并且缓冲区预留了一个位置时，才可以手动补：

~~~c
ssize_t count = recv(fd, buffer, sizeof buffer - 1, 0);
if (count > 0) {
    buffer[count] = '\0';
    printf("%s", buffer);
}
~~~

二进制数据（图片、压缩包等）不能用 `%s` 打印，也不能用 `strlen()` 计算长度；必须始终配合明确的字节数。

### 6.3 `recv()` 的三个结果

~~~text
> 0  实际收到的字节数
= 0  对端有序关闭连接（收到 FIN）
< 0  发生错误
~~~

`0` 不是“暂时没数据”。阻塞 Socket 在暂时没数据时会继续等待；返回 `0` 说明对端已经不会再发送数据。

### 6.4 `SO_REUSEADDR` 不是“强行抢端口”

服务端重启时，旧 TCP 连接可能短时间处于 `TIME_WAIT`。设置 `SO_REUSEADDR` 可以减少重新绑定时受到这种状态影响的情况，但它不能让两个正在运行的程序同时占用同一个地址和端口。若 `bind()` 仍失败，应先检查是否已有旧服务端进程。

### 6.5 为什么看起来“卡住”

这是阻塞模式的正常行为：

| 位置 | 等待什么 |
| --- | --- |
| `accept()` | 等待客户端连接 |
| `recv()` | 等待下一字节或对端关闭 |
| `send()` | 等待内核发送缓冲区有空间 |

本项目规定“客户端先发一行，服务端再回显”。如果两边都先调用 `recv()`，就会互相等待；这不是 TCP 故障，而是应用协议顺序错误。

## 7. 运行测试与观察结果

### 测试一：正常回显

1. 编译对应平台的服务端和客户端。
2. 先启动服务端，确认出现 `listening on 127.0.0.1:8080`。
3. 另开终端启动客户端，输入 `hello` 并回车。
4. 客户端显示原样文本，服务端显示收到的字节数。

### 测试二：服务端未启动

先不要运行服务端，直接运行客户端。通常 Linux 会显示 `connect: Connection refused`，Windows 会显示 Winsock 错误码。原因是 8080 端口当前没有监听者。

### 测试三：改变端口

只把服务端端口改成 `8081`，客户端仍连接 `8080`。客户端会连接失败。再把客户端也改成 `8081`，连接即可恢复。这说明客户端和服务端的端口必须一致。

### 测试四：超长输入

本章客户端会在本地拒绝超过缓冲区的输入。若要验证服务端限制，可使用另一个测试客户端向服务端发送超过 1023 个字节且没有换行的文本；服务端会报告“行太长”并关闭连接。限制长度是为了防止无限追加导致缓冲区溢出；真实项目应设计明确的最大消息长度和错误响应。

### 测试五：查看监听端口

~~~bash
# Linux/WSL2
ss -ltnp | grep 8080
~~~

~~~powershell
# Windows
Get-NetTCPConnection -LocalPort 8080
~~~

看到 `LISTEN` 表示服务端正在等待连接；客户端连接期间还可能看到 `ESTABLISHED`。测试结束后按 `Ctrl+C` 停止服务端。

## 8. 常见错误排查

| 现象 | 可能原因 | 处理方法 |
| --- | --- | --- |
| `Connection refused` | 服务端没启动，或端口不一致 | 先启动服务端，核对双方端口 |
| `Address already in use` | 旧服务端仍在运行或端口被占用 | 用 `ss`/`Get-NetTCPConnection` 检查 PID |
| 服务端停在 `accept()` | 还没有客户端连接 | 另开终端启动客户端 |
| 服务端停在 `recv_line()` | 客户端没发送换行符 | 客户端按回车，或检查协议 |
| 客户端停在 `recv_line()` | 服务端没有回显，或回显缺少 `\n` | 检查服务端 `send_all()` 的数据 |
| 输出乱码 | 两端编码不一致 | 统一使用 UTF-8，并按字节数发送 |
| Windows 链接错误 | 没有链接 Winsock 库 | 添加 `-lws2_32` 或 `ws2_32.lib` |
| 连接局域网地址失败 | 防火墙、监听地址或路由问题 | 先用 `127.0.0.1` 验证，再逐步扩大范围 |

排错时记录四项信息：平台、目标 IP、目标端口、失败的 API 和错误码。只记录“连不上”通常不足以定位问题。

## 9. 这个示例的边界

为了让流程清晰，本示例有意保持简单：

- 一次连接只处理一行，然后关闭客户端 Socket；
- 服务端一次只处理一个客户端，没有线程、进程或 `select`/`poll`/`epoll`；
- `recv_line()` 每次读取一个字节，适合学习，不适合高吞吐生产环境；
- 只监听 IPv4 回环地址，不接受其他机器连接；
- 没有身份认证、加密和复杂输入校验。

后续可以按这个顺序升级：先把接收缓存改为“批量读取 + 查找换行符”，再支持一条连接发送多行，最后使用线程或 I/O 多路复用处理并发客户端。不要在没有消息边界的情况下直接把它改成“读一次就处理一次”。

## 10. 完成检查

- [ ] 我能说出服务端 `socket -> bind -> listen -> accept` 的顺序。
- [ ] 我知道监听 Socket 和已连接 Socket 的用途不同。
- [ ] 我能在 Linux/WSL2 编译并运行两个程序。
- [ ] 我能在 Windows 下使用 MinGW 或 Visual Studio 编译并运行两个程序。
- [ ] 我知道 `send()` 可能只发送一部分，所以需要 `send_all()`。
- [ ] 我知道 TCP 没有消息边界，本项目用 `\n` 表示一行结束。
- [ ] 我能解释 `recv()` 返回大于 0、等于 0 和小于 0 的含义。
- [ ] 我能解释程序阻塞在 `accept()` 或 `recv()` 的原因。
- [ ] 我知道 Linux 使用 `close()`，Windows 使用 `closesocket()`。
- [ ] 我能用 `ss` 或 `Get-NetTCPConnection` 检查 8080 端口。

## 关联

- [[Socket基础]]
- [[TCP客户端]]
- [[TCP服务器]]
- [[连接生命周期]]
- [[发送与接收数据]]
- [[消息边界与拆包]]
- [[超时、断线与异常]]
- [[TCP聊天室项目]]
- [[06-实践笔记/README]]
