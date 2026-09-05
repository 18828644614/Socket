---
type: topic
status: complete
created: 2026-08-25
updated: 2026-09-01
tags:
  - UDP
  - 客户端
  - 服务器
  - Windows
  - Linux
---

# UDP客户端与服务器

## 学习目标

学完本章后，你应该能够：

- 写出 UDP 服务端和客户端的基本调用顺序。
- 解释 UDP 服务端为何没有 `listen()` 和 `accept()`。
- 使用 `sendto()` 指定目标地址，并用 `recvfrom()` 取得发送者地址。
- 以 Windows 为主编译并运行带超时的 UDP 回显程序。
- 在 Linux/WSL2 上运行等价程序，并说出主要平台差异。

## 1. 整体流程

UDP 不建立 TCP 那种持续连接。每次发送时，程序把一份数据报和目标地址交给系统；服务端接收时，同时得到数据内容和来源地址。

~~~text
客户端                                      服务端
socket()                                    socket()
  |                                           |
  |                                           bind(127.0.0.1:9000)
  |                                           |
sendto("hello", 127.0.0.1:9000)  ------>  recvfrom(..., 客户端地址)
  |                                           |
recvfrom(..., 服务端地址)          <------  sendto("hello", 客户端地址)
~~~

~~~text
UDP 服务端：socket() -> bind() -> 循环 recvfrom() / sendto() -> close()
UDP 客户端：socket() -> sendto() -> recvfrom() -> close()
~~~

客户端通常不需要手动 `bind()`。第一次 `sendto()` 时，系统会自动选择本机 IP 与临时端口，例如 `127.0.0.1:52143`。服务端通过 `recvfrom()` 得到该地址，便可以回复。

### 1.1 为什么没有 `listen()` 和 `accept()`

TCP 要建立连接，服务端需 `listen()` 等待连接、以 `accept()` 为每位客户端取得新的连接 Socket。UDP 没有这种连接状态；一个已绑定的 UDP Socket 能直接收到多位客户端的数据报：

~~~text
客户端 A 127.0.0.1:52143 --\
客户端 B 127.0.0.1:52144 ----> 服务端 Socket 127.0.0.1:9000
客户端 C 127.0.0.1:52145 --/
~~~

每次 `recvfrom()` 返回的来源 IP 和端口，决定当前应向谁回复。

## 2. 三条关键规则

### 2.1 UDP 保留数据报边界

~~~c
sendto(sock, "one", 3, 0, ...);
sendto(sock, "two", 3, 0, ...);
~~~

接收方会用两次 `recvfrom()` 分别得到 `one` 与 `two`。一次 `recvfrom()` 最多读取一份数据报，不会把两次发送拼成连续字节流。

缓冲区不足时，下一次 `recvfrom()` 不能读取同一份数据报的“剩余部分”：该数据报会被截断，剩余字节会丢失（或在某些平台报告过大消息错误）。因此文本示例传入 `BUFFER_SIZE - 1`，为结尾的 `\0` 留出位置。

### 2.2 `sendto()` 成功不等于对方已收到

`sendto()` 返回正数只说明本机操作系统接受了该数据报：

~~~text
sendto() 成功 -> 本机内核接受数据报 -> 网络仍可能丢失
                                      -> 对方程序还必须 recvfrom() 并处理
~~~

UDP 不保证到达、顺序或不重复。重要操作应在应用层设计请求 ID、响应、超时、必要时重传与去重。

### 2.3 `recvfrom() == 0` 不是断开

UDP 中 `recvfrom() == 0` 表示收到了**零长度数据报**，不是客户端关闭。UDP 没有 TCP 中 `recv() == 0` 的“对端有序关闭”语义。

## 3. Windows 优先：回显服务端

服务端绑定 `127.0.0.1:9000`，仅允许本机访问，适合第一次实验。保存为 `udp_server_win.c`：

~~~c
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

