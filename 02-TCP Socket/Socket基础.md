---
type: topic
status: complete
created: 2026-08-25
updated: 2026-08-26
tags:
  - TCP
  - Socket
  - 基础入门
---

# Socket基础

## 学习目标

学完本章后，你应该能够：

- 解释 Socket 是什么，以及它和 TCP/IP 的关系。
- 看懂 socket() 的三个参数和返回值。
- 区分 Linux 文件描述符与 Windows Winsock 句柄。
- 理解 IPv4 地址、端口和网络字节序。
- 区分监听 Socket 与已连接 Socket。
- 画出 TCP Socket 从创建到关闭的基本生命周期。
- 识别常见的 socket、bind、connect 失败原因。

## 1. Socket 是什么

Socket（套接字）可以理解为：**应用程序与操作系统网络协议栈之间的一扇门**。

应用程序不需要自己实现网卡驱动、IP 路由、TCP 重传或 UDP 校验，而是通过 Socket API 把请求交给操作系统：

~~~text
应用程序
  -> socket、bind、listen、connect、send、recv
操作系统内核
  -> TCP/UDP、IP、路由、缓冲区、网卡驱动
网络
  -> 其他主机或本机回环接口
~~~

Socket 有两个常见含义：

1. **编程接口**：一组函数，例如 socket、bind、connect、send 和 recv。
2. **内核中的通信对象**：调用 socket 后，操作系统创建并维护的一份状态，包含协议类型、地址、缓冲区和连接状态等。

Socket 本身不是 TCP，也不是 IP 地址。可以这样记：

~~~text
Socket：程序使用网络能力的接口和对象
TCP：提供可靠字节流的传输协议
IP：负责把数据包送到目标主机
端口：在目标主机上找到对应进程
~~~

## 2. 一个 Socket 的组成

| 信息 | 作用 | 示例 |
| --- | --- | --- |
| 地址族 | 规定地址格式 | AF_INET（IPv4）、AF_INET6（IPv6） |
| Socket 类型 | 规定通信抽象 | SOCK_STREAM（字节流）、SOCK_DGRAM（数据报） |
| 协议 | 规定具体传输协议 | TCP、UDP |
| 本地地址 | 标识本机接口和端口 | 192.168.1.20:53124 |
| 对端地址 | 标识远端接口和端口 | 192.168.1.10:8080 |
| 内核缓冲区 | 暂存待发送和已接收的字节 | 发送缓冲区、接收缓冲区 |
| 状态 | 记录生命周期 | 监听、已连接、关闭等 |

对于 TCP 连接，四元组通常唯一确定一条连接：

~~~text
(源 IP, 源端口, 目的 IP, 目的端口)
~~~

所以同一个服务器端口可以同时服务许多客户端。服务器端口相同并不冲突，因为每个客户端的源 IP 或源端口通常不同。

## 3. 创建 Socket：socket()

Linux/POSIX 的函数原型通常是：

~~~c
int socket(int domain, int type, int protocol);
~~~

Windows Winsock 的原型返回 SOCKET，参数含义基本相同：

~~~c
SOCKET socket(int af, int type, int protocol);
~~~

### 3.1 第一个参数：地址族

| 参数                 | 含义                      |
| ------------------ | ----------------------- |
| AF_INET            | IPv4 地址，例如 127.0.0.1    |
| AF_INET6           | IPv6 地址，例如 ::1          |
| AF_UNIX / AF_LOCAL | 同一台机器上的本地进程通信，不经过 IP 网络 |

这里的 IPv6 地址 ::1 没有写错，它是 IPv6 的**回环地址**，作用类似 IPv4 的 127.0.0.1。IPv6 地址允许连续的 0 使用双冒号 :: 压缩表示：

~~~text
完整写法：0:0:0:0:0:0:0:1
压缩写法：::1
~~~

IPv6 回环地址 ::1 只能表示本机自身，不能作为局域网中其他电脑访问本机的地址。IPv6 的通配地址通常写作 ::，含义类似 IPv4 的 0.0.0.0 或 INADDR_ANY，表示监听本机符合条件的 IPv6 网卡地址。

初学 TCP/IPv4 时通常使用 AF_INET。地址族决定后面使用哪种地址结构：IPv4 使用 struct sockaddr_in，IPv6 使用 struct sockaddr_in6。

### 3.2 第二个参数：Socket 类型

