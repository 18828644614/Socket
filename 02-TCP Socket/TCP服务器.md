---
type: topic
status: complete
created: 2026-08-25
updated: 2026-08-27
tags:
  - TCP
  - 服务器
  - Socket
---

# TCP服务器

## 学习目标

学完本章后，你应该能够：

- 说出 TCP 服务器从启动到关闭的基本流程。
- 区分**监听 Socket**和 `accept()` 返回的**已连接 Socket**。
- 理解 `bind()`、`listen()`、`accept()` 分别解决什么问题。
- 写出一个一次处理一个客户端的 TCP 回显服务器。
- 看懂 `accept()`、`recv()` 发生阻塞时程序为什么“停住”。
- 正确处理 `recv()` 的三种返回结果和 `send()` 的部分发送。
- 根据“端口被占用”“连不上”“服务器只能处理一个人”等现象定位问题。

## 1. TCP 服务器到底做什么

TCP 服务器是**被动等待连接的一方**。客户端知道服务器的 IP 和端口，调用 `connect()` 发起连接；服务器则提前把这个端口准备好，接受连接并与客户端收发数据。

最基础的阻塞式服务器流程如下：

~~~text
socket()        创建一个 TCP Socket
   ↓
bind()          绑定本机 IP 和端口，例如 127.0.0.1:8080
   ↓
listen()        把它变为“监听 Socket”，开始等待连接
   ↓
accept()        取出一个客户端连接，得到新的“已连接 Socket”
   ↓
recv()/send()   使用已连接 Socket 与这个客户端通信
   ↓
close()         关闭这个客户端的连接；返回 accept() 继续等待下一个
~~~

注意：上图最后的 `close()` 通常关闭的是**客户端连接 Socket**，不是监听 Socket。监听 Socket 应一直保留到服务器真正停止时才关闭。

### 1.1 先建立整体画面

假设服务器监听 `127.0.0.1:8080`，两个客户端先后连接：

~~~text
客户端 A  ---- connect() ---->  监听 Socket（127.0.0.1:8080）
                                      |
                                      | accept()
                                      v
                               连接 Socket A <----> 客户端 A

客户端 B  ---- connect() ---->  同一个监听 Socket
                                      |
                                      | accept()
                                      v
                               连接 Socket B <----> 客户端 B
~~~

服务器的监听端口始终是 `8080`，但每条 TCP 连接由四元组区分：

~~~text
(客户端 IP, 客户端端口, 服务器 IP, 服务器端口)
~~~

因此，许多客户端可以同时连接同一个 `8080` 端口；它们通常拥有不同的客户端临时端口。

## 2. 服务器端的两个 Socket：最重要的概念

初学者最容易把 `listen_fd` 和 `client_fd` 当成同一个对象。它们其实职责完全不同：

| 对象 | 从哪里得到 | 用途 | 是否与某个客户端一一对应 |
| --- | --- | --- | --- |
| 监听 Socket | `socket()` 后再 `bind()`、`listen()` | 等待新的连接请求 | 否，一个服务器通常只有一个 |
| 已连接 Socket | `accept()` 的返回值 | 对一个客户端 `recv()`、`send()` | 是，一条连接一个 |

可以把监听 Socket 想成餐厅的接待台：它只负责接待新客人。`accept()` 相当于给每位客人分配一张独立餐桌；后续点菜、上菜都发生在该客人的桌子上，而不是接待台上。

### 2.1 为什么 `accept()` 必须返回新 Socket

如果监听 Socket 既要等待新连接，又要与客户端 A 收发数据，那么客户端 B 到来时，系统无法清晰地区分两种状态。TCP 的设计把它们分开：

~~~text
listen_fd = 3                  只负责等待新连接
accept(listen_fd, ...) -> 4    fd 4 只服务客户端 A
accept(listen_fd, ...) -> 5    fd 5 只服务客户端 B
~~~

关闭 `4` 只会断开客户端 A，不会影响 `3` 继续接受客户端 B。反过来，若关闭 `3`，已有的 `4`、`5` 通常仍可继续通信，但服务器不能再接受新客户端。

## 3. 创建 Socket：`socket()`

Linux/POSIX 上创建 IPv4 TCP Socket：

