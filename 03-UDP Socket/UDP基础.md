---
type: topic
status: complete
created: 2026-08-25
updated: 2026-09-01
tags:
  - UDP
  - Socket
  - Windows
  - Linux
---

# UDP基础

## 学习目标

学完本章后，你应该能够：

- 解释 UDP 的“无连接”和“数据报”分别是什么意思。
- 区分 UDP 服务端流程与 TCP 服务端流程。
- 使用 `sendto()` 和 `recvfrom()` 收发一个 UDP 数据报。
- 知道 `sendto()` 成功不代表对端已经收到或处理数据。
- 在 Windows 上优先完成 UDP 回显实验，并在 Linux/WSL2 上运行等价程序。
- 处理最常见的阻塞等待、超时、端口占用和缓冲区问题。

## 1. UDP 到底在做什么

UDP（User Datagram Protocol，用户数据报协议）是传输层协议。程序把一份独立的数据交给 UDP，并写明目标 IP 和端口；UDP 尽力把这份数据交给目标主机上对应端口的程序。

```text
客户端程序                                      服务端程序
sendto(内容, 服务端 IP:端口)   ---- 数据报 ---->  recvfrom(内容, 客户端 IP:端口)
recvfrom(内容, 服务端 IP:端口) <--- 数据报 ----  sendto(内容, 客户端 IP:端口)
```

可以把 UDP 数据报想成一张独立的明信片：每一张都写着目标地址，寄出前不需要先建立一段持续会话。收件人收到后，也能看到寄件人的地址，从而回信。

这个比喻有两个重点：

1. 每次发送是一份独立数据报。发送两次，就是两份数据报。
2. UDP 不承诺每份都送达，也不承诺送达顺序。确认、重传和排序要由应用自己决定是否实现。

## 2. UDP 服务端为什么没有 `listen()` 和 `accept()`

TCP 要先建立连接，因此服务端流程是：

```text
TCP：socket() -> bind() -> listen() -> accept() -> recv()/send()
```

UDP 不建立 TCP 那种连接状态。服务端绑定端口后，就可以直接接收许多客户端发来的数据报：

```text
UDP 服务端：socket() -> bind() -> recvfrom()/sendto()
UDP 客户端：socket() -> sendto()/recvfrom() -> close()
```

所以 UDP 中通常**不调用** `listen()` 或 `accept()`；这不是漏步骤，而是协议模型不同。

```text
一个 UDP 服务端 Socket（127.0.0.1:9000）
  <- 数据报 -- 客户端 A（127.0.0.1:51001）
  <- 数据报 -- 客户端 B（127.0.0.1:51002）
  <- 数据报 -- 客户端 C（127.0.0.1:51003）
```

`recvfrom()` 一次会得到两类信息：

| 信息 | 用途 |
| --- | --- |
| 数据报内容与长度 | 处理业务数据 |
| 发送方 IP 和端口 | 记录来源，或用 `sendto()` 回应这个客户端 |

## 3. UDP 的两个关键特性

### 3.1 保留数据报边界

客户端连续发送：

~~~c
sendto(sock, "one", 3, 0, ...);
sendto(sock, "two", 3, 0, ...);
~~~

服务端会分别读到两份数据报：

```text
第一次 recvfrom() -> "one"
第二次 recvfrom() -> "two"
```

一次 `recvfrom()` 最多取出**一个**数据报，不会把两份数据报合并。这与 TCP 字节流不同：TCP 的接收方必须自行处理半包、粘包和消息边界。

但是，保留边界不等于可靠。UDP 数据报仍可能丢失、重复、乱序或延迟太久。

### 3.2 不保证到达、顺序和不重复

| 可能情况 | 例子 | 需要时的应用层做法 |
| --- | --- | --- |
| 丢失 | 序号 2 根本没到 | 超时、确认、必要时重传 |
| 乱序 | 先到序号 3，再到序号 2 | 数据中增加序号并排序或丢弃旧消息 |
| 重复 | 一条请求收到了两次 | 请求 ID 与去重逻辑 |
| 延迟过久 | 旧坐标现在才到 | 加时间戳，丢弃过期数据 |

`sendto()` 返回成功，只表示本机操作系统已经接受了这份数据报；它不表示目标程序已经收到、解析或完成业务：

```text
sendto() 返回发送长度
  -> 本机内核接受数据报
  -> 网络中仍可能丢失
  -> 对端内核可能收到
  -> 对端程序还必须调用 recvfrom()
  -> 对端程序处理业务，并选择是否回复
```

