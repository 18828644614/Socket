---
type: topic
status: complete
created: 2026-08-25
updated: 2026-08-26
tags:
  - TCP
  - 客户端
  - Socket
---

# TCP客户端

## 学习目标

学完本章后，你应该能够：

- 画出 TCP 客户端从创建到关闭的执行顺序。
- 看懂 `connect()` 的地址参数、长度参数和返回值。
- 在 Linux 和 Windows 上分别创建一个 TCP 客户端。
- 理解客户端为什么通常不需要手动 `bind()`。
- 正确处理 `send()`/`recv()` 的返回值，而不是假设一次调用就完成全部数据。
- 识别“服务器未启动、地址错误、端口错误、连接被关闭”等常见问题。

## 1. 客户端到底做什么

TCP 客户端是**主动发起连接的一方**。它通常已经知道服务器的地址和端口，然后按照下面的顺序工作：

~~~text
准备服务器地址和端口
        ↓
socket()      创建一个 TCP Socket
        ↓
connect()     向服务器发起连接
        ↓
send()/recv() 交换字节
        ↓
close()       关闭连接并释放资源
~~~

服务器必须先运行并处于监听状态，客户端的 `connect()` 才有机会成功。客户端不能只凭“服务器程序存在”就连接成功，还要确认：

| 要素 | 例子 | 作用 |
| --- | --- | --- |
| IP 地址 | `127.0.0.1` | 找到目标主机；它表示本机回环接口 |
| 端口 | `8080` | 找到目标主机上的某个服务 |
| 传输协议 | TCP | 决定使用可靠的字节流连接 |

`127.0.0.1:8080` 的意思是“连接本机 8080 端口”，不是连接互联网中的某台服务器。访问局域网其他电脑时，要换成服务器电脑的局域网地址，例如 `192.168.1.20`。

## 2. 客户端为什么通常不需要 `bind()`

服务器需要让客户端知道“服务在哪个固定端口”，所以通常会显式调用 `bind()`，例如绑定 `0.0.0.0:8080`。

客户端一般只需要知道服务器地址：

~~~text
客户端 socket()
      ↓ connect(服务器IP, 服务器端口)
操作系统自动选择：
  - 合适的本地网卡/IP
  - 一个未占用的临时源端口，例如 53124
~~~

连接建立后，客户端的一端可能是 `192.168.1.8:53124`，服务器的一端是 `192.168.1.20:8080`。这个四元组唯一标识连接：

~~~text
(客户端IP, 客户端临时端口, 服务器IP, 服务器端口)
~~~

只有在以下情况才可能需要客户端手动 `bind()`：必须使用固定源端口、必须选择某张网卡、或程序有特殊协议要求。初学阶段不要为了“完整”而给客户端绑定固定端口，否则容易遇到端口已占用问题。

## 3. 创建 TCP Socket

Linux/POSIX 和 Windows Winsock 的 `socket()` 参数含义基本一致：

~~~c
// Linux/POSIX
int fd = socket(AF_INET, SOCK_STREAM, 0);

// Windows Winsock
SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
~~~

| 参数 | 含义 | 本章常用值 |
| --- | --- | --- |
| 地址族 | 地址结构的格式 | `AF_INET`（IPv4） |
| 类型 | 通信抽象 | `SOCK_STREAM`（字节流） |
| 协议 | 具体传输协议 | `0` 或 `IPPROTO_TCP` |

`AF_INET + SOCK_STREAM` 通常表示 IPv4 TCP。Linux 成功时返回非负文件描述符，失败返回 `-1`；Windows 成功时返回 `SOCKET`，失败返回 `INVALID_SOCKET`。

~~~c
// Linux
if (fd == -1) {
    perror("socket");
}

// Windows
if (sock == INVALID_SOCKET) {
    printf("socket failed: %d\n", WSAGetLastError());
}
~~~

## 4. 准备服务器地址

### 4.1 IPv4 地址结构

初学时可以先使用 `sockaddr_in`：

~~~c
struct sockaddr_in server_addr = {0};
server_addr.sin_family = AF_INET;
server_addr.sin_port = htons(8080);
server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
~~~

逐行理解：

1. `struct sockaddr_in server_addr = {0};` 创建一个 IPv4 地址结构，并把未使用字段清零。
2. `sin_family` 告诉系统这是 IPv4 地址。
3. `sin_port` 保存服务器端口。`htons(8080)` 把主机字节序转换成网络字节序。
4. `INADDR_LOOPBACK` 表示 `127.0.0.1`，只连接本机。也可以使用 `inet_pton()` 从字符串转换地址。

如果要连接局域网服务器，可以写成：