#pragma comment(lib, "Ws2_32.lib")

#define SERVER_PORT 9000
#define BUFFER_SIZE 1024

int main(void) {
    WSADATA wsa_data;
    SOCKET server_sock;
    struct sockaddr_in server_addr = {0};

    /* Windows 使用 Socket 前必须初始化 Winsock。 */
    int startup_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (startup_result != 0) {
        printf("WSAStartup failed: %d\n", startup_result);
        return 1;
    }
    server_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_sock == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);  /* 网络字节序 */
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server_sock, (const struct sockaddr *)&server_addr,
             sizeof(server_addr)) == SOCKET_ERROR) {
        printf("bind failed: %d\n", WSAGetLastError());
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    printf("UDP server is bound to 127.0.0.1:%d\n", SERVER_PORT);
    for (;;) {
        char buffer[BUFFER_SIZE];
        struct sockaddr_in client_addr;
        int client_addr_len = sizeof(client_addr);
        char client_ip[INET_ADDRSTRLEN];

        /* 无数据报时阻塞在此处，是正常等待。 */
        int received = recvfrom(server_sock, buffer, BUFFER_SIZE - 1, 0,
                                (struct sockaddr *)&client_addr,
                                &client_addr_len);
        if (received == SOCKET_ERROR) {
            printf("recvfrom failed: %d\n", WSAGetLastError());
            continue;
        }
        buffer[received] = '\0';  /* recvfrom() 不会自动追加字符串结尾 */

        if (inet_ntop(AF_INET, &client_addr.sin_addr,
                      client_ip, sizeof(client_ip)) == NULL) {
            printf("inet_ntop failed: %d\n", WSAGetLastError());
            continue;
        }
        printf("received %d bytes from %s:%u: %s\n", received, client_ip,
               (unsigned int)ntohs(client_addr.sin_port), buffer);

        /* 来源地址正是这次回显的目标地址。 */
        if (sendto(server_sock, buffer, received, 0,
                   (const struct sockaddr *)&client_addr,
                   client_addr_len) == SOCKET_ERROR) {
            printf("sendto failed: %d\n", WSAGetLastError());
        }
    }
}
~~~

### 3.1 `recvfrom()` 如何得到客户端地址

~~~c
int received = recvfrom(server_sock, buffer, BUFFER_SIZE - 1, 0,
                        (struct sockaddr *)&client_addr,
                        &client_addr_len);
~~~

可读作：“从 `server_sock` 取一份数据报，内容写入 `buffer`，发送者地址写入 `client_addr`。”最后两个参数是输出参数：

~~~text
调用前：client_addr_len = client_addr 可容纳的大小
调用后：client_addr     = 发送者的 IP 和端口
        client_addr_len = 实际写入的地址长度
~~~

`recvfrom()` 接收通用的 `struct sockaddr *`，而 IPv4 变量是 `struct sockaddr_in`，所以代码需要指针类型转换；转换不改变结构内容。`inet_ntop()` 把二进制 IP 转为文本，`ntohs()` 把网络字节序端口转为本机可读数字。`recvfrom()` 会改写地址长度，故 `client_addr_len = sizeof(client_addr)` 必须位于循环中、每轮重新设置。

## 4. Windows 优先：客户端与超时