| 类型 | 常见对应协议 | 特点 |
| --- | --- | --- |
| SOCK_STREAM | TCP | 面向连接、可靠、有序字节流 |
| SOCK_DGRAM | UDP | 无连接、独立数据报、保留数据报边界 |
| SOCK_SEQPACKET | 少数协议 | 有序且保留消息边界，初学阶段较少使用 |

SOCK_STREAM 表达“字节流”这一抽象；在互联网 IPv4 中，AF_INET + SOCK_STREAM + 0 通常得到 TCP Socket。

### 3.3 第三个参数：具体协议

传 0 通常表示让系统根据地址族和类型选择默认协议：

~~~c
int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
~~~

也可以显式写 IPPROTO_TCP 或 IPPROTO_UDP，让代码意图更清晰：

~~~c
int tcp_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
int udp_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
~~~

### 3.4 返回值和错误检查

Linux 成功时返回非负文件描述符，失败时返回 -1：

~~~c
int fd = socket(AF_INET, SOCK_STREAM, 0);
if (fd == -1) {
    perror("socket");
    return 1;
}
~~~

Windows 成功时返回有效 SOCKET，失败时返回 INVALID_SOCKET：

~~~c
SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
if (sock == INVALID_SOCKET) {
    printf("socket failed: %d\n", WSAGetLastError());
    return 1;
}
~~~

不要把 Linux 的 fd == -1 判断原样复制到 Windows，也不要在 Windows 中用 close 代替 closesocket。平台差异详见 [[01-基础入门/Socket编程环境]]。

## 4. 文件描述符、句柄和缓冲区

### 4.1 Linux：Socket 是文件描述符

Linux 把许多输入输出对象统一抽象成文件描述符（file descriptor，简称 fd）。普通文件、管道和 Socket 都可以用整数表示：

~~~c
int fd = socket(AF_INET, SOCK_STREAM, 0);
~~~

这个整数不是 IP 地址，也不是端口号，而是当前进程用来查找内核 Socket 对象的索引。send(fd, ...) 和 recv(fd, ...) 通过它告诉内核要操作哪一个 Socket。

进程退出或调用 close(fd) 后，这个描述符就不再指向该 Socket。描述符编号以后可能被系统重新分配，不要把 fd 当成永久身份。

### 4.2 Windows：Winsock 句柄

Windows 通常使用 SOCKET 类型。它在概念上和 Linux fd 类似，都是程序操作内核通信对象的句柄，但类型、失败值和关闭函数不同。跨平台代码应使用条件编译或封装函数。

### 4.3 发送成功表示什么

send 返回正数时，通常只表示本机内核接受了这部分数据。它不保证对端已经收到、读取、解析或完成业务。要确认业务结果，仍需等待应用层响应。TCP 字节流和部分读写见 [[01-基础入门/字节、编码与数据边界]]。

## 5. 地址结构：IP、端口与网络字节序

### 5.1 IPv4 地址结构

Linux/POSIX 中常见的 IPv4 地址结构是：

~~~c
struct sockaddr_in address = {0};
address.sin_family = AF_INET;
address.sin_port = htons(8080);
address.sin_addr.s_addr = htonl(INADDR_ANY);
~~~

| 字段 | 含义 |
| --- | --- |
| sin_family | 地址族，IPv4 时是 AF_INET |
| sin_port | 端口，必须使用网络字节序 |
| sin_addr | IPv4 地址 |

### 5.2 为什么端口要 htons

不同 CPU 可能使用不同的字节排列方式。网络协议统一规定多字节整数使用网络字节序（通常是大端）：

~~~text
主机字节序  -- htons/htonl -->  网络字节序
网络字节序  -- ntohs/ntohl -->  主机字节序
~~~

端口是 16 位整数，所以写成 htons(8080)。常见转换函数如下：

| 函数 | 作用 |
| --- | --- |
| htons | host to network short，主机转网络 16 位 |
| ntohs | network to host short，网络转主机 16 位 |
| htonl | host to network long，主机转网络 32 位 |
| ntohl | network to host long，网络转主机 32 位 |

### 5.3 地址字符串如何转换

不要把字符串 127.0.0.1 直接赋给 sin_addr，可以使用：

~~~c
inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
~~~

inet_pton 把人可读的地址文本转换为网络地址格式；反向显示时可以使用 inet_ntop。支持 IPv4 和 IPv6 的程序通常使用 getaddrinfo，后续客户端章节会介绍。

### 5.4 sockaddr 为什么要强制转换

IPv4 使用 sockaddr_in，IPv6 使用 sockaddr_in6，而 bind、connect 等通用函数接收 struct sockaddr 指针：