~~~c
if (inet_pton(AF_INET, "192.168.1.20", &server_addr.sin_addr) != 1) {
    // 0 表示文本格式不正确，-1 表示地址族不支持或其他错误
}
~~~

不要直接把字符串赋给 `sin_addr`。`"127.0.0.1"` 是给人看的文本，而 `sin_addr` 需要网络地址的二进制表示。

### 4.2 为什么 `connect()` 要转换成 `sockaddr *`

IPv4 使用 `struct sockaddr_in`，IPv6 使用 `struct sockaddr_in6`，但通用的 `connect()` 原型接收 `struct sockaddr *`：

~~~c
connect(fd, (struct sockaddr *)&server_addr, sizeof server_addr);
~~~

这里的强制转换不会复制或改变地址内容，只是告诉编译器“把这个具体地址结构当成通用地址结构传入”。函数会根据结构中的 `sin_family` 判断如何解释后面的字段。

## 5. 建立连接：`connect()`

Linux/POSIX 常见原型：

~~~c
int connect(int sockfd,
            const struct sockaddr *addr,
            socklen_t addrlen);
~~~

Windows 原型使用 `SOCKET` 和 `int` 长度：

~~~c
int connect(SOCKET s,
            const struct sockaddr *name,
            int namelen);
~~~

三个参数可以这样记：

| 参数 | 含义 |
| --- | --- |
| Socket | 前面 `socket()` 创建的客户端 Socket |
| 地址指针 | 服务器的 IP、端口和地址族 |
| 地址长度 | 地址结构占用的字节数，通常是 `sizeof server_addr` |

调用过程可以理解为：

~~~text
客户端 connect()
    ↓ TCP 握手请求
服务器监听 Socket 检查端口
    ↓ 接受或拒绝
握手成功：connect 返回 0，客户端进入已连接状态
握手失败：connect 返回错误
~~~

阻塞模式下，`connect()` 可能等待一段时间。等待的原因可能是网络延迟、路由不可达或防火墙丢弃数据包。如果服务器明确拒绝，常见情况是较快返回“连接被拒绝”。后续可以用超时和非阻塞方式限制等待时间，见 [[超时、断线与异常]] 和 [[阻塞与非阻塞]]。

## 6. 一个最小 Linux 客户端

下面的示例连接本机 `127.0.0.1:8080`，发送一行文本，然后接收服务器返回的数据。它假设服务器实现了“收到什么就返回什么”的回显协议；如果服务器只监听而不回复，程序会阻塞在 `recv()`，这是协议不匹配，不一定是客户端代码崩溃。

~~~c
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);

    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) != 1) {
        fprintf(stderr, "invalid server address\n");
        close(fd);
        return 1;
    }

    if (connect(fd,
                (struct sockaddr *)&server_addr,
                sizeof server_addr) == -1) {
        perror("connect");
        close(fd);
        return 1;
    }

    const char message[] = "hello from client\n";
    size_t total = strlen(message);
    size_t sent = 0;

    while (sent < total) {
        ssize_t n = send(fd, message + sent, total - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        perror("send");
        close(fd);
        return 1;
    }

    char buffer[1024];
    ssize_t count = recv(fd, buffer, sizeof buffer - 1, 0);
    if (count > 0) {
        buffer[count] = '\0';
        printf("server replied: %s", buffer);
    } else if (count == 0) {
        printf("server closed the connection\n");
    } else {
        perror("recv");
    }

    close(fd);
    return count < 0 ? 1 : 0;
}
~~~

### 6.1 按执行顺序逐段解释

1. `socket()` 只创建通信对象，还没有连接任何服务器。
2. `sockaddr_in` 保存目标服务器的 IPv4 地址和端口。
3. `inet_pton()` 把文本地址转换成二进制地址；返回值不是 `1` 就说明地址不可用。
4. `connect()` 成功后，`fd` 才代表一条已经建立的 TCP 连接。
5. `message` 是要发送的字节数组；`strlen()` 得到文本长度，不包含结尾的 `\0`。
6. `send()` 放入循环，是因为一次调用可能只发送一部分字节。`sent` 表示已经成功交给内核的字节数。
7. `recv()` 把内核接收缓冲区中的字节复制到 `buffer`。它最多读取 `sizeof buffer - 1` 字节，为末尾的字符串 `\0` 预留一个位置。
8. `count > 0` 表示读到了字节；`count == 0` 表示对端已经有序关闭连接；`count < 0` 表示发生错误。
9. 只有收到正数时才在末尾补 `\0`。`recv()` 返回的是字节数，不会自动把数据变成 C 字符串。

## 7. Windows Winsock 客户端

Windows 需要先初始化 Winsock，并使用 `closesocket()` 关闭 Socket：