重要业务不能只靠“发送没有报错”判断成功。至少需要设计“请求 ID + 响应 + 超时”，必要时还要重传和去重，详见 [[UDP可靠性设计]]。

## 4. `sendto()`、`recvfrom()` 与地址

一个 UDP 目标由 IP 和端口共同决定：

```text
127.0.0.1:9000
│          └─ 端口：本机上要交给哪个程序
└─ IP：要送到哪台主机；127.0.0.1 表示本机回环地址
```

服务端必须 `bind()` 到一个稳定端口，让客户端知道发送目标。客户端通常可以不调用 `bind()`；它第一次 `sendto()` 时，操作系统会自动分配本地临时端口。服务端通过 `recvfrom()` 得到该端口，便可以回包。

简化后的 `recvfrom()` 形式：

~~~c
recvfrom(socket, buffer, buffer_size, flags, source_address, source_address_size);
~~~

后两个参数是输出位置。调用前要告诉系统地址结构有多大；调用后，系统把实际来源地址写入其中。因此每次循环都要重新设置长度：

~~~c
struct sockaddr_in client_addr;
int client_addr_len = sizeof(client_addr);  /* Windows 示例 */
/* recvfrom() 会写 client_addr，并可能改写 client_addr_len */
~~~

### 4.1 文本数据为什么预留 `\0`

网络传输的是字节，不会自动添加 C 字符串结束符 `\0`。`recvfrom()` 返回实际收到的字节数，所以文本示例要这样写：

~~~c
char buffer[1024];
int received = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, ...);
if (received >= 0) {
    buffer[received] = '\0';  /* 仅为 printf 显示文本准备 */
}
~~~

`sizeof(buffer) - 1` 留出一个位置写 `\0`。图片、音频等二进制数据不能按 C 字符串打印，必须依靠返回长度处理。

### 4.2 缓冲区太小时会怎样

接收缓冲区小于数据报时，不能在下一次 `recvfrom()` 读取“同一份数据报的剩余部分”。该数据报会被截断，或被平台作为过大消息报告错误；Windows 与 Linux 的具体返回方式可能不同，但剩余字节都不会像 TCP 一样等着下次读取。

因此，不要把文件或巨型 JSON 直接塞进单个 UDP 数据报。对自己的协议先规定最大报文长度，并让缓冲区能容纳它；更大消息需要分块编号、重组、超时和完整性校验，或者选择 TCP。

## 5. Windows 优先实践：UDP 回显程序

下面的程序使用 Windows 原生 Winsock。服务端绑定 `127.0.0.1:9000`；客户端发送一行文本，服务端打印来源地址并原样回发。`127.0.0.1` 只在本机内通信，第一次实验不需要局域网防火墙配置。

### 5.1 Windows 服务端：`udp_server_win.c`

~~~c
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

#pragma comment(lib, "Ws2_32.lib")  /* 供 Visual Studio 链接 Winsock 使用 */

#define SERVER_PORT 9000
#define BUFFER_SIZE 1024

int main(void) {
    WSADATA wsa_data;
    SOCKET server_sock = INVALID_SOCKET;
    struct sockaddr_in server_addr = {0};

    /* Windows 使用 Socket 前必须初始化 Winsock。 */
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    server_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_sock == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    /* 只允许本机访问；INADDR_ANY 会监听所有 IPv4 网卡。 */
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(server_sock, (const struct sockaddr *)&server_addr,
             sizeof(server_addr)) == SOCKET_ERROR) {
        printf("bind failed: %d\n", WSAGetLastError());
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    printf("UDP server is bound to 127.0.0.1:%d\n", SERVER_PORT);
    printf("Press Ctrl+C to stop.\n");

    for (;;) {
        char buffer[BUFFER_SIZE];
        struct sockaddr_in client_addr;
        int client_addr_len = sizeof(client_addr);
        char client_ip[INET_ADDRSTRLEN];

        /* 默认阻塞：没有数据报时，程序在这里等待是正常的。 */
        int received = recvfrom(server_sock, buffer, BUFFER_SIZE - 1, 0,
                                (struct sockaddr *)&client_addr,
                                &client_addr_len);
        if (received == SOCKET_ERROR) {
            printf("recvfrom failed: %d\n", WSAGetLastError());
            continue;
        }

        buffer[received] = '\0';
        if (inet_ntop(AF_INET, &client_addr.sin_addr,
                      client_ip, sizeof(client_ip)) == NULL) {
            printf("inet_ntop failed: %d\n", WSAGetLastError());
            continue;
        }

        printf("received %d bytes from %s:%u: %s\n", received, client_ip,
               (unsigned int)ntohs(client_addr.sin_port), buffer);

        /* recvfrom() 返回的来源地址，就是回显数据报的目标地址。 */
        int sent = sendto(server_sock, buffer, received, 0,
                          (const struct sockaddr *)&client_addr,
                          client_addr_len);
        if (sent == SOCKET_ERROR) {
            printf("sendto failed: %d\n", WSAGetLastError());
        } else {
            printf("echoed %d bytes\n", sent);
        }
    }
}
~~~

