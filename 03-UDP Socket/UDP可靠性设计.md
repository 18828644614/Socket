---
type: topic
status: complete
created: 2026-08-25
updated: 2026-09-01
tags:
  - UDP
  - 可靠性
  - Windows
  - Linux
---

# UDP可靠性设计

## 学习目标

- 理解 UDP 不保证到达、顺序和不重复的原因。
- 掌握序号、ACK、超时、重传和去重的作用。
- 能在 Windows（优先）和 Linux/WSL2 上运行一个停等协议示例。
- 知道教学版可靠 UDP 与生产级协议的差别。

## 1. UDP 为什么不可靠

UDP 的 sendto() 返回成功，只表示本机内核接收了数据报，不表示对方程序已收到或处理。

~~~text
sendto() 成功 -> 本机内核接受
                 -> 网络可能丢弃
                 -> 对方可能没有进程接收
                 -> 数据可能晚到、重复或乱序
~~~

常见问题与补救方法：

| 问题 | 应用层补充 |
| --- | --- |
| 丢失 | ACK（确认）和超时重传 |
| 重复 | 序号和去重 |
| 乱序 | 序号、缓存或丢弃 |
| 格式错误 | 长度、校验和、字段检查 |

UDP 保留数据报边界，一次 recvfrom() 对应一份数据报；缓冲区太小会截断，不能下一次再读取剩余内容。

## 2. 最小方案：停等协议

一次只发送一条消息，收到匹配 ACK 后才发送下一条。

~~~text
客户端                                      服务端
  |--- DATA seq=0, "hello" ----------------->|
  |<-------------- ACK seq=0 ----------------|
  |--- DATA seq=1, "world" ----------------->|
  |<-------------- ACK seq=1 ----------------|
~~~

本章使用易调试的文本格式：

~~~text
DATA <序号> <内容>
ACK <序号>
~~~

四个关键字段：

1. 序号：标识消息，例如 0、1、2。
2. ACK 序号：服务端确认已经处理的消息。
3. 超时：等待 ACK 的最长时间，例如 1000 毫秒。
4. 最大重试次数：避免服务端失效时无限发包，例如 3 次。

### 2.1 ACK 丢失为什么会导致重复

~~~text
客户端                         服务端
  |--- DATA 0 ----------------->|  已处理一次
  |<--- ACK 0 ----X（丢失）------|
  |--- DATA 0 ----------------->|  再次收到同一序号
~~~

客户端无法区分“数据没到”和“ACK 丢了”，所以必须重传。服务端要识别重复包：重复包不再次执行扣款、写文件等业务，但要重新发送 ACK。可靠 UDP 不是让网络绝不重复，而是让重复可识别、可安全处理。

服务端可按下表处理：

| 序号 | 动作 |
| --- | --- |
| seq == expected_seq | 第一次收到，处理、ACK，然后 expected_seq 加一 |
| seq < expected_seq | 旧重复包，不处理，重新 ACK |
| seq > expected_seq | 前序包尚未确认；教学版丢弃并等待重传 |

### 2.2 超时不等于“服务器断开”

超时可能由数据丢失、ACK 丢失、服务端处理慢、端口写错或防火墙造成。提示应是“限定时间内没有确认”，而不是“连接断开”。UDP 没有连接可供判断。

生产程序常用有限重试和指数退避，例如等待 500、1000、2000 毫秒；本示例固定等待 1000 毫秒以便观察。

## 3. Windows 优先示例

示例只绑定 127.0.0.1:9001，适合本机练习。

### 3.1 Windows 服务端：reliable_udp_server_win.c

~~~c
#include <winsock2.h>
#include <stdio.h>
#include <string.h>
#pragma comment(lib, "Ws2_32.lib")

#define PORT 9001
#define SIZE 1024

int main(void) {
    WSADATA wsa;
    SOCKET s;
    struct sockaddr_in addr = {0};
    unsigned int expected = 0;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { printf("socket: %d\n", WSAGetLastError()); return 1; }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (const struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("bind: %d\n", WSAGetLastError());
        closesocket(s); WSACleanup(); return 1;
    }
    printf("server: 127.0.0.1:%d\n", PORT);

    for (;;) {
        char buf[SIZE], payload[SIZE], ack[64];
        struct sockaddr_in client;
        int client_len = sizeof(client);
        unsigned int seq;
        int n = recvfrom(s, buf, SIZE - 1, 0,
                         (struct sockaddr *)&client, &client_len);
        if (n == SOCKET_ERROR) { printf("recvfrom: %d\n", WSAGetLastError()); continue; }
        buf[n] = '\0';

        /* 这个格式允许 payload 中包含空格。 */
        if (sscanf(buf, "DATA %u %1023[^\n]", &seq, payload) != 2) {
            printf("bad packet: %s\n", buf); continue;
        }
        if (seq == expected) {
            printf("process seq=%u, payload=%s\n", seq, payload);
            /* 真实业务在这里执行一次。 */
            expected++;
        } else if (seq < expected) {
            printf("duplicate seq=%u; ignore business\n", seq);
        } else {
            printf("out of order: got=%u expected=%u\n", seq, expected);
            continue;
        }
        _snprintf_s(ack, sizeof(ack), _TRUNCATE, "ACK %u", seq);
        sendto(s, ack, (int)strlen(ack), 0,
               (const struct sockaddr *)&client, client_len);
    }
}
~~~