~~~c
int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
if (listen_fd == -1) {
    perror("socket");
    return 1;
}
~~~

三个参数的含义：

| 参数 | 本例取值 | 含义 |
| --- | --- | --- |
| 地址族 | `AF_INET` | 使用 IPv4 地址结构 `sockaddr_in` |
| 类型 | `SOCK_STREAM` | 使用面向连接的字节流 Socket |
| 协议 | `0` | 由系统按前两项选择 TCP；也可写 `IPPROTO_TCP` |

此时 Socket 只是一个内核通信对象，尚未有固定端口，也没有在等待客户端。服务器需要继续执行 `bind()` 和 `listen()`。

## 4. 绑定地址：`bind()`

`bind()` 的作用是把 Socket 和**本机的地址、端口**关联起来。服务器必须让客户端知道去哪里找它，所以通常要显式绑定一个稳定端口。

~~~c
struct sockaddr_in server_addr = {0};
server_addr.sin_family = AF_INET;
server_addr.sin_port = htons(8080);
server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

if (bind(listen_fd,
         (struct sockaddr *)&server_addr,
         sizeof server_addr) == -1) {
    perror("bind");
    close(listen_fd);
    return 1;
}
~~~

### 4.1 地址结构逐项解释

`sockaddr_in` 是 IPv4 地址专用结构。`= {0}` 会先把所有字段清零，避免未初始化数据造成问题。

| 字段 | 含义 | 本例 |
| --- | --- | --- |
| `sin_family` | 地址族 | `AF_INET`，必须与 `socket()` 一致 |
| `sin_port` | 端口号，采用网络字节序 | `htons(8080)` |
| `sin_addr` | IPv4 地址，采用网络格式 | `INADDR_LOOPBACK` 即 `127.0.0.1` |

`htons()` 的 `h` 是 host（主机），`n` 是 network（网络），`s` 是 short（16 位）。端口是 16 位整数，而网络协议规定多字节整数要使用网络字节序，因此不能直接写 `8080`。

`bind()` 接收通用的 `struct sockaddr *`，而 `server_addr` 是更具体的 `struct sockaddr_in`；这个指针转换不会复制或修改数据，只是让同一个通用 API 能接收 IPv4、IPv6 等不同的地址结构。

### 4.2 绑定 `127.0.0.1`、`0.0.0.0` 还是具体网卡地址

绑定哪个地址决定了哪些网络接口能访问服务：

| 绑定地址 | 它表示什么 | 谁能连接 |
| --- | --- | --- |
| `127.0.0.1` / `INADDR_LOOPBACK` | 本机回环接口 | 只有本机程序，适合练习 |
| `0.0.0.0` / `INADDR_ANY` | 所有本机 IPv4 接口 | 本机和能到达该主机的其他设备，受防火墙影响 |
| 例如 `192.168.1.20` | 一张指定网卡的地址 | 只能通过这个地址连接 |

第一次学习建议绑定 `127.0.0.1`，因为它不会把练习服务器暴露到局域网。需要让局域网客户端访问时，再改为 `INADDR_ANY` 或明确的局域网地址，并根据系统防火墙规则放行端口。

### 4.3 `bind()` 常见失败：端口已被占用

`bind()` 失败并显示 `Address already in use`，通常是以下之一：

1. 另一个程序正在监听相同 IP 和端口。
2. 上一次服务器仍在运行。
3. 服务器刚退出，连接还处于 TCP 的 `TIME_WAIT` 等状态，系统暂时不允许立即复用该地址。

服务器通常会在 `bind()` 前设置 `SO_REUSEADDR`：

~~~c
int opt = 1;
if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof opt) == -1) {
    perror("setsockopt(SO_REUSEADDR)");
    close(listen_fd);
    return 1;
}
~~~

它主要帮助服务器在重启时更顺利地重新绑定本地地址。它**不表示**两个普通程序可以随意抢占同一个端口；具体复用规则还与操作系统和绑定地址有关。

## 5. 开始监听：`listen()`

`listen()` 把已经绑定的 TCP Socket 切换为监听状态：

~~~c
if (listen(listen_fd, 16) == -1) {
    perror("listen");
    close(listen_fd);
    return 1;
}
~~~

