---
type: topic
status: active
created: 2026-08-25
updated: 2026-10-14
tags:
  - I-O
  - 并发
  - 新手入门
---

# I/O 多路复用

> 本章从完全新手视角出发。遇到“就绪”“描述符”等词时会先解释再使用。建议先读学习地图，再按阶段运行实验。
>
> 本章要回答的问题是：**一个线程怎样同时照看多个网络连接，而不必为每个客户端单独创建线程？**

## 0. 学习地图：范围和顺序

### 0.1 学完以后应该能做到

- 解释 I/O 多路复用解决什么问题。
- 区分“Socket 就绪”和“数据已经处理完毕”。
- 理解 select 的工作流程，并在 Windows 和 Linux 分别编译、运行多客户端示例。
- 对比 select、poll、Linux epoll，并知道 Windows IOCP 的定位。
- 知道多路复用不负责消息边界、部分发送、业务逻辑和连接状态管理。

### 0.2 推荐顺序

| 阶段 | 范围 | 建议时间 | 完成标志 |
| --- | --- | --- | --- |
| A. 补齐地基 | TCP、Socket、客户端/服务端、阻塞/非阻塞 | 30～60 分钟 | 能画出一次连接和收发顺序 |
| B. 看见问题 | 单线程顺序服务多个客户端的限制 | 20 分钟 | 能解释慢客户端为何拖住其他人 |
| C. 核心概念 | I/O、多路复用、就绪、读写集合 | 30～45 分钟 | 能解释“先等，再处理” |
| D. 学 select | 调用流程、集合、超时、平台差异 | 45～60 分钟 | 能读懂服务端循环 |
| E. 比较方案 | poll、epoll、IOCP、触发模式概览 | 30～45 分钟 | 能说出常见平台选择 |
| F. 动手实验 | 跨平台编译、多个客户端、观察日志 | 60～90 分钟 | 两个客户端都能被接受和读取 |
| G. 巩固检查 | 错误、练习、清单 | 45～60 分钟 | 能判断就绪不等于完整收发 |

必须掌握：就绪的含义、select 基本循环、监听 Socket 和客户端 Socket 都要监视、集合每轮重新准备、返回值检查、多路复用的边界。

先理解即可：poll/epoll/IOCP 的应用场景、水平触发与边缘触发。Linux epoll 的细节见 [[08-C与Linux深入/epoll深入]]。

### 0.3 前置知识

按顺序回顾，不熟悉时先打开对应章节：

1. [[IP地址与端口]]：地址和端口怎样定位服务。
2. [[TCP与UDP概念]]：本章用 TCP；TCP 传输有序字节流，但不提供消息边界。
3. [[Socket基础]]：Socket 是程序使用网络功能的通信端点。
4. [[阻塞与非阻塞]]：一次调用可能等待，非阻塞调用也可能暂时取不到数据。
5. [[线程与进程模型]]：理解为什么希望一条线程处理多个连接。

### 0.4 术语小抄

| 术语 | 通俗解释 |
| --- | --- |
| I/O | 输入/输出。网络程序中，从 Socket 读、向 Socket 写都属于 I/O。 |
| 多路复用 | 一次等待多个通信对象；哪个有动静，就先处理哪个。 |
| 文件描述符（fd） | Linux 用小整数代表打开的文件或 Socket。Windows Winsock 使用 SOCKET 类型。 |
| 就绪 | 现在尝试某种操作，通常能取得进展或得到结束/错误结果，不必一直等在那里。 |
| 监听 Socket | 服务端等待新连接的 Socket。 |
| 已连接 Socket | 与某个客户端连接后用于收发数据的 Socket。 |
| 内核 | 操作系统中负责管理网络、内存和设备等工作的核心部分。 |

## 1. 为什么需要多路复用

### 1.1 一个连接的服务端

最简单的服务端可能这样工作：

~~~text
等待一个客户端连接
和这个客户端收发数据
这个客户端结束后，再等下一个客户端
~~~

如果第一个客户端连上后一直不发数据，服务端的 recv() 可能一直等着。服务端无法回到 accept() 接受第二个客户端，第二个客户端便可能没有响应。

### 1.2 常见的三种办法