保存为 `udp_client_win.c`。客户端不调用 `bind()`；系统会分配临时端口。三秒超时避免服务端未启动时永久等待。

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

    int startup_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (startup_result != 0) {
        printf("WSAStartup failed: %d\n", startup_result);
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
    buffer[strcspn(buffer, "\r\n")] = '\0'; /* 去掉 fgets() 保留的换行 */

    int sent = sendto(client_sock, buffer, (int)strlen(buffer), 0,
                      (const struct sockaddr *)&server_addr,
                      sizeof(server_addr));
    if (sent == SOCKET_ERROR) {
        printf("sendto failed: %d\n", WSAGetLastError());
        closesocket(client_sock);
        WSACleanup();
        return 1;
    }

    DWORD timeout_ms = 3000;  /* Windows 的超时单位是毫秒。 */
    if (setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO,
                   (const char *)&timeout_ms, sizeof(timeout_ms)) == SOCKET_ERROR) {
        printf("setsockopt failed: %d\n", WSAGetLastError());
        closesocket(client_sock);
        WSACleanup();
        return 1;
    }

    printf("sent %d bytes; waiting for echo...\n", sent);
    int received = recvfrom(client_sock, buffer, BUFFER_SIZE - 1, 0, NULL, NULL);
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
    closesocket(client_sock);
    WSACleanup();
    return 0;
}
~~~

首次 `sendto()` 后，客户端拥有系统分配的临时端口。服务端把 `recvfrom()` 得到的地址传给 `sendto()`，回复便能抵达客户端。`SO_RCVTIMEO` 仅表示本次等待时间已到，不能断定是服务端未启动、未回复、网络丢包还是防火墙拦截。生产程序还应验证回复来源地址与消息格式，防止接受伪造数据报。

## 5. Windows 编译与运行

~~~powershell
# MinGW-w64
gcc -Wall -Wextra -g udp_server_win.c -o udp_server_win.exe -lws2_32
gcc -Wall -Wextra -g udp_client_win.c -o udp_client_win.exe -lws2_32

# Visual Studio Developer PowerShell
cl /W4 /Zi udp_server_win.c ws2_32.lib
cl /W4 /Zi udp_client_win.c ws2_32.lib
~~~

先在窗口 A 运行 `.\udp_server_win.exe`，再在窗口 B 运行 `.\udp_client_win.exe`。输入 `hello udp` 后客户端应输出：

~~~text
Input a message: hello udp
sent 9 bytes; waiting for echo...
received 9 bytes: hello udp
~~~

## 6. Linux/WSL2 等价示例

Linux/WSL2 的网络流程相同，主要差异是：不需要 `WSAStartup()`，Socket 是 `int`，用 `close()` 关闭，并使用 `errno`/`perror()` 报错。

### 6.1 Linux 服务端：`udp_server_linux.c`

~~~c
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_PORT 9000
#define BUFFER_SIZE 1024

int main(void) {
    int server_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_fd < 0) { perror("socket"); return 1; }
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); close(server_fd); return 1;
    }
    for (;;) {
        char buffer[BUFFER_SIZE];
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        ssize_t received = recvfrom(server_fd, buffer, BUFFER_SIZE - 1, 0,
                                    (struct sockaddr *)&client_addr, &client_addr_len);
        if (received < 0) { perror("recvfrom"); continue; }
        buffer[received] = '\0';
        printf("received %zd bytes: %s\n", received, buffer);
        if (sendto(server_fd, buffer, (size_t)received, 0,
                   (struct sockaddr *)&client_addr, client_addr_len) < 0) {
            perror("sendto");
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
    if (client_fd < 0) { perror("socket"); return 1; }
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    char buffer[BUFFER_SIZE];
    printf("Input a message: ");
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) { close(client_fd); return 1; }
    buffer[strcspn(buffer, "\r\n")] = '\0';
    if (sendto(client_fd, buffer, strlen(buffer), 0,
               (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("sendto"); close(client_fd); return 1;
    }
    struct timeval timeout = {3, 0}; /* 秒 + 微秒 */
    if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt"); close(client_fd); return 1;
    }
    ssize_t received = recvfrom(client_fd, buffer, BUFFER_SIZE - 1, 0, NULL, NULL);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("Timed out: no reply within 3 seconds.\n");
        } else {
            perror("recvfrom");
        }
        close(client_fd); return 1;
    }
    buffer[received] = '\0';
    printf("received %zd bytes: %s\n", received, buffer);
    close(client_fd);
    return 0;
}
~~~

编译并分别在两个终端运行：