~~~c
bind(fd, (struct sockaddr *)&address, sizeof address);
~~~

强制转换不会改变结构内容，只是告诉编译器：把这个具体地址结构按通用 Socket 地址指针传给函数，函数会根据地址族解释它。

## 6. TCP Socket 的基本生命周期

### 6.1 服务器端

~~~text
socket()
  -> bind()       把 Socket 绑定到本地 IP 和端口
  -> listen()     进入监听状态，等待连接
  -> accept()     取出一个客户端连接，得到新的已连接 Socket
  -> recv()/send()
  -> close()
~~~

listen Socket 和 accept 返回的连接 Socket 是两个不同对象：

~~~text
监听 Socket：只负责等待新连接
已连接 Socket：负责与某一个客户端收发数据
~~~

服务器关闭某个客户端连接时，只关闭对应的已连接 Socket；监听 Socket 仍可继续接受其他客户端。

### 6.2 客户端

~~~text
socket()
  -> connect()    向服务器地址和端口发起 TCP 连接
  -> send()/recv()
  -> close()
~~~

客户端通常不需要手动 bind。调用 connect 时，操作系统会根据路由选择本地 IP，并自动分配临时端口。如果业务必须使用固定源端口，才需要显式绑定。

### 6.3 服务器为什么必须先 bind

客户端需要知道服务器去哪个地址和端口寻找服务。服务器通过 bind 声明“这个 Socket 使用本机某个地址和端口”，再通过 listen 表示“我准备接受 TCP 连接”。

常见服务器片段：

~~~c
int fd = socket(AF_INET, SOCK_STREAM, 0);
if (fd < 0) {
    perror("socket");
    return 1;
}

struct sockaddr_in address = {0};
address.sin_family = AF_INET;
address.sin_port = htons(8080);
address.sin_addr.s_addr = htonl(INADDR_ANY);

if (bind(fd, (struct sockaddr *)&address, sizeof address) < 0) {
    perror("bind");
    close(fd);
    return 1;
}

if (listen(fd, 16) < 0) {
    perror("listen");
    close(fd);
    return 1;
}
~~~

INADDR_ANY 表示监听本机所有符合条件的 IPv4 网卡地址。若只绑定 127.0.0.1，通常只能接受本机连接；绑定地址的选择见 [[01-基础入门/IP地址与端口]]。

上面这段是 Linux/POSIX 写法。Windows 原生程序使用 Winsock，主要差异是：先调用 WSAStartup()，Socket 类型是 SOCKET，失败值是 INVALID_SOCKET，关闭时使用 closesocket()，错误通过 WSAGetLastError() 获取。

Windows Winsock 对应示例：

~~~c
#include <winsock2.h>
#include <stdio.h>