~~~c
#include <winsock2.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        printf("WSAStartup failed: %d\n", result);
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(sock,
                (const struct sockaddr *)&server_addr,
                sizeof server_addr) == SOCKET_ERROR) {
        printf("connect failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    const char message[] = "hello from Windows client\n";
    int total = (int)strlen(message);
    int sent = 0;
    while (sent < total) {
        int n = send(sock, message + sent, total - sent, 0);
        if (n > 0) {
            sent += n;
            continue;
        }
        printf("send failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    char buffer[1024];
    int count = recv(sock, buffer, (int)sizeof buffer - 1, 0);
    if (count > 0) {
        buffer[count] = '\0';
        printf("server replied: %s", buffer);
    } else if (count == 0) {
        printf("server closed the connection\n");
    } else {
        printf("recv failed: %d\n", WSAGetLastError());
    }

    closesocket(sock);
    WSACleanup();
    return count < 0 ? 1 : 0;
}
~~~

编译：

~~~powershell
# MinGW
gcc -Wall -Wextra -g client.c -o client.exe -lws2_32

# Visual Studio Developer PowerShell
cl /W4 /Zi client.c ws2_32.lib
~~~

Linux 和 Windows 的核心流程相同，主要差别如下：

| 项目 | Linux/POSIX | Windows Winsock |
| --- | --- | --- |
| Socket 类型 | `int` | `SOCKET` |
| 创建失败 | `-1` | `INVALID_SOCKET` |
| 初始化 | 通常不需要额外初始化 | `WSAStartup()` |
| 错误获取 | `errno`、`perror()` | `WSAGetLastError()` |
| 关闭 | `close(fd)` | `closesocket(sock)` |
| 清理 | 关闭 fd | `closesocket()` 后 `WSACleanup()` |

## 8. `send()` 和 `recv()` 的关键规则

### 8.1 `send()` 返回多少，不等于对端收到多少

`send()` 返回正数，表示本机内核已经接受了这些字节。它不代表对端应用程序已经读取，更不代表对端已经完成业务处理。因此：

- 发送大块数据时必须处理“部分发送”。
- 需要业务确认时，设计应用层响应，例如服务器返回 `OK`。
- 不要把一次 `send()` 当成一次完整消息。

这就是示例中 `while (sent < total)` 循环存在的原因。更完整的字节流和部分读写说明见 [[发送与接收数据]] 与 [[字节、编码与数据边界]]。

### 8.2 `recv()` 的三种结果

~~~text
count > 0   收到 count 个字节
count == 0  对端正常关闭了 TCP 发送方向
count < 0   发生错误
~~~

`recv()` 返回 `0` 不是“暂时没有数据”。阻塞模式下，暂时没有数据时它会继续等待；返回 `0` 通常意味着对端已经发送 FIN，后续不会再有数据可读。

### 8.3 TCP 没有消息边界

如果客户端连续发送：

~~~text
send("HELLO")
send("WORLD")
~~~

服务器可能一次 `recv()` 读到 `HELLOWORLD`，也可能先读到 `HEL`，再读到 `LOWORLD`。TCP 只保证字节顺序和可靠性，不保存调用次数。应用程序必须自行定义消息边界，例如固定长度、分隔符或长度前缀，详见 [[消息边界与拆包]]。

## 9. 阻塞、超时与“程序卡住”

默认 Socket 通常是阻塞模式：

| 调用 | 可能阻塞的原因 |
| --- | --- |
| `connect()` | 等待 TCP 握手结果 |
| `send()` | 内核发送缓冲区暂时没有空间 |
| `recv()` | 尚未收到服务器数据 |

例如客户端发送完请求后立即调用 `recv()`，但服务器协议规定“客户端发送结束后才回复”，客户端可能一直等。解决办法不是盲目增加循环，而是先明确协议的结束条件：

~~~text
客户端发送请求
  -> 服务器是否会立即回复？
  -> 回复长度是否固定？
  -> 是否以换行符结束？
  -> 是否需要客户端关闭发送方向表示请求结束？
~~~

学习阶段可以使用接收超时避免无限等待。Linux 常见写法是：

~~~c
#include <sys/time.h>

struct timeval timeout;
timeout.tv_sec = 5;
timeout.tv_usec = 0;
setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
           &timeout, sizeof timeout);
~~~

Winsock 通常传入“毫秒数”的 `DWORD`，例如：

~~~c
DWORD timeout_ms = 5000;
setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
           (const char *)&timeout_ms, sizeof timeout_ms);
~~~

两段代码的含义都是：`recv()` 最多等待约 5 秒，超时后返回错误。超时不是“连接一定断了”，而是本次等待没有在规定时间内得到结果，是否重试要由应用协议决定。

## 10. 使用域名和 IPv6：`getaddrinfo()` 的方向

固定写 `sockaddr_in` 只适合学习 IPv4。真实客户端常常需要支持域名、IPv4 和 IPv6，此时使用 `getaddrinfo()`：

~~~text
域名 + 端口文本
       ↓ getaddrinfo()
得到一个候选地址链表
       ↓ 依次 socket() + connect()
第一个成功的地址就是连接结果
~~~

`getaddrinfo()` 会处理 DNS 查询和地址结构差异，但引入了 `addrinfo` 链表、`freeaddrinfo()` 和逐个尝试地址等概念。建议先掌握本章的 IPv4 版本，再学习它。相关地址族知识见 [[Socket基础]] 和 [[IP地址与端口]]。

## 11. 常见连接错误与排查

| 现象/错误 | 常见原因 | 排查方法 |
| --- | --- | --- |
| `Connection refused` | 目标端口没有监听，或服务器主动拒绝 | 先启动服务器；用 `ss`/`netstat` 查看监听状态 |
| `timed out` | 防火墙丢弃、路由不可达、地址写错 | 核对 IP、端口和网络连通性 |
| `Address family not supported` | 地址族和地址结构不匹配 | `AF_INET` 配 `sockaddr_in`，`AF_INET6` 配 `sockaddr_in6` |
| 连接成功但 `recv()` 一直等 | 服务器没有回复，或协议缺少结束条件 | 阅读服务器协议，确认何时回复/关闭 |
| 收到的数据不完整 | 把一次 `recv()` 误认为完整消息 | 使用循环和消息边界解析 |
| Windows 的 `WSAStartup` 失败 | Winsock 初始化失败 | 打印返回值，确认程序和系统环境 |
| Windows 编译链接错误 | 没有链接 Winsock 库 | 添加 `-lws2_32` 或 `ws2_32.lib` |

排错时至少记录：目标 IP、目标端口、失败的 API、返回值和平台错误码。只写“连接失败”无法判断是服务器、网络还是代码问题。

## 12. 实践

### 实践一：连接本机回显服务器

1. 先运行一个监听 `127.0.0.1:8080` 的 TCP 回显服务器。
2. 编译本章的 Linux 或 Windows 客户端。
3. 运行客户端，确认输出服务器返回的文本。
4. 暂停服务器，再次运行客户端，观察 `connect()` 的错误信息。

### 实践二：改变地址和端口

依次尝试：

- 把 `127.0.0.1` 改成不存在的地址。
- 把 `8080` 改成服务器没有监听的端口。
- 把回环地址改成局域网服务器地址。

每次只改一个因素，并记录 `connect()` 的表现。这样可以把“地址错误”“端口错误”“服务器未启动”区分开。

### 实践三：观察部分读写

1. 发送几 KB 或更大的字符串。
2. 让服务器每次只读取很小的缓冲区。
3. 打印每次 `send()` 和 `recv()` 的返回值。
4. 观察一次调用的字节数与完整消息长度并不总是相同。

### 实践四：自己写出客户端流程图

不看代码，补全下面的空白：

~~~text
socket()
  -> __________
  -> send()/recv()
  -> __________
~~~

然后说明：为什么客户端通常没有 `bind()`？`recv()` 返回 `0` 表示什么？

## 检查

- [ ] 我能说出 TCP 客户端的 `socket -> connect -> send/recv -> close` 流程。
- [ ] 我能解释客户端通常由操作系统自动分配临时源端口。
- [ ] 我能解释 `connect()` 的三个参数。
- [ ] 我知道 `127.0.0.1` 只能连接本机，局域网连接要使用服务器的局域网 IP。
- [ ] 我能正确初始化 `sockaddr_in` 并使用 `htons()`。
- [ ] 我知道 Linux 使用 `close()`，Windows 使用 `closesocket()`。
- [ ] 我能区分 `recv() > 0`、`recv() == 0` 和 `recv() < 0`。
- [ ] 我不会把一次 `send()` 或 `recv()` 当成一条完整消息。
- [ ] 我能解释客户端阻塞在 `connect()` 或 `recv()` 上的常见原因。
- [ ] 我能根据错误码和网络观察工具排查连接失败。

## 关联

- [[Socket基础]]
- [[TCP服务器]]
- [[连接生命周期]]
- [[发送与接收数据]]
- [[消息边界与拆包]]
- [[超时、断线与异常]]
- [[字节、编码与数据边界]]
- [[网络观察工具]]
- [[阻塞与非阻塞]]
- [[TCP回显项目]]