### 3.2 Windows 客户端：reliable_udp_client_win.c

~~~c
#include <winsock2.h>
#include <stdio.h>
#include <string.h>
#pragma comment(lib, "Ws2_32.lib")

#define PORT 9001
#define SIZE 1024
#define RETRIES 3

int main(void) {
    WSADATA wsa;
    SOCKET s;
    struct sockaddr_in server = {0};
    char input[SIZE], packet[SIZE], reply[64];
    unsigned int seq = 0;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { printf("socket: %d\n", WSAGetLastError()); return 1; }

    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    printf("Input a message: ");
    if (fgets(input, sizeof(input), stdin) == NULL) return 1;
    input[strcspn(input, "\r\n")] = '\0';
    _snprintf_s(packet, sizeof(packet), _TRUNCATE, "DATA %u %s", seq, input);

    {
        DWORD timeout = 1000; /* Windows 超时单位是毫秒。 */
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                   (const char *)&timeout, sizeof(timeout));
    }

    for (int attempt = 1; attempt <= RETRIES; ++attempt) {
        int sent = sendto(s, packet, (int)strlen(packet), 0,
                          (const struct sockaddr *)&server, sizeof(server));
        if (sent == SOCKET_ERROR) { printf("sendto: %d\n", WSAGetLastError()); break; }
        printf("sent seq=%u (%d/%d)\n", seq, attempt, RETRIES);

        for (;;) {
            int n = recvfrom(s, reply, sizeof(reply) - 1, 0, NULL, NULL);
            if (n == SOCKET_ERROR) {
                if (WSAGetLastError() == WSAETIMEDOUT) {
                    printf("timeout; retrying...\n"); break;
                }
                printf("recvfrom: %d\n", WSAGetLastError());
                closesocket(s); WSACleanup(); return 1;
            }
            reply[n] = '\0';
            {
                unsigned int ack_seq;
                if (sscanf(reply, "ACK %u", &ack_seq) == 1 && ack_seq == seq) {
                    printf("confirmed: %s\n", reply);
                    closesocket(s); WSACleanup(); return 0;
                }
            }
            printf("ignored reply: %s\n", reply);
        }
    }
    printf("failed: no ACK after %d attempts\n", RETRIES);
    closesocket(s); WSACleanup(); return 1;
}
~~~

Visual Studio Developer PowerShell：

~~~powershell
cl /W4 /Zi reliable_udp_server_win.c ws2_32.lib
cl /W4 /Zi reliable_udp_client_win.c ws2_32.lib
~~~

MinGW-w64：

~~~powershell
gcc -Wall -Wextra -g reliable_udp_server_win.c -o reliable_udp_server_win.exe -lws2_32
gcc -Wall -Wextra -g reliable_udp_client_win.c -o reliable_udp_client_win.exe -lws2_32
~~~

打开两个 PowerShell 窗口，先运行服务端，再运行客户端：

~~~powershell
.\reliable_udp_server_win.exe
.\reliable_udp_client_win.exe
~~~

停止服务端时，客户端会每秒重试，三次后报告失败。重新启动客户端会从序号 0 开始，因为示例没有持久化序号。

## 4. Linux/WSL2 等价示例

Linux 不需要 WSAStartup()，Socket 是 int，关闭使用 close()，超时结构为 timeval。

### 4.1 Linux 服务端：reliable_udp_server_linux.c

~~~c
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 9001
#define SIZE 1024

int main(void) {
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {0};
    unsigned int expected = 0;
    if (s < 0) { perror("socket"); return 1; }
    addr.sin_family = AF_INET; addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(s); return 1;
    }
    for (;;) {
        char buf[SIZE], payload[SIZE], ack[64];
        struct sockaddr_in client;
        socklen_t len = sizeof(client);
        unsigned int seq;
        ssize_t n = recvfrom(s, buf, SIZE - 1, 0,
                             (struct sockaddr *)&client, &len);
        if (n < 0) { perror("recvfrom"); continue; }
        buf[n] = '\0';
        if (sscanf(buf, "DATA %u %1023[^\n]", &seq, payload) != 2) continue;
        if (seq == expected) {
            printf("process seq=%u, payload=%s\n", seq, payload); expected++;
        } else if (seq < expected) {
            printf("duplicate seq=%u\n", seq);
        } else {
            printf("out of order: got=%u expected=%u\n", seq, expected); continue;
        }
        snprintf(ack, sizeof(ack), "ACK %u", seq);
        sendto(s, ack, strlen(ack), 0, (struct sockaddr *)&client, len);
    }
}
~~~