int main(void) {
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        printf("WSAStartup failed: %d\n", result);
        return 1;
    }

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_sock,
             (const struct sockaddr *)&address,
             sizeof address) == SOCKET_ERROR) {
        printf("bind failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    if (listen(listen_sock, 16) == SOCKET_ERROR) {
        printf("listen failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    printf("listening on port 8080\n");

    /* 后续在这里调用 accept()，接收客户端连接 */

    closesocket(listen_sock);
    WSACleanup();
    return 0;
}
~~~

这段 Windows 代码的执行顺序仍然是：

~~~text
WSAStartup()
  -> socket()
  -> bind()
  -> listen()
  -> accept()
  -> closesocket()
  -> WSACleanup()
~~~

如果使用 MinGW 编译，通常需要链接 Winsock 库：

~~~powershell
gcc -Wall -Wextra -g server.c -o server.exe -lws2_32
~~~

如果使用 Visual Studio Developer PowerShell，通常写：

~~~powershell
cl /W4 /Zi server.c ws2_32.lib
~~~

这里的 Windows 示例暂时只完成“创建、绑定、监听”，还没有进入 accept() 和收发数据；完整的客户端/服务器流程见 [[TCP服务器]] 和 [[TCP客户端]]。

### 6.4 accept 为什么返回新 Socket

listen Socket 要一直保留，才能继续等待新客户端。如果它同时负责与客户端 A 收发，又要等待客户端 B，状态会混在一起。因此 accept 为每个客户端提供独立的已连接 Socket：

~~~text
监听 fd = 3
  accept() -> 客户端 A 的连接 fd = 4
  accept() -> 客户端 B 的连接 fd = 5
  监听 fd = 3 继续等待
~~~

可以把监听 Socket 想象成前台接待处，把已连接 Socket 想象成分配给每位客户的独立房间。

## 7. 阻塞行为：为什么程序看起来卡住了

默认情况下，Socket 通常是阻塞模式。阻塞调用会等待条件满足：

| 调用 | 可能等待什么 |
| --- | --- |
| accept | 新客户端连接 |
| connect | 连接建立、失败或超时 |
| recv | 数据到达、对端关闭或错误 |
| send | 发送缓冲区有空间，或发送失败 |

例如：

~~~c
int client_fd = accept(listen_fd, NULL, NULL);
~~~

如果暂时没有客户端，程序停在这一行通常是正常等待，不代表崩溃。后续学习非阻塞和 I/O 多路复用时，会用事件驱动方式处理多个 Socket，见 [[04-并发与I-O模型/阻塞与非阻塞]]。

## 8. 常见错误与排查方向

| 调用 | 常见现象 | 初步排查 |
| --- | --- | --- |
| socket | 参数不支持、资源不足 | 检查地址族、类型、协议和系统资源 |
| bind | 地址已使用、权限不足 | 检查端口占用、绑定地址、特权端口 |
| listen | Socket 类型或状态不对 | 确认使用 SOCK_STREAM 且 bind 成功 |
| accept | 被信号中断或监听 Socket 出错 | 检查返回值和错误码 |
| connect | 超时、拒绝、地址不可达 | 检查服务端 LISTEN、IP/端口、防火墙和路由 |
| send/recv | 部分读写、连接关闭 | 按返回值处理，不假设一次完成 |

排错时记录哪个调用失败、返回值和错误码。只记录“连接失败”通常不够。可以使用 [[01-基础入门/网络观察工具]] 查看端口和 TCP 状态。

## 9. 最小知识串联

~~~text
socket() 创建内核通信对象
  -> 服务器 bind() 绑定地址和端口
  -> 服务器 listen() 等待连接
  -> 客户端 connect() 发起连接
  -> 服务器 accept() 得到专属连接 Socket
  -> 双方 send()/recv() 交换字节
  -> 双方 close() 释放资源
~~~

这只是 TCP 的阻塞式基础流程。真实程序还要处理部分读写、超时、信号中断、并发、消息边界、异常断线和输入校验。

## 实践

### 实践一：观察文件描述符

1. 编写程序调用 socket 并打印返回的 fd。
2. 再打开一个普通文件并打印文件 fd，比较它们都是当前进程中的整数描述符。
3. 调用 close 后不要继续使用已关闭的 fd。

### 实践二：只完成监听

1. 创建 IPv4 TCP Socket。
2. 绑定到回环地址 127.0.0.1 和一个未占用端口。
3. 调用 listen。
4. 在另一个终端用 ss 或 netstat 查看 LISTEN。
5. 关闭程序后再次查看，确认监听项消失。

### 实践三：画出两个 Socket

连接一个客户端后，画出：

~~~text
监听 Socket
  ├─ 连接 Socket A <-> 客户端 A
  └─ 连接 Socket B <-> 客户端 B
~~~

在图中标注每个 Socket 的本地地址、远端地址和用途。

## 检查

- [ ] 我能解释 Socket 是应用程序使用操作系统网络能力的接口和内核对象。
- [ ] 我能说出 socket(domain, type, protocol) 三个参数的含义。
- [ ] 我能区分 AF_INET、SOCK_STREAM 和 TCP。
- [ ] 我能解释 Linux fd 与 Windows SOCKET 的关系和差异。
- [ ] 我知道端口要使用网络字节序转换。
- [ ] 我能解释为什么服务器需要 bind 和 listen。
- [ ] 我能解释监听 Socket 和 accept 返回的连接 Socket 的区别。
- [ ] 我知道阻塞在 accept 或 recv 上可能是正常等待。
- [ ] 我会检查系统调用返回值，并记录错误码。

## 关联

- [[01-基础入门/网络通信概览]]
- [[01-基础入门/IP地址与端口]]
- [[01-基础入门/TCP与UDP概念]]
- [[01-基础入门/Socket编程环境]]
- [[01-基础入门/网络观察工具]]
- [[TCP客户端]]
- [[TCP服务器]]
- [[连接生命周期]]
- [[发送与接收数据]]
- [[消息边界与拆包]]
- [[04-并发与I-O模型/阻塞与非阻塞]]
- [[08-C与Linux深入/Linux Socket系统调用]]