调用成功后，操作系统开始为该端口处理新到的 TCP 连接请求。`listen()` 本身不接收业务数据，也不会返回客户端 Socket；这两件事由后面的 `accept()` 和 `recv()` 完成。

第二个参数常被称为 `backlog`（监听队列长度提示）。可以先把它理解为“应用暂时来不及 `accept()` 时，内核最多愿意排队多少个已完成或正在完成的连接请求”的提示值。实际有效长度由系统内核限制和调整，不能把它简单理解成“服务器最大客户端数量”。

例如，单线程服务器即使写了 `listen(listen_fd, 128)`，若它正花很长时间处理客户端 A，客户端 B 也只是暂时排队，并没有被应用程序真正处理。并发模型会在 [[04-并发与I-O模型/线程与进程模型]] 和 [[04-并发与I-O模型/I-O多路复用]] 中学习。

## 6. 接受连接：`accept()`

~~~c
struct sockaddr_in client_addr;
socklen_t client_addr_len = sizeof client_addr;

int client_fd = accept(listen_fd,
                       (struct sockaddr *)&client_addr,
                       &client_addr_len);
if (client_fd == -1) {
    perror("accept");
}
~~~

`accept()` 的三个参数分别是：

| 参数 | 作用 |
| --- | --- |
| `listen_fd` | 已经进入监听状态的 Socket |
| `client_addr` | 输出参数：内核在这里写入客户端 IP、端口等信息；不关心时可传 `NULL` |
| `client_addr_len` | 输入时写入缓冲区大小，返回时写入实际地址长度；不关心地址时也可传 `NULL` |

### 6.1 为什么 `accept()` 看起来卡住

默认 Socket 是阻塞模式。如果暂时没有客户端连接，`accept()` 会等待：

~~~text
服务器运行到 accept()
        ↓
没有客户端：停在这里等待，这是正常现象
        ↓
客户端调用 connect()
        ↓
握手完成，accept() 返回一个新的 client_fd
~~~

这不是死循环，也不代表程序崩溃。可以在另一个终端启动 [[TCP客户端]] 中的客户端，或使用 `nc`/`telnet`（若系统已安装）连接该端口来唤醒它。

`accept()` 成功只是说明 TCP 连接建立了，不表示客户端已经发送完整业务请求。业务数据仍需要在 `client_fd` 上使用 `recv()` 读取。

### 6.2 显示客户端地址

`accept()` 成功后，可用 `inet_ntop()` 和 `ntohs()` 显示对端：

~~~c
char ip[INET_ADDRSTRLEN];
inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof ip);
printf("client connected: %s:%u\n", ip, ntohs(client_addr.sin_port));
~~~

`inet_ntop()` 把二进制网络地址转换成人可读文本；`ntohs()` 则把网络字节序端口转换回主机字节序。不要直接打印 `sin_port`，否则在小端机器上常会得到错误的数字。

## 7. 最小可运行的 Linux 回显服务器

下面的程序监听 `127.0.0.1:8080`。每当一个客户端连接时，它读取客户端发来的字节，并把读到的每一段字节原样返回（称为**回显**）。客户端关闭连接后，服务器再等待下一个客户端。

~~~c
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

static int send_all(int fd, const char *buffer, size_t len) {
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, buffer + sent, len - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        perror("send");
        return -1;
    }
    return 0;
}

int main(void) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        return 1;
    }

    /* 客户端突然关闭时，send() 不会让整个进程因 SIGPIPE 退出。 */
    signal(SIGPIPE, SIG_IGN);

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof opt) == -1) {
        perror("setsockopt(SO_REUSEADDR)");
        close(listen_fd);
        return 1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listen_fd,
             (struct sockaddr *)&server_addr,
             sizeof server_addr) == -1) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 16) == -1) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("listening on 127.0.0.1:%d\n", SERVER_PORT);

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof client_addr;
        int client_fd = accept(listen_fd,
                               (struct sockaddr *)&client_addr,
                               &client_addr_len);
        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }

        char ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &client_addr.sin_addr,
                      ip, sizeof ip) == NULL) {
            strcpy(ip, "<unknown>");
        }
        printf("client connected: %s:%u\n",
               ip, ntohs(client_addr.sin_port));

        char buffer[BUFFER_SIZE];
        for (;;) {
            ssize_t count = recv(client_fd, buffer, sizeof buffer, 0);
            if (count > 0) {
                printf("received %zd bytes\n", count);
                if (send_all(client_fd, buffer, (size_t)count) == -1) {
                    break;
                }
            } else if (count == 0) {
                printf("client closed the connection\n");
                break;
            } else if (errno != EINTR) {
                perror("recv");
                break;
            }
        }

        close(client_fd);
    }

    close(listen_fd);
    return 0;
}
~~~