### 5.1.1 服务端接收循环：客户端 IP 是怎样得到的

上面的 `for (;;)` 不是一个单独的函数，而是服务端持续“接收、打印、回显”的循环：

```text
等待数据报
  -> 收到客户端消息
  -> 取得客户端 IP 和端口
  -> 打印消息
  -> 把消息发回客户端
```

先看循环开始时准备的变量：

~~~c
char buffer[BUFFER_SIZE];
struct sockaddr_in client_addr;
int client_addr_len = sizeof(client_addr);
char client_ip[INET_ADDRSTRLEN];
~~~

- `buffer` 保存客户端发送的数据。
- `client_addr` 保存客户端的地址信息。
- `client_addr_len` 表示 `client_addr` 可容纳的大小。
- `client_ip` 保存转换后的文本 IP，例如 `"127.0.0.1"`。

`struct sockaddr_in` 中最重要的字段是：

~~~c
client_addr.sin_family;  /* 地址族，例如 AF_INET */
client_addr.sin_addr;    /* 客户端 IP，二进制形式 */
client_addr.sin_port;    /* 客户端端口，网络字节序 */
~~~

#### `recvfrom()` 如何获得客户端地址

关键调用是：

~~~c
int received = recvfrom(server_sock, buffer, BUFFER_SIZE - 1, 0,
                        (struct sockaddr *)&client_addr,
                        &client_addr_len);
~~~

可以把它理解成下面的意思：

> 从 `server_sock` 读取一个 UDP 数据报，把内容放入 `buffer`，并把发送方的地址写入 `client_addr`。

`recvfrom()` 的最后两个参数是输出参数：

```text
调用前：client_addr_len = client_addr 的容量
调用后：client_addr      = 发送方的 IP 和端口
        client_addr_len = 实际写入的地址长度
```

例如，客户端从 `127.0.0.1:52000` 向服务端 `127.0.0.1:9000` 发送 `hello`，调用成功后可以得到：

```text
client_addr.sin_addr = 127.0.0.1
client_addr.sin_port = 52000
```

这些信息来自收到的 UDP 数据报中的**源 IP 和源端口**。程序不需要自己解析底层 IP 数据包，Windows 网络协议栈会把来源地址填入 `client_addr`。

`client_addr` 的类型是 `struct sockaddr_in`，而 `recvfrom()` 接收的是通用的 `struct sockaddr *`，所以要进行强制转换：

~~~c
(struct sockaddr *)&client_addr
~~~

这不会改变结构内容，只是告诉编译器：把这个 IPv4 地址结构按通用 Socket 地址指针传给系统。

#### `inet_ntop()` 如何把 IP 变成字符串

`client_addr.sin_addr` 内部保存的是适合网络传输的二进制 IP，不能直接使用 `%s` 打印。因此代码调用：

~~~c
if (inet_ntop(AF_INET, &client_addr.sin_addr,
              client_ip, sizeof(client_ip)) == NULL) {
    printf("inet_ntop failed: %d\n", WSAGetLastError());
    continue;
}
~~~

`inet_ntop()` 的作用是把二进制 IP 转换为人能读懂的文本：

```text
二进制地址  ->  "127.0.0.1"
```

参数含义：

- `AF_INET`：说明地址是 IPv4。
- `&client_addr.sin_addr`：要转换的 IP 地址。
- `client_ip`：转换结果写入的字符数组。
- `sizeof(client_ip)`：字符数组大小，防止写越界。

转换成功后，`client_ip` 就可以传给 `printf()` 的 `%s`。

#### 为什么端口要调用 `ntohs()`

~~~c
(unsigned int)ntohs(client_addr.sin_port)
~~~