| 办法 | 做法 | 代价或限制 |
| --- | --- | --- |
| 每个连接一个线程/进程 | 每个客户端交给独立执行路线 | 容易理解，但连接多时会增加内存和调度开销 |
| 非阻塞轮询 | 反复问每个 Socket “现在有数据吗？” | 检查太频繁会浪费 CPU，连接越多要检查的对象越多 |
| I/O 多路复用 | 一次等待多个 Socket；有就绪对象时再处理 | 等待集中，但程序仍要管理各连接和读写状态 |

它们可以组合使用。多路复用不是“没有线程”，而是让一条线程可以等待许多 Socket 的 I/O 状态。

### 1.3 一个类比

想象前台照看许多取件柜：

- 不停逐个检查：像忙轮询。
- 每个柜子配一个专人：像每连接一个线程。
- 柜子系统在有包裹时通知前台：像 I/O 多路复用。

真实程序还需处理字节、错误、缓冲区和连接状态，类比只用来建立直觉。

## 2. 核心概念：等待就绪，再由程序读写

### 2.1 网络 I/O 是什么

常见操作包括：

- accept()：从监听 Socket 取得一个新连接。
- recv()：从已连接 Socket 读取数据。
- send()：向已连接 Socket 提交数据。

这些操作要和操作系统网络栈协作。数据可能还没到，发送缓冲区也可能暂时没有空间。

### 2.2 就绪不等于完成

select、poll、epoll 通常属于**就绪通知**：告诉程序“某个 Socket 现在值得尝试读或写”。

例如被报告可读，接下来调用 recv() 可能：

1. 返回一个或多个字节；
2. 返回 0，表示 TCP 对端正常结束发送；
3. 返回错误；
4. 对非阻塞 Socket 来说，状态可能变化，仍返回“暂时无数据”。

所以“可读”不等于“肯定有正数字节”，也不等于“完整消息已到达”。

被报告可写也不表示全部数据都能一次发出去。send() 可能只接受一部分，程序需要保存剩余数据。参见 [[阻塞与非阻塞]]、[[状态机与连接管理]]。

### 2.3 就绪通知和完成通知

- **就绪通知**：现在可以尝试读/写。POSIX select/poll/epoll 和 Windows Winsock select 都属于此类。
- **完成通知**：之前提交的操作已经完成，再来取结果。Windows **IOCP** 属于此类。

不要把 Windows 的 select 和 IOCP 混为一谈。它们都可用于构建并发网络服务，但编程方式不同。IOCP 是进阶内容，本章只建立概念。

### 2.4 服务端同时观察什么

服务端通常需要观察：

1. **监听 Socket 可读**：通常表示有待接受的新连接，应调用 accept()。
2. **客户端 Socket 可读**：可能有数据、对端结束发送或发生错误，应调用 recv() 并检查结果。
3. **客户端 Socket 可写**：只在程序有待发送数据时通常才需要关注。已连接 Socket 往往一直可写，持续监视可能让循环立即返回并占用 CPU。

下面示例只观察“新连接”和“客户端发来的数据”，收到后打印，不做回显。

### 阶段 A/B 检查

- [ ] 能说明单线程顺序服务为何会被沉默客户端拖住。
- [ ] 能说出线程、轮询、多路复用方案的主要代价。
- [ ] 能解释“就绪后仍然要调用 recv()”。
- [ ] 知道 send() 可能只发送一部分。
- [ ] 能区别就绪通知和完成通知。

## 3. select：入门的多路复用接口

### 3.1 工作流程

~~~text
1. 准备一组想观察的 Socket
2. 调用 select 等待状态变化
3. 检查哪些 Socket 就绪
4. 对就绪对象调用 accept/recv/send
5. 回到第 1 步，重新准备集合
~~~

select() 会改写传入的集合，使其只留下就绪对象。因此程序通常保存“所有客户端 Socket”的主列表，每轮从主列表重新构造临时集合。

### 3.2 三类集合

- **读集合**：关注可读状态。监听 Socket 上可读常代表有新连接；客户端 Socket 上可读可能代表数据、EOF 或错误。
- **写集合**：关注可写状态。通常用于有待发送数据，或判断非阻塞连接进度。
- **异常集合**：按平台文档定义关注特定异常；它不是万能错误集合。