编译和运行（Linux）：

~~~bash
gcc -Wall -Wextra -Wpedantic -g server.c -o server
./server
~~~

在另一个终端运行本章对应的客户端，或在已安装 `nc` 的 Linux 环境中执行：

~~~bash
nc 127.0.0.1 8080
~~~

输入一行文字并按回车，服务器会将字节回显。按 `Ctrl+D` 发送输入结束，客户端关闭后服务器会回到 `accept()` 等待下一位客户端。

### 7.1 按执行顺序读懂示例

1. `socket()` 创建 `listen_fd`，它还没有端口和客户端。
2. `SO_REUSEADDR` 在绑定前配置；如果配置失败就停止，因为后续状态不完整。
3. `server_addr` 指定 IPv4、端口 `8080` 与回环地址。
4. `bind()` 给 Socket 分配本地地址，`listen()` 让它开始接受连接。
5. 外层 `for (;;)` 反复调用 `accept()`，所以服务器可以依次服务多个客户端。
6. 每次 `accept()` 成功都会产生一个新的 `client_fd`；`client_addr` 是刚连入客户端的地址。
7. 内层 `for (;;)` 在 `client_fd` 上反复 `recv()`，直到对端关闭、发生错误或回显发送失败。
8. `send_all()` 处理部分发送；把所有这次读到的字节都发送完，才继续下一次 `recv()`。
9. 内层循环结束后只关闭 `client_fd`，外层循环再次等待新客户端。

### 7.2 这个示例的能力边界

它很适合理解基础流程，但还不是生产服务器：

- 一次只处理一个客户端。服务客户端 A 时，B 只能在内核队列中等待。
- 它按“读到多少就回显多少”工作，不能解析聊天消息、文件等有明确边界的业务消息。
- 它没有退出信号处理、日志、认证、超时、并发和资源上限。

后续可依次学习 [[发送与接收数据]]、[[消息边界与拆包]]、[[超时、断线与异常]]，再进入并发与 I/O 模型章节。

## 8. `recv()` 和 `send()`：连接建立后如何正确收发

### 8.1 `recv()` 的三种结果

~~~c
ssize_t count = recv(client_fd, buffer, sizeof buffer, 0);
~~~

| 返回值 | 含义 | 服务器通常怎么做 |
| --- | --- | --- |
| `count > 0` | 收到 `count` 个字节 | 处理这批字节，或继续累计解析 |
| `count == 0` | 对端有序关闭了它的发送方向 | 结束该客户端会话，关闭 `client_fd` |
| `count < 0` | 出现错误 | 检查 `errno`；`EINTR` 通常可重试，其他错误通常结束本次会话 |

`recv() == 0` **不是**“暂时没有数据”。阻塞模式下，暂时没有数据时 `recv()` 继续等待；返回 `0` 说明对端不会再发送后续字节。

### 8.2 为什么一次 `recv()` 不是一条消息

TCP 提供的是连续、有序的**字节流**，不保留调用 `send()` 时的消息边界。客户端执行：

~~~c
send(fd, "HELLO", 5, 0);
send(fd, "WORLD", 5, 0);
~~~

服务器可能读到下列任意拆分方式：

~~~text
一次 recv():  HELLOWORLD
两次 recv():  HELLO / WORLD
三次 recv():  HEL / LOWO / RLD
~~~

这些结果都正确。因此，示例中的“读多少回显多少”适用于回显练习；真实协议必须约定消息边界，例如固定长度、换行分隔符或长度前缀。详细设计见 [[消息边界与拆包]]。

### 8.3 为什么 `send()` 要循环