端口在网络中使用网络字节序，而本机 CPU 使用主机字节序。`ntohs()` 的含义是：

```text
network to host short
网络字节序 -> 主机字节序（16 位）
```

转换后才能正确打印端口号。例如：

~~~c
printf("received %d bytes from %s:%u: %s\n", received, client_ip,
       (unsigned int)ntohs(client_addr.sin_port), buffer);
~~~

可能输出：

```text
received 5 bytes from 127.0.0.1:52000: hello
```

#### 为什么可以用同一个地址回包

服务端收到数据后执行：

~~~c
int sent = sendto(server_sock, buffer, received, 0,
                  (const struct sockaddr *)&client_addr,
                  client_addr_len);
~~~

这里直接使用 `recvfrom()` 填好的 `client_addr`。它已经包含客户端的 IP 和端口，所以这段代码的意思是：

> 把刚才收到的内容，发送回刚才的发送者。

完整地址流转如下：

```text
客户端 127.0.0.1:52000
        |
        | sendto("hello", 服务端 127.0.0.1:9000)
        v
服务端 127.0.0.1:9000
        |
        | recvfrom() 把来源写入 client_addr
        | client_addr = 127.0.0.1:52000
        |
        | sendto(..., client_addr, ...)
        v
客户端收到 "hello"
```

#### 这段循环中的几个注意点

- `recvfrom()` 默认是阻塞调用。没有数据时停在这一行等待，是正常现象。
- 只有 `recvfrom()` 成功后，`client_addr` 中的地址才可用于回包。
- `buffer[received] = '\0'` 是为了把收到的字节临时当作 C 字符串打印；二进制数据不能这样处理。
- UDP 中 `recvfrom()` 返回 `0` 表示收到一个零长度数据报，不表示客户端关闭连接。UDP 没有 TCP 那种连接关闭语义。
- `sendto()` 成功只表示本机内核接受了数据报，不代表客户端一定已经收到或处理成功。

### 5.2 Windows 客户端：`udp_client_win.c`

~~~c
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "Ws2_32.lib")

#define SERVER_PORT 9000
#define BUFFER_SIZE 1024

int main(void) {
    WSADATA wsa_data;
    SOCKET client_sock;
    struct sockaddr_in server_addr = {0};
    char buffer[BUFFER_SIZE];

    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    client_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client_sock == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    printf("Input a message: ");
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        closesocket(client_sock);
        WSACleanup();
        return 1;
    }

    /* fgets() 可能保留换行符；删除它，让示例输出更直观。 */
    buffer[strcspn(buffer, "\r\n")] = '\0';
    int message_len = (int)strlen(buffer);

    int sent = sendto(client_sock, buffer, message_len, 0,
                      (const struct sockaddr *)&server_addr,
                      sizeof(server_addr));
    if (sent == SOCKET_ERROR) {
        printf("sendto failed: %d\n", WSAGetLastError());
        closesocket(client_sock);
        WSACleanup();
        return 1;
    }

    /* 服务端不存在时，避免 recvfrom() 永久等待。 */
    {
        DWORD timeout_ms = 3000;
        if (setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO,
                       (const char *)&timeout_ms,
                       sizeof(timeout_ms)) == SOCKET_ERROR) {
            printf("setsockopt failed: %d\n", WSAGetLastError());
            closesocket(client_sock);
            WSACleanup();
            return 1;
        }
    }

    printf("sent %d bytes; waiting for echo...\n", sent);
    {
        struct sockaddr_in reply_addr;
        int reply_addr_len = sizeof(reply_addr);
        int received = recvfrom(client_sock, buffer, BUFFER_SIZE - 1, 0,
                                (struct sockaddr *)&reply_addr,
                                &reply_addr_len);
        if (received == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAETIMEDOUT) {
                printf("Timed out: no reply within 3 seconds.\n");
            } else {
                printf("recvfrom failed: %d\n", error);
            }
            closesocket(client_sock);
            WSACleanup();
            return 1;
        }

        buffer[received] = '\0';
        printf("received %d bytes: %s\n", received, buffer);
    }

    closesocket(client_sock);
    WSACleanup();
    return 0;
}
~~~

客户端没有 `bind()`，服务端仍能回包：首次 `sendto()` 时 Windows 自动为客户端分配了本地临时端口。服务端从 `recvfrom()` 拿到这个端口，再将响应发送回去。

### 5.3 Windows 编译与运行