初学的服务端通常先只使用读集合。

### 3.3 返回值和超时

等待时间表示“最多等多久”：

- 返回正数：一个或多个 Socket 就绪。
- 返回 0：等待超时，没有观察到关注的状态。
- 返回错误：读取平台错误码；Linux 看 errno，Windows 调用 WSAGetLastError()。

示例使用约一秒的超时来打印存活日志。超时本身不代表客户端断线，也不是 TCP 断线检测。

### 3.4 Linux 与 Windows 差异

| 事项 | Linux/POSIX | Windows/Winsock |
| --- | --- | --- |
| Socket 类型 | int 文件描述符 | SOCKET |
| select 第一个参数 | 最大 fd 加 1 | 忽略，通常传 0 |
| 集合容量 | 受 FD_SETSIZE 等限制 | 默认 FD_SETSIZE 常为 64；可在包含 Winsock 头文件前定义 |
| 错误 | errno | WSAGetLastError() |
| 关闭 | close(fd) | closesocket(s) |
| 初始化 | 通常不需网络库初始化 | 先 WSAStartup()，结束时 WSACleanup() |

不能把 Linux 的 select(max_fd + 1) 原样照搬到 Winsock。

### 3.5 select 的边界

- 监听数量有限，受平台和集合实现限制。
- 每次调用前要重新准备集合。
- 返回后通常要检查关注列表中的对象，数量变大时扫描成本也增加。
- 它不会替你拼装 TCP 消息、处理部分写入或管理业务状态。

这些限制不表示 select 没用。它适合学习、连接数不大的工具和兼容性需求。是否够用应根据负载测量，而不是只看接口名字。

## 4. Windows 实验：Winsock select 多客户端服务端

此程序监听 127.0.0.1:19090，接受多个客户端并打印收到的字节。客户端关闭后从列表移除。为保护 Winsock 默认集合容量，最多保留 60 个客户端。示例用于教学，不是生产级服务器：没有实现消息协议、回显写队列、认证和完整日志系统。

### 4.1 保存代码

保存为 select_server_win.c：

~~~c
#define FD_SETSIZE 64
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

#define PORT 19090
#define MAX_CLIENTS 60
#define BUFFER_SIZE 1024

static void remove_client(SOCKET clients[], int *count, int index) {
    closesocket(clients[index]);
    clients[index] = clients[*count - 1];
    (*count)--;
}

int main(void) {
    WSADATA wsa;
    SOCKET listener;
    SOCKET clients[MAX_CLIENTS];
    int client_count = 0;
    struct sockaddr_in address = {0};
    int startup_status;

    startup_status = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (startup_status != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", startup_status);
        return 1;
    }

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (address.sin_addr.s_addr == INADDR_NONE) {
        fprintf(stderr, "inet_addr failed\n");
        closesocket(listener);
        WSACleanup();
        return 1;
    }

    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
        closesocket(listener);
        WSACleanup();
        return 1;
    }

    if (listen(listener, SOMAXCONN) == SOCKET_ERROR) {
        fprintf(stderr, "listen failed: %d\n", WSAGetLastError());
        closesocket(listener);
        WSACleanup();
        return 1;
    }

    printf("Listening on 127.0.0.1:%d\n", PORT);

    for (;;) {
        fd_set read_set;
        struct timeval timeout;
        int ready;

        FD_ZERO(&read_set);
        FD_SET(listener, &read_set);
        for (int i = 0; i < client_count; i++) {
            FD_SET(clients[i], &read_set);
        }

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        ready = select(0, &read_set, NULL, NULL, &timeout);

        if (ready == SOCKET_ERROR) {
            fprintf(stderr, "select failed: %d\n", WSAGetLastError());
            break;
        }
        if (ready == 0) {
            puts("select timeout: still waiting");
            continue;
        }

        if (FD_ISSET(listener, &read_set)) {
            SOCKET client = accept(listener, NULL, NULL);
            if (client == INVALID_SOCKET) {
                fprintf(stderr, "accept failed: %d\n", WSAGetLastError());
            } else if (client_count >= MAX_CLIENTS) {
                puts("client limit reached; rejecting connection");
                closesocket(client);
            } else {
                clients[client_count++] = client;
                printf("client accepted; clients=%d\n", client_count);
            }
        }

        for (int i = 0; i < client_count;) {
            if (FD_ISSET(clients[i], &read_set)) {
                char buffer[BUFFER_SIZE];
                int n = recv(clients[i], buffer, sizeof(buffer), 0);
                if (n > 0) {
                    printf("received %d bytes: ", n);
                    fwrite(buffer, 1, (size_t)n, stdout);
                    putchar('\n');
                    i++;
                } else if (n == 0) {
                    puts("client finished sending and disconnected");
                    remove_client(clients, &client_count, i);
                } else {
                    fprintf(stderr, "recv failed: %d\n", WSAGetLastError());
                    remove_client(clients, &client_count, i);
                }
            } else {
                i++;
            }
        }
    }

    for (int i = 0; i < client_count; i++) {
        closesocket(clients[i]);
    }
    closesocket(listener);
    WSACleanup();
    return 0;
}
~~~