`send()` 返回正数时，只说明本机内核接收了这部分字节；它可能小于你要求发送的长度。比如你要发送 1000 字节，`send()` 可能只返回 600，此时从第 601 字节继续发送。

~~~text
已发送 sent = 0
send(..., 1000) 返回 600
已发送 sent = 600
send(..., 400) 继续发送剩余部分
~~~

这就是 `send_all()` 存在的原因。并且，`send()` 成功不等于客户端程序已经读取或处理成功；如果业务需要确认，必须由应用协议让客户端返回明确响应。

### 8.4 `SIGPIPE` 是什么，为什么示例忽略它

在 Linux 上，若对端已经断开，而服务器仍向该连接发送数据，`send()` 除了可能返回 `EPIPE`，默认还可能让进程收到 `SIGPIPE` 信号并直接退出。

示例用 `signal(SIGPIPE, SIG_IGN)` 忽略这个信号，使 `send()` 以错误返回值的形式报告失败，从而只关闭出问题的客户端连接。生产程序通常会使用更严格的信号处理方式或平台专用选项；初学阶段先记住：**客户端突然断开是正常网络事件，服务器不能因此整体退出。**

## 9. 阻塞与“只能处理一个客户端”

本章示例是**串行阻塞服务器**。它有两个可能等待的位置：

| 调用 | 等待条件 | 等待时服务器在做什么 |
| --- | --- | --- |
| `accept()` | 新客户端连接 | 空闲地等待下一位客户端 |
| `recv(client_fd, ...)` | 当前客户端的数据、关闭或错误 | 只等待当前客户端 |

假设客户端 A 连上后一直不发送数据：

~~~text
服务器 accept() 得到 A
        ↓
服务器阻塞在 recv(A)
        ↓
客户端 B 连入：连接可暂时排队
        ↓
服务器不会 accept(B)，直到 A 发送、关闭或发生错误
~~~

这并不是 TCP 的限制，而是本章示例选择了最简单的控制流程。常见升级方式有：

- 每个客户端一个线程或进程；
- 使用 `select`、`poll`、`epoll` 等 I/O 多路复用；
- 使用非阻塞 Socket 加事件循环。

不要在还没理解两个 Socket、返回值和消息边界时就急着上多线程；并发会放大资源管理和协议解析错误。基础扎实后再学习 [[04-并发与I-O模型/线程与进程模型]] 与 [[04-并发与I-O模型/I-O多路复用]]。

## 10. Windows Winsock 对应关系

Windows 的服务器流程与 Linux 完全相同，只是 API 类型和初始化方式不同：

~~~text
WSAStartup()
  -> socket()
  -> bind()
  -> listen()
  -> accept()
  -> recv()/send()
  -> closesocket()
  -> WSACleanup()
~~~

| 项目 | Linux/POSIX | Windows Winsock |
| --- | --- | --- |
| Socket 类型 | `int` | `SOCKET` |
| 创建失败 | `-1` | `INVALID_SOCKET` |
| 通用调用失败 | 通常返回 `-1` | 通常返回 `SOCKET_ERROR` |
| 初始化 | 通常不需要 | 先 `WSAStartup()` |
| 获取错误 | `errno`、`perror()` | `WSAGetLastError()` |
| 关闭连接 | `close(fd)` | `closesocket(sock)` |
| 清理库 | 无 | 最后 `WSACleanup()` |

Windows 中 `accept()` 的返回值仍是新的客户端 Socket，监听 Socket 与连接 Socket 的职责也完全一样。编译 Winsock 程序时需要链接库：

~~~powershell
# MinGW
gcc -Wall -Wextra -g server.c -o server.exe -lws2_32

# Visual Studio Developer PowerShell
cl /W4 /Zi server.c ws2_32.lib
~~~

本仓库的 [server.c](../src/socket/server.c) 提供了 Windows 下“创建、绑定、监听”的对应起点；完成 `accept()` 与收发的思路与本章 Linux 示例相同。

## 11. 常见错误与排查