### 4.2 Linux 客户端：reliable_udp_client_linux.c

~~~c
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define PORT 9001
#define SIZE 1024
#define RETRIES 3

int main(void) {
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in server = {0};
    char input[SIZE], packet[SIZE], reply[64];
    unsigned int seq = 0;
    if (s < 0) { perror("socket"); return 1; }
    server.sin_family = AF_INET; server.sin_port = htons(PORT);
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    printf("Input a message: ");
    if (fgets(input, sizeof(input), stdin) == NULL) return 1;
    input[strcspn(input, "\r\n")] = '\0';
    snprintf(packet, sizeof(packet), "DATA %u %s", seq, input);

    struct timeval timeout = {1, 0}; /* Linux 超时单位是秒和微秒。 */
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt"); close(s); return 1;
    }
    for (int attempt = 1; attempt <= RETRIES; ++attempt) {
        if (sendto(s, packet, strlen(packet), 0,
                   (struct sockaddr *)&server, sizeof(server)) < 0) {
            perror("sendto"); close(s); return 1;
        }
        printf("sent seq=%u (%d/%d)\n", seq, attempt, RETRIES);
        for (;;) {
            ssize_t n = recvfrom(s, reply, sizeof(reply) - 1, 0, NULL, NULL);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                perror("recvfrom"); close(s); return 1;
            }
            reply[n] = '\0';
            {
                unsigned int ack_seq;
                if (sscanf(reply, "ACK %u", &ack_seq) == 1 && ack_seq == seq) {
                    printf("confirmed: %s\n", reply); close(s); return 0;
                }
            }
        }
        printf("timeout; retrying...\n");
    }
    printf("failed: no ACK after %d attempts\n", RETRIES);
    close(s); return 1;
}
~~~

编译运行：

~~~bash
gcc -Wall -Wextra -std=c11 -g reliable_udp_server_linux.c -o reliable_udp_server_linux
gcc -Wall -Wextra -std=c11 -g reliable_udp_client_linux.c -o reliable_udp_client_linux
./reliable_udp_server_linux
./reliable_udp_client_linux
~~~

## 5. 走向真实项目

### 5.1 ACK 不一定等于业务成功

如果业务包含数据库写入，应在写入成功后再发送 ACK。也可以明确返回：

~~~text
ACK 12 OK
ACK 12 ERROR insufficient_balance
~~~

### 5.2 幂等性

“给账户加 100 元”不能每收到一次就执行一次。服务端应把客户端 ID 和请求 ID 组成唯一键：第一次处理并记录结果，重复请求直接返回已保存的结果。

~~~text
请求 ID = client-A:42
第一次：扣款并记录，返回成功
重试：发现 ID 已完成，不再扣款，只返回原结果
~~~

### 5.3 大消息、并发和安全

- 大文件需要分片号、总片数和重组超时。
- 多客户端要分别保存 expected_seq，不能共用一个序号。
- 停等吞吐量低；高性能方案使用滑动窗口、累计 ACK、选择性重传。
- 数据报过大可能触发 IP 分片，应主动控制大小。
- 序号和 ACK 可能被伪造；重要业务需要认证和重放保护，不要直接暴露公网。

## 实践

1. Windows 先启动服务端，再运行客户端，输入 hello udp，观察序号和 ACK。
2. 停止服务端，确认客户端重试三次后失败。
3. 暂时不发送 ACK，观察客户端重传；恢复后确认服务端只处理一次。
4. 思考为什么多客户端不能共用 expected_seq。
5. 把固定超时改成 500、1000、2000 毫秒，比较固定重试和指数退避。

## 检查

- [ ] 我知道 sendto() 成功不代表对方已经收到或处理。
- [ ] 我能说明序号、ACK、超时和重传分别解决什么问题。
- [ ] 我能解释 ACK 丢失为什么会出现重复数据。
- [ ] 我知道重要请求必须去重并保证幂等。
- [ ] 我能区分超时与“服务器断开”。
- [ ] 我知道停等简单但吞吐量低，生产项目可能需要滑动窗口。
- [ ] 我能说出 Windows 和 Linux 的初始化、关闭、错误和超时差异。

## 关联

- [[UDP基础]]
- [[UDP客户端与服务器]]
- [[数据报与地址]]
- [[05-协议与工程实践/心跳、超时与重连]]
- [[05-协议与工程实践/消息格式与版本]]
- [[05-协议与工程实践/应用层协议设计]]