### 4.2 编译和启动

需要 MinGW-w64 GCC 或 Visual Studio 开发者命令行，二选一：

- MinGW-w64：gcc -std=c11 -Wall -Wextra select_server_win.c -o select_server_win.exe -lws2_32
- Visual Studio：cl /W4 select_server_win.c ws2_32.lib

然后运行 .\select_server_win.exe。看到 Listening on 127.0.0.1:19090 表示监听成功。

### 4.3 连接客户端

可安装 Nmap 并使用它附带的 Ncat。在另一个 PowerShell 窗口执行 ncat 127.0.0.1 19090。输入文字并按 Enter，服务端会打印收到的字节。再开一个窗口连第二个客户端并交错发送，观察两个连接都可被处理。按 Ctrl+C 关闭 Ncat，服务端会观察连接结束。

没有 Ncat 时，可用 Test-NetConnection 127.0.0.1 -Port 19090 检查端口是否可连；它不提供交互式文本发送。

## 5. Linux 实验：POSIX select 多客户端服务端

逻辑与 Windows 版相同，但 Socket 类型、关闭函数、错误码和 select 参数不同。本程序只监听本机回环地址。

### 5.1 保存代码

保存为 select_server_linux.c：

~~~c
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 19090
#define MAX_CLIENTS 128
#define BUFFER_SIZE 1024

static void remove_client(int clients[], int *count, int index) {
    close(clients[index]);
    clients[index] = clients[*count - 1];
    (*count)--;
}

int main(void) {
    int listener;
    int clients[MAX_CLIENTS];
    int client_count = 0;
    struct sockaddr_in address;

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");
        return 1;
    }
    if (listener >= FD_SETSIZE) {
        fprintf(stderr, "listener fd exceeds FD_SETSIZE; select cannot monitor it\n");
        close(listener);
        return 1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        fprintf(stderr, "inet_pton failed\n");
        close(listener);
        return 1;
    }

    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(listener);
        return 1;
    }
    if (listen(listener, SOMAXCONN) < 0) {
        perror("listen");
        close(listener);
        return 1;
    }

    printf("Listening on 127.0.0.1:%d\n", PORT);

    for (;;) {
        fd_set read_set;
        struct timeval timeout;
        int max_fd = listener;
        int ready;

        FD_ZERO(&read_set);
        FD_SET(listener, &read_set);
        for (int i = 0; i < client_count; i++) {
            FD_SET(clients[i], &read_set);
            if (clients[i] > max_fd) {
                max_fd = clients[i];
            }
        }

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        ready = select(max_fd + 1, &read_set, NULL, NULL, &timeout);

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }
        if (ready == 0) {
            puts("select timeout: still waiting");
            continue;
        }

        if (FD_ISSET(listener, &read_set)) {
            int client = accept(listener, NULL, NULL);
            if (client < 0) {
                perror("accept");
            } else if (client >= FD_SETSIZE || client_count >= MAX_CLIENTS) {
                puts("client limit reached; rejecting connection");
                close(client);
            } else {
                clients[client_count++] = client;
                printf("client accepted; clients=%d\n", client_count);
            }
        }

        for (int i = 0; i < client_count;) {
            if (FD_ISSET(clients[i], &read_set)) {
                char buffer[BUFFER_SIZE];
                ssize_t n = recv(clients[i], buffer, sizeof(buffer), 0);
                if (n > 0) {
                    printf("received %zd bytes: ", n);
                    fwrite(buffer, 1, (size_t)n, stdout);
                    putchar('\n');
                    i++;
                } else if (n == 0) {
                    puts("client finished sending and disconnected");
                    remove_client(clients, &client_count, i);
                } else if (errno == EINTR) {
                    continue;
                } else {
                    perror("recv");
                    remove_client(clients, &client_count, i);
                }
            } else {
                i++;
            }
        }
    }

    for (int i = 0; i < client_count; i++) {
        close(clients[i]);
    }
    close(listener);
    return 0;
}
~~~