| 现象 | 常见原因 | 先做什么 |
| --- | --- | --- |
| `bind: Address already in use` | 端口已有程序监听，或旧连接状态影响重启 | 检查是否有旧服务器；确认端口；设置 `SO_REUSEADDR` |
| 客户端 `Connection refused` | 服务器未启动、未成功 `listen()`，或端口写错 | 查看服务器日志与监听端口 |
| 外部电脑连不上，但本机能连 | 服务只绑定了 `127.0.0.1`，或防火墙拦截 | 视需求改为 `INADDR_ANY`，检查防火墙 |
| 服务器停在 `accept()` | 暂时没有客户端 | 启动客户端连接；这是正常等待 |
| 服务器停在 `recv()` | 当前客户端还未发数据 | 核对客户端是否发送，确认协议结束条件 |
| 只要一个客户端不动，其他客户端就没响应 | 单线程串行处理 | 学习线程、进程或 I/O 多路复用 |
| 收到的文本缺半截或粘在一起 | 把一次 `recv()` 错当完整消息 | 定义并实现消息边界 |
| 服务器向断开的客户端发送后退出 | 未处理 `SIGPIPE` 或发送错误 | 检查 `send()` 返回值并处理 `SIGPIPE` |

Linux 可使用 `ss -ltn` 或 `netstat -an`（取决于系统是否安装）观察监听状态。排错时请至少记录：绑定 IP、端口、失败的 API、返回值和错误码；只说“服务器打不开”通常不足以定位问题。

## 12. 实践

### 实践一：启动并连接回显服务器

1. 将示例保存为 `server.c`，编译后运行它。
2. 启动 [[TCP客户端]] 中的客户端，连接 `127.0.0.1:8080`。
3. 观察服务器输出的客户端 IP、端口与接收字节数。
4. 关闭客户端，确认服务器打印“client closed”，随后继续等待新连接。

### 实践二：观察两个 Socket

在 `accept()` 成功后打印：

~~~c
printf("listen_fd=%d, client_fd=%d\n", listen_fd, client_fd);
~~~

连续连接两次客户端，观察 `listen_fd` 保持不变，而每次 `accept()` 得到的 `client_fd` 可能不同。解释：为什么不能用 `listen_fd` 直接调用 `recv()` 来读取客户端消息？

### 实践三：体验绑定地址的差异

1. 保持 `INADDR_LOOPBACK`，从本机连接，确认成功。
2. 查看本机局域网地址后，从另一台已授权设备尝试连接，确认它不能通过局域网地址访问。
3. 仅在确实需要局域网访问时改为 `INADDR_ANY`，配置防火墙后再测试。

只在自己的设备或获得明确授权的局域网内测试，不要扫描或连接未知设备与端口。

### 实践四：故意制造端口占用

启动一个服务器后，不关闭它，再启动第二个同端口服务器。观察第二个程序的 `bind()` 错误。然后停止第一个程序，重新启动第二个程序，理解“一个地址和端口通常只能由一个监听者占用”。

## 检查

- [ ] 我能画出 `socket -> bind -> listen -> accept -> recv/send -> close` 的服务器流程。
- [ ] 我能解释 `bind()` 中 IP 和端口分别决定什么。
- [ ] 我知道 `127.0.0.1` 只允许本机访问，`INADDR_ANY` 会监听所有 IPv4 网卡。
- [ ] 我能区分监听 Socket 与客户端连接 Socket，并知道何时关闭它们。
- [ ] 我能解释 `accept()` 为什么会阻塞，以及它为什么返回新 Socket。
- [ ] 我能正确处理 `recv() > 0`、`recv() == 0` 和 `recv() < 0`。
- [ ] 我不会假设一次 `send()` 或 `recv()` 就是一条完整消息。
- [ ] 我知道串行阻塞服务器一次只能主动处理一个客户端。
- [ ] 我能根据失败 API 和错误码开始排查端口、地址与防火墙问题。

## 关联

- [[Socket基础]]
- [[TCP客户端]]
- [[连接生命周期]]
- [[发送与接收数据]]
- [[消息边界与拆包]]
- [[超时、断线与异常]]
- [[TCP回显项目]]
- [[01-基础入门/IP地址与端口]]
- [[01-基础入门/网络观察工具]]
- [[04-并发与I-O模型/阻塞与非阻塞]]
- [[04-并发与I-O模型/线程与进程模型]]
- [[04-并发与I-O模型/I-O多路复用]]