在两个 PowerShell 窗口进入源文件目录。使用 MinGW GCC：

~~~powershell
gcc -Wall -Wextra -g udp_server_win.c -o udp_server_win.exe -lws2_32
gcc -Wall -Wextra -g udp_client_win.c -o udp_client_win.exe -lws2_32
~~~

使用 Visual Studio Developer PowerShell：

~~~powershell
cl /W4 /Zi udp_server_win.c ws2_32.lib
cl /W4 /Zi udp_client_win.c ws2_32.lib
~~~

运行顺序：

~~~powershell
# 窗口 A：先启动服务端
.\udp_server_win.exe

# 窗口 B：再启动客户端
.\udp_client_win.exe
~~~

客户端输入 `hello udp` 后，预期输出：

```text
Input a message: hello udp
sent 9 bytes; waiting for echo...
received 9 bytes: hello udp
```

服务端会显示客户端的临时端口；该端口每次运行可能不同。

## 6. Linux/WSL2：等价回显程序

Linux/WSL2 的核心网络流程相同。主要差异是：不需要 `WSAStartup()`，Socket 是 `int` 文件描述符，关闭时调用 `close()`，错误使用 `perror()` 或 `errno` 查看。

### 6.1 Linux 服务端：`udp_server_linux.c`

~~~c
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_PORT 9000
#define BUFFER_SIZE 1024

int main(void) {
    int server_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    printf("UDP server is bound to 127.0.0.1:%d\n", SERVER_PORT);
    for (;;) {
        char buffer[BUFFER_SIZE];
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        char client_ip[INET_ADDRSTRLEN];

        ssize_t received = recvfrom(server_fd, buffer, BUFFER_SIZE - 1, 0,
                                    (struct sockaddr *)&client_addr,
                                    &client_addr_len);
        if (received < 0) {
            perror("recvfrom");
            continue;
        }

        buffer[received] = '\0';
        if (inet_ntop(AF_INET, &client_addr.sin_addr,
                      client_ip, sizeof(client_ip)) == NULL) {
            perror("inet_ntop");
            continue;
        }

        printf("received %zd bytes from %s:%u: %s\n", received, client_ip,
               (unsigned int)ntohs(client_addr.sin_port), buffer);

        ssize_t sent = sendto(server_fd, buffer, (size_t)received, 0,
                              (struct sockaddr *)&client_addr,
                              client_addr_len);
        if (sent < 0) {
            perror("sendto");
        } else {
            printf("echoed %zd bytes\n", sent);
        }
    }
}
~~~

### 6.2 Linux 客户端：`udp_client_linux.c`

~~~c
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define SERVER_PORT 9000
#define BUFFER_SIZE 1024

int main(void) {
    int client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client_fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    char buffer[BUFFER_SIZE];
    printf("Input a message: ");
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        close(client_fd);
        return 1;
    }

    buffer[strcspn(buffer, "\r\n")] = '\0';
    size_t message_len = strlen(buffer);
    ssize_t sent = sendto(client_fd, buffer, message_len, 0,
                          (struct sockaddr *)&server_addr,
                          sizeof(server_addr));
    if (sent < 0) {
        perror("sendto");
        close(client_fd);
        return 1;
    }

    {
        struct timeval timeout = {3, 0};
        /* Linux 的 SO_RCVTIMEO 使用 struct timeval。 */
        if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                       &timeout, sizeof(timeout)) < 0) {
            perror("setsockopt");
            close(client_fd);
            return 1;
        }
    }

    printf("sent %zd bytes; waiting for echo...\n", sent);
    {
        struct sockaddr_in reply_addr;
        socklen_t reply_addr_len = sizeof(reply_addr);
        ssize_t received = recvfrom(client_fd, buffer, BUFFER_SIZE - 1, 0,
                                    (struct sockaddr *)&reply_addr,
                                    &reply_addr_len);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("Timed out: no reply within 3 seconds.\n");
            } else {
                perror("recvfrom");
            }
            close(client_fd);
            return 1;
        }

        buffer[received] = '\0';
        printf("received %zd bytes: %s\n", received, buffer);
    }

    close(client_fd);
    return 0;
}
~~~

编译并分别在两个终端运行：

~~~bash
gcc -Wall -Wextra -g udp_server_linux.c -o udp_server_linux
gcc -Wall -Wextra -g udp_client_linux.c -o udp_client_linux

# 终端 A
./udp_server_linux

# 终端 B
./udp_client_linux
~~~