### 5.2 编译、启动、连接

在 Linux 终端进入文件所在目录：

- 编译：gcc -std=c11 -Wall -Wextra -O2 select_server_linux.c -o select_server_linux
- 启动：./select_server_linux
- 另开终端连接：nc 127.0.0.1 19090

没有 nc 时，Debian/Ubuntu/WSL 可安装 netcat-openbsd 包；其他发行版包名可能不同。也可用已安装的 telnet 127.0.0.1 19090。

连接后输入文字，再开第二个终端连接。关闭客户端时，服务端会将其移出监视列表。

### 5.3 WSL 提醒

可以在 WSL 编译运行 Linux 版本，也可以在 Windows 原生终端运行 Windows 版本。第一轮请让客户端和服务端在同一环境中，减少网络配置变量。不要用 Windows 编译器编译 Linux 文件描述符代码，反之亦然。

## 6. 逐步读懂服务端循环

1. **准备集合**：FD_ZERO() 清空集合，加入监听 Socket 和已连接客户端。因为 select() 会修改集合，每轮都要重新构造。
2. **等待**：有连接或数据时提前返回；超时只表示这段时间没有观察到关注状态。
3. **接受新连接**：监听 Socket 被报告可读时调用 accept()，把新 Socket 放入客户端列表。
4. **读取客户端**：就绪后调用 recv()。正数是收到字节，零表示对端结束发送，负数是错误并应检查原因。
5. **释放资源**：关闭监听和剩余客户端 Socket。Windows 还要调用 WSACleanup()。

代码通过“用最后一个元素填补被删除的位置”来移除客户端，避免搬动整个数组；所以数组顺序不稳定，但实验不依赖顺序。

### 阶段 C/D 检查

- [ ] 能按顺序讲出 select 的五步流程。
- [ ] 知道每轮调用前要重建集合。
- [ ] 知道监听 Socket 可读后通常调用 accept()。
- [ ] 知道 recv() 的正数、零、错误代表不同结果。
- [ ] 能说出 Windows 和 Linux 的 Socket 类型、关闭函数及 select 首参数差异。
- [ ] 知道 poll、epoll、IOCP 的大致平台和用途。
- [ ] 知道边缘触发通常配合非阻塞 Socket 并读到 EAGAIN；初学优先水平触发。

## 7. poll、epoll、IOCP：整体地图

| 接口/模型 | 平台 | 通知类型 | 特点 |
| --- | --- | --- | --- |
| select | Linux/Unix、Winsock | 就绪通知 | 入门直观；集合容量有限，每轮重建并检查 |
| poll | Linux/Unix | 就绪通知 | 用数组描述 Socket；等待后通常仍需检查数组 |
| epoll | Linux | 就绪通知 | 注册关注项，再等待就绪项；适合管理较多连接，应用仍需维护状态 |
| IOCP | Windows | 完成通知 | 异步操作完成后取得结果，常配合重叠 I/O 和完成队列 |

### 7.1 poll 与 select

select 用集合表示关注对象，poll 用数组元素描述 Socket 和事件。poll 避开了 select 同一种固定集合表达方式，但不代表连接数无限或自动高性能。

### 7.2 epoll 的直观流程