~~~bash
gcc -Wall -Wextra -g udp_server_linux.c -o udp_server_linux
gcc -Wall -Wextra -g udp_client_linux.c -o udp_client_linux
./udp_server_linux
./udp_client_linux
~~~

WSL2 使用 Linux 头文件和命令；不要用 Windows 原生编译器编译包含 `sys/socket.h`、`unistd.h` 的代码。

## 7. 平台差异与排错

| 项目 | Windows 原生 Winsock | Linux/WSL2 |
| --- | --- | --- |
| 使用前初始化 | `WSAStartup()` | 不需要 |
| Socket 类型 | `SOCKET` | `int` |
| 创建失败 | `INVALID_SOCKET` | 小于 0 |
| 常规调用失败 | `SOCKET_ERROR` | 通常小于 0 |
| 错误信息 | `WSAGetLastError()` | `errno`、`perror()` |
| 关闭 Socket | `closesocket()` | `close()` |
| 接收超时 | `DWORD` 毫秒数 | `struct timeval` 秒与微秒 |
| 链接 | `-lws2_32` 或 `ws2_32.lib` | 通常无需额外库 |
| 地址长度 | `int` | `socklen_t` |

~~~powershell
# Windows PowerShell：查看 UDP 端口
Get-NetUDPEndpoint -LocalPort 9000
netstat -ano -p udp
~~~

~~~bash
# Linux/WSL2：查看 UDP 端口
ss -u -a -n | grep 9000
~~~

| 现象 | 常见原因与检查方向 |
| --- | --- |
| `bind failed` | 端口已被占用；关闭旧服务端或更换端口，客户端也要同步修改。 |
| 客户端超时 | 服务端未启动、IP/端口不同、服务端未回包，或跨机器时被防火墙拦截。 |
| 服务端看似卡住 | 它正在阻塞等待 `recvfrom()`，属于正常状态。 |
| 文本不完整 | 数据报超过接收缓冲区，下一次调用不能补读剩余部分。 |
| 端口数字异常 | 打印时漏掉了 `ntohs()`。 |
| Windows 链接失败 | 为 MinGW 添加 `-lws2_32`，或给 Visual Studio 添加 `ws2_32.lib`。 |

将 `INADDR_LOOPBACK` 改为 `INADDR_ANY` 会监听所有 IPv4 网卡，允许局域网设备访问。只应在授权网络中测试，并为 UDP 端口配置防火墙；未经认证的练习服务不应暴露在不受信任网络中。

## 实践

1. 先运行 Windows 服务端，再连续运行客户端三次，观察服务端打印的临时端口是否变化。
2. 暂时注释服务端的回显 `sendto()`，确认客户端三秒后超时，并说明超时不能证明什么。
3. 输入超过 1023 字节的文本，解释为什么示例不能完整接收它。
4. 在 Windows 与 Linux/WSL2 各运行一次，比较初始化、关闭、错误输出和超时设置。

## 检查

- [ ] 我能写出 UDP 服务端和客户端的调用顺序。
- [ ] 我知道 UDP 服务端不需要 `listen()` 和 `accept()`。
- [ ] 我能解释 `recvfrom()` 如何返回客户端 IP 和端口。
- [ ] 我知道客户端未显式 `bind()` 时会获得临时端口。
- [ ] 我知道 UDP 保留数据报边界，但不保证到达、顺序或不重复。
- [ ] 我不会把 `sendto()` 成功当作对端业务处理成功。
- [ ] 我知道 UDP 中 `recvfrom() == 0` 是零长度数据报。
- [ ] 我能说出 Windows 与 Linux/WSL2 的主要 Socket 差异。

## 关联

- [[UDP基础]]
- [[数据报与地址]]
- [[UDP回显项目]]
- [[UDP可靠性设计]]
- [[01-基础入门/IP地址与端口]]
- [[01-基础入门/Socket编程环境]]
- [[01-基础入门/网络观察工具]]