在 WSL2 中，使用 Linux 版本的头文件和命令。不要用 Windows 原生编译器去编译包含 `sys/socket.h`、`unistd.h` 的代码。

## 7. Windows 与 Linux 的关键差异

| 项目 | Windows 原生 Winsock | Linux/WSL2 |
| --- | --- | --- |
| 初始化 | `WSAStartup()` | 不需要 |
| Socket 类型 | `SOCKET` | `int` |
| 创建失败 | `INVALID_SOCKET` | 小于 0 |
| 调用失败 | `SOCKET_ERROR` | 通常小于 0 |
| 错误信息 | `WSAGetLastError()` | `errno`、`perror()` |
| 关闭 Socket | `closesocket()` | `close()` |
| 接收超时参数 | `DWORD` 毫秒数 | `struct timeval` |
| 链接 | `-lws2_32` 或 `ws2_32.lib` | 基础 Socket 通常无需额外库 |

## 8. 观察与排错

UDP 没有 TCP 的 `LISTEN`、`ESTABLISHED` 连接状态。服务端绑定端口后，可以观察 UDP 端口：

~~~powershell
# Windows PowerShell
Get-NetUDPEndpoint -LocalPort 9000
netstat -ano -p udp
~~~

~~~bash
# Linux/WSL2
ss -u -a -n
ss -u -a -n | grep 9000
~~~

常见问题：

| 现象 | 常见原因与检查方向 |
| --- | --- |
| `bind failed` | 端口已被占用；关闭旧服务端或换端口，客户端也要同步修改 |
| 客户端超时 | 服务端未启动、IP/端口不同、服务端未回包，或跨机器时被防火墙拦截 |
| 服务端看似卡住 | 正在阻塞等待 `recvfrom()`，是正常状态 |
| 收到内容不完整 | 接收缓冲区太小；不能靠下一次 `recvfrom()` 补回同一数据报 |

特别注意：UDP 接收长度为 0 表示收到了一个**零长度数据报**，不是“对端关闭”。这是 TCP 与 UDP 的重要区别，UDP 没有 TCP 那种连接关闭语义。

未 `connect()` 的 UDP Socket 可接收发往本地端口的任意来源数据。示例为教学简化，生产客户端应检查 `reply_addr` 是否确实是预期服务端，并校验数据长度、格式和身份。

## 9. 小结

```text
UDP 服务端：socket -> bind -> 循环 recvfrom -> 根据来源地址 sendto 回包
UDP 客户端：socket -> sendto -> recvfrom（应有超时）-> close

UDP 保留一次发送对应的一份数据报边界
UDP 不保证到达、顺序或不重复
发送成功不等于对端已收到或处理成功
```

## 实践

1. 按 Windows 示例启动服务端和客户端，连续运行客户端三次，观察服务端显示的客户端临时端口。
2. 暂时注释服务端的回显 `sendto()`，确认客户端三秒后超时。
3. 输入超过 1023 字节的文本，解释为什么该示例不适合处理它。
4. 将服务端从 `INADDR_LOOPBACK` 改为 `INADDR_ANY` 前，先说明两者允许哪些来源访问；不要在不受信任网络暴露未经认证的练习程序。
5. 在 Windows 与 Linux/WSL2 各运行一次，记录初始化、关闭、错误处理和超时设置的差异。

## 检查

- [ ] 我知道 UDP 服务端不需要 `listen()` 和 `accept()`。
- [ ] 我能说明 `sendto()` 的目标地址和 `recvfrom()` 的来源地址分别有什么作用。
- [ ] 我能解释 UDP 保留数据报边界的含义。
- [ ] 我知道 UDP 不保证到达、顺序或不重复。
- [ ] 我知道 `sendto()` 成功不代表业务处理成功。
- [ ] 我能解释为什么文本缓冲区要预留 `\0`。
- [ ] 我知道缓冲区不足时不能在下一次读取补回同一数据报。
- [ ] 我能在 Windows 与 Linux/WSL2 上分别编译并运行示例。
- [ ] 我知道 UDP 客户端不应无限等待响应。

## 关联

- [[01-基础入门/TCP与UDP概念]]
- [[01-基础入门/IP地址与端口]]
- [[01-基础入门/Socket编程环境]]
- [[01-基础入门/网络观察工具]]
- [[UDP客户端与服务器]]
- [[数据报与地址]]
- [[UDP回显项目]]
- [[UDP可靠性设计]]