Linux epoll 大致为：创建实例、注册关注的 Socket、等待一批就绪事件、处理事件并更新注册项。它避免每次等待都重新提交整份关注列表，但返回事件处理、连接管理、缓冲和错误处理仍由应用承担。不要简单背成“任何情况下都是 O(1)”；实际成本与负载、就绪事件数量和实现相关。详见 [[08-C与Linux深入/epoll深入]]。

### 7.3 水平触发和边缘触发先认名字

- **水平触发（LT）**：只要条件仍满足，通常还会继续提醒，比较容易上手。
- **边缘触发（ET）**：状态从不满足变为满足时提醒；通常要用非阻塞 Socket，并持续读取直到暂时无数据（EAGAIN/EWOULDBLOCK），否则可能漏掉后续处理机会。

初学先学 LT。ET 的细节留到 epoll 进阶章节。本章 select 示例不涉及这两种 epoll 模式。

## 8. 实验步骤和记录

### 实验 1：一个客户端

启动服务端和客户端，输入 hello 并回车，观察服务端显示字节数。关闭客户端，观察服务端移除连接。

### 实验 2：两个客户端交错发送

同时开两个客户端，分别发送 from A 和 from B。更换发送顺序再试。谁有数据，谁就可能先被处理，不必按连接先后轮流处理。

### 实验 3：一个沉默，另一个发送

客户端 A 连上后不发数据；客户端 B 再连上并发送文字。观察服务端仍能读取 B。这说明服务端线程在 select() 等整组 Socket，而不是卡在 A 的 recv() 上。

### 实验 4：超时与 TCP 字节流

所有客户端都沉默时，服务端每秒打印一次超时。把 timeout.tv_sec 改为 3 并重编译，观察日志频率。然后输入较长文本，观察一次 recv() 得到的长度并不固定。TCP 是字节流，发送边界和接收边界没有一一对应关系。

实验记录建议：

~~~text
操作系统和编译器：
客户端数量：
运行前预测：
服务端实际输出：
沉默客户端是否影响其他客户端：
差异原因：
~~~

## 9. 常见错误及处理

| 现象 | 常见原因 | 怎么检查 |
| --- | --- | --- |
| bind 失败 | 端口已被占用或地址错误 | 关闭旧服务端，或在代码中同时修改端口 |
| Windows 找不到 winsock2.h | 编译器环境没有 Windows SDK/Winsock 头文件 | 使用 MinGW-w64 或 Visual Studio 开发者命令行 |
| Windows 链接 Winsock 函数失败 | 忘了链接 Winsock 库 | GCC 命令加 -lws2_32；MSVC 加 ws2_32.lib |
| Linux 找不到 sys/select.h | 在 Windows 原生环境编译 Linux 代码 | 转到 Linux/WSL 编译对应版本 |
| 第二个客户端没被处理 | 客户端没有真正连接，或运行的不是本章代码 | 看服务端是否输出 client accepted |
| 连上但输入后没日志 | 客户端未发送数据或连到不同环境 | 按回车，核对地址、端口和运行环境 |
| 服务端不断输出 timeout | 一秒超时是示例预期行为 | 调大等待时间或删去调试输出 |
| 一条文本分多次显示 | TCP 不保留应用消息边界 | 设计分隔符/长度头并累积数据 |
| 到客户端上限后被拒绝 | 示例设置了明确容量保护 | 真实程序需设计容量策略 |
| 监视写集合后 CPU 很忙 | Socket 通常一直可写，等待立刻返回 | 只在有待发送数据时关注写就绪 |
| select 后集合内容不对 | 忘了每轮重建，或集合容量有误 | 从主列表重建并检查 FD_SETSIZE |
| Linux 出现 Interrupted system call | 信号中断等待，errno == EINTR | 示例重新循环；复杂程序应重新计算剩余超时 |
| recv() 负数就认定断线 | 负数代表调用出错，原因不只断线 | 检查错误码；非阻塞模式还要处理 EAGAIN |

排查顺序：确认操作系统与示例匹配，确认编译成功，确认服务端监听，再确认客户端连接，最后看具体失败调用和错误码。

## 10. 练习题

### 10.1 概念题

1. select 报告 Socket 可读，是否保证 recv() 返回正数？还有哪些结果？
2. 为什么把客户端 Socket 放入写集合可能导致服务端几乎不等待？
3. select() 超时能说明客户端断线吗？
4. send() 成功能证明对方应用已经处理数据吗？
5. Windows IOCP 是不是 Windows 版 epoll？说明通知方式差异。
6. 为什么要保存客户端主列表，并每轮重新构造 select 临时集合？

### 10.2 动手题

7. 完成一个客户端、两个客户端、一个沉默而另一个发送三组实验，记录日志。
8. 把等待从 1 秒改成 3 秒，先预测日志变化，再编译验证。
9. 将缓冲区改为 4 字节，发送较长内容，观察接收次数为何不固定。
10. 在 Linux 输出每个客户端的 fd 编号，观察 fd 是否连续，并解释为什么不能把数组下标当 fd。
11. 选做：实现回显。为每个客户端保存待发送字节和偏移，只在该客户端有待发数据时加入写集合；正确处理部分发送。
12. 选做：用 poll 重写 Linux 等待部分，再阅读 [[08-C与Linux深入/epoll深入]]。

## 11. 参考答案与阶段检查

### 11.1 概念题参考

1. 不保证。可读可能代表有数据、EOF 或错误；非阻塞 Socket 的状态也可能在通知后改变。
2. 写就绪通常很容易满足。没有待发送数据也一直关注它，会导致等待很快返回、循环反复运行。
3. 不能。超时只说明这段时间没有观察到关注状态。
4. 不能。它只说明本机接受了部分或全部字节；业务处理完成需要协议层确认。
5. 不是。IOCP 报告异步操作完成；epoll 报告 Socket 当前就绪，可以继续尝试 I/O。
6. select() 会改写集合；没有主列表就无法恢复下一轮原本要监视的对象。

### 11.2 最终检查清单

**理解**

- [ ] 我能用日常语言解释 I/O 多路复用。
- [ ] 我能解释就绪不等于操作完成或完整消息到达。
- [ ] 我能说出多路复用解决什么问题，以及不负责什么。
- [ ] 我能区别就绪通知和完成通知。

**代码**

- [ ] 我知道监听 Socket 可读后通常应调用 accept()。
- [ ] 我知道客户端可读后仍需检查 recv() 的返回值。
- [ ] 我知道 select() 会改写集合，每轮要重新准备。
- [ ] 我知道只在有待发数据时关注写就绪。
- [ ] 我知道连接列表、消息拼装、部分写入和资源清理仍由程序负责。

**平台与实验**

- [ ] 我能选择正确的 Windows 或 Linux 版本并运行。
- [ ] 我能让两个客户端同时连接并交错发送。
- [ ] 我能解释沉默客户端为何没有阻止另一个被处理。
- [ ] 我知道 Windows Winsock 与 Linux select 的参数、类型差异。
- [ ] 我能说出 select、poll、epoll、IOCP 的大致定位。

验收建议：概念题 1～6 能正确解释，并完成实验 1～3，再继续 [[事件循环]]。回显扩展属于进阶练习，不必在本章做成生产级服务器。

## 12. 下一步

按本阶段路线继续：

1. [[事件循环]]：把等待、分发和处理组织成持续循环。
2. [[状态机与连接管理]]：记录每个连接的状态、接收缓冲区和待发送数据。
3. [[并发聊天室项目]]：组合多个客户端、消息边界、广播与退出处理。
4. Linux 进阶时阅读 [[08-C与Linux深入/epoll深入]]，继续学习触发模式与非阻塞读写。

## 13. 参考资料

遇到参数和平台细节时再查手册，不必从头通读：

- Linux select(2)：https://man7.org/linux/man-pages/man2/select.2.html
- Linux poll(2)：https://man7.org/linux/man-pages/man2/poll.2.html
- Linux epoll(7)：https://man7.org/linux/man-pages/man7/epoll.7.html
- Microsoft Winsock select：https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-select
- Microsoft I/O Completion Ports：https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports

---

> 本章主线：**把多个 Socket 交给系统一起等待；系统报告哪个现在有动静；程序再接受连接、读取数据或处理结束状态。等待可以集中组织，但具体连接和数据仍由应用程序负责。**
