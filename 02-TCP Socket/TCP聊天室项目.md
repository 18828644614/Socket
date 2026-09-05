---
type: topic
status: complete
created: 2026-08-25
updated: 2026-08-31
tags:
  - TCP
  - 项目
  - 聊天室
---

# TCP 聊天室项目

本章把前面学过的 `socket()`、`bind()`、`listen()`、`accept()`、`connect()`、`send()` 和 `recv()` 组合成一个可以运行的**多人 TCP 聊天室**。代码先给出 Windows Winsock 版本，再给出 Linux/POSIX 版本；两个版本使用同一个应用层协议，因此 Windows 客户端也可以连接 Linux 服务器。

> 本章的重点是理解流程，不是做一个带账号、加密和持久化的生产系统。示例使用“一个客户端对应一个线程”，适合入门；大规模聊天室应改用 I/O 多路复用或异步框架。

## 1. 项目目标和最终效果

启动服务器后，多个客户端都连接到 `127.0.0.1:8080`。任意客户端输入一行文字，服务器就把它转发给所有在线客户端：

```text
客户端 A 输入：你好
客户端 A 收到：[1] 你好
客户端 B 收到：[1] 你好
```

本章完成后，你应该能够：

- 解释监听 Socket 与客户端连接 Socket 的区别。
- 看懂“主线程接收连接、工作线程收发消息”的结构。
- 理解为什么客户端需要一个接收线程，才能边输入边显示别人发来的消息。
- 使用换行符定义消息边界，并正确处理半包、粘包、断线和超长消息。
- 在 Windows 和 Linux 上编译、运行并排查常见问题。

## 2. 先画出整体结构

### 2.1 服务器的线程结构

服务器只有一个监听 Socket，但每次 `accept()` 都会得到一个新的连接 Socket：

```text
主线程
  socket -> bind -> listen
                  |
                  +-- accept -> client_socket_1 -> 工作线程 1
                  +-- accept -> client_socket_2 -> 工作线程 2
                  +-- accept -> client_socket_3 -> 工作线程 3
```

- **主线程**只做 `accept()`，负责把新客户端交给工作线程。
- **工作线程**只处理一个客户端：读取它发来的消息，再调用广播函数。
- **客户端列表**是所有工作线程共享的数据，因此修改列表时必须加锁。

如果把所有代码都写在主线程里，客户端 A 一直不发消息时，服务器会卡在 `recv(A)`，无法及时 `accept()` 客户端 B。这正是并发模型要解决的问题。

### 2.2 客户端为什么也要两个执行流

聊天客户端有两个互相独立的等待操作：

```text
主线程       等待键盘输入 -> send()
接收线程     等待服务器消息 -> recv() -> printf()
```

只有一个线程时，如果程序正在 `fgets()` 等待你输入，服务器转发来的消息就无法显示；反过来也一样。客户端接收线程发现连接结束后会设置状态并关闭网络方向，主线程在下一次检查状态或读到输入结束时退出，最后统一关闭 Socket。

## 3. 消息协议：一行就是一条消息

TCP 是字节流，不保留 `send()` 调用边界。客户端连续发送两次：

```c
send(sock, "hello\\n", 6, 0);
send(sock, "world\\n", 6, 0);
```

服务器可能一次 `recv()` 得到 `hello\\nworld\\n`，也可能先得到 `hel`，下一次得到 `lo\\nworld\\n`。这两种情况都合法，不能把一次 `recv()` 当成一条消息。

本项目约定：**每条聊天内容以 `\\n` 结束**。接收方持续读取字节，遇到换行才交付一条消息；如果一次读取包含两行，就依次交付两条。示例还限制单行最多 `1023` 个字节（不含换行），防止对方发送无限长数据耗尽内存。

### 3.1 `recv_line()` 的返回值

代码中的辅助函数统一约定：

| 返回值 | 含义 | 处理方式 |
| --- | --- | --- |
| `> 0` | 读到一条完整消息，值是消息字节数 | 交给广播函数 |
| `0` | 对端有序关闭连接 | 清理客户端并退出线程 |
| `-1` | Socket 错误 | 打印错误并断开 |
| `-2` | 消息超过最大长度 | 丢弃这一行，回复错误，继续等待下一行 |

超长时仍然要把这一行读到换行符再恢复正常解析，否则下一行的开头可能会被误当成上一行的尾部。

## 4. Windows 版本（优先）

下面的 Windows 代码使用 Winsock2 和 C 运行库线程。把两个代码块分别保存为 `chat_server_win.c` 和 `chat_client_win.c`。

### 4.1 Windows 服务器

```c
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <process.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "Ws2_32.lib")

#define SERVER_PORT 8080
#define MAX_CLIENTS 32
#define MAX_LINE 1024

typedef struct {
    SOCKET sock;
    unsigned id;
    int active;
} Client;
static Client clients[MAX_CLIENTS];
static CRITICAL_SECTION clients_lock;
static unsigned next_id = 1;

/* send() 可能只发送一部分，必须循环直到全部写完。 */
static int send_all(SOCKET sock, const char *data, int length) {
    int sent = 0;
    while (sent < length) {
        int n = send(sock, data + sent, length - sent, 0);
        if (n == SOCKET_ERROR || n == 0) return -1;
        sent += n;
    }
    return 0;
}

/* 读到换行才返回一条消息；超长消息会继续读到换行以保持同步。 */
static int recv_line(SOCKET sock, char *line, int capacity) {
    int used = 0, too_long = 0;
    for (;;) {
        char ch;
        int n = recv(sock, &ch, 1, 0);
        if (n == 1) {
            if (ch == '\n') break;
            if (used + 1 < capacity) {
                line[used++] = ch;
            } else {
                too_long = 1;
            }
        } else if (n == 0) {
            if (used == 0 && !too_long) return 0;
            break;
        } else {
            return -1;
        }
    }
    line[used] = '\0';
    return too_long ? -2 : used;
}

static int add_client(SOCKET sock, unsigned *id_out) {
    int result = -1;
    EnterCriticalSection(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i].active) {
            clients[i].sock = sock;
            clients[i].id = next_id++;
            clients[i].active = 1;
            *id_out = clients[i].id;
            result = 0;
            break;
        }
    }
    LeaveCriticalSection(&clients_lock);
    return result;
}

static void remove_client(SOCKET sock) {
    EnterCriticalSection(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active && clients[i].sock == sock) {
            clients[i].active = 0;
            break;
        }
    }
    LeaveCriticalSection(&clients_lock);
}

static void broadcast_message(SOCKET sender, unsigned sender_id, const char *line) {
    char packet[MAX_LINE + 64];
    int length = _snprintf_s(packet, sizeof packet, _TRUNCATE, "[%u] %s\n", sender_id, line);
    if (length < 0) return;
    /* 先复制列表，再解锁发送，避免一个慢客户端阻塞所有人。 */
    SOCKET targets[MAX_CLIENTS];
    int count = 0;
    EnterCriticalSection(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active && clients[i].sock != sender)
            targets[count++] = clients[i].sock;
    }
    LeaveCriticalSection(&clients_lock);
    for (int i = 0; i < count; ++i) {
        if (send_all(targets[i], packet, length) < 0)
            remove_client(targets[i]);
    }
}

static unsigned __stdcall client_thread(void *argument) {
    SOCKET sock = (SOCKET)argument;
    unsigned my_id = 0;
    EnterCriticalSection(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; ++i)
        if (clients[i].active && clients[i].sock == sock)
            my_id = clients[i].id;
    LeaveCriticalSection(&clients_lock);

    char line[MAX_LINE];
    for (;;) {
        int n = recv_line(sock, line, sizeof line);
        if (n == 0) {
            printf("client %u disconnected\n", my_id);
            break;
        }
        if (n == -2) {
            const char error[] = "[server] message too long (max 1023 bytes)\n";
            send_all(sock, error, (int)sizeof error - 1);
            continue;
        }
        if (n < 0) {
            printf("client %u recv error: %d\n", my_id, WSAGetLastError());
            break;
        }
        printf("[%u] %s\n", my_id, line);
        broadcast_message(sock, my_id, line);
    }
    remove_client(sock);
    shutdown(sock, SD_BOTH);
    closesocket(sock);
    return 0;
}

int main(void) {
    WSADATA wsa;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (wsa_result != 0) {
        printf("WSAStartup failed: %d\n", wsa_result);
        return 1;
    }
    InitializeCriticalSection(&clients_lock);
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        DeleteCriticalSection(&clients_lock);
        WSACleanup();
        return 1;
    }
    BOOL reuse = TRUE;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof reuse);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(listen_sock, (struct sockaddr *)&address, sizeof address) == SOCKET_ERROR || listen(listen_sock, 16) == SOCKET_ERROR) {
        printf("bind/listen failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        DeleteCriticalSection(&clients_lock);
        WSACleanup();
        return 1;
    }
    printf("chat server listening on 127.0.0.1:%d\n", SERVER_PORT);
    for (;;) {
        SOCKET client = accept(listen_sock, NULL, NULL);
        if (client == INVALID_SOCKET) {
            printf("accept failed: %d\n", WSAGetLastError());
            break;
        }
        unsigned id;
        if (add_client(client, &id) < 0) {
            const char full[] = "[server] room is full\n";
            send_all(client, full, (int)sizeof full - 1);
            closesocket(client);
            continue;
        }
        printf("client %u connected\n", id);
        uintptr_t handle = _beginthreadex(NULL, 0, client_thread, (void *)client, 0, NULL);
        if (handle == 0) {
            printf("cannot create thread for client %u\n", id);
            remove_client(client);
            closesocket(client);
        } else {
            CloseHandle((HANDLE)handle); /* 线程继续运行，主线程不必等待。 */
        }
    }
    closesocket(listen_sock);
    DeleteCriticalSection(&clients_lock);
    WSACleanup();
    return 0;
}
```

代码中有三个容易忽略的点：

1. `clients` 会被多个线程同时读写，`CRITICAL_SECTION` 是 Windows 的互斥锁；进入锁后要尽快离开。
2. 广播时先复制目标 Socket，再解锁后发送。网络发送可能阻塞，不能让锁一直被占用。
3. `_beginthreadex()` 创建的线程使用 C 运行库，适合这个包含 `printf()`、字符串函数的示例；返回的句柄可以立即 `CloseHandle()`，并不表示线程被终止。

### 4.2 Windows 客户端

```c
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <process.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "Ws2_32.lib")
#define SERVER_PORT 8080
#define MAX_LINE 1024
static volatile LONG running = 1;

static int send_all(SOCKET sock, const char *data, int length) {
    int sent = 0;
    while (sent < length) {
        int n = send(sock, data + sent, length - sent, 0);
        if (n == SOCKET_ERROR || n == 0) return -1;
        sent += n;
    }
    return 0;
}

static int recv_line(SOCKET sock, char *line, int capacity) {
    int used = 0;
    for (;;) {
        char ch;
        int n = recv(sock, &ch, 1, 0);
        if (n == 1) {
            if (ch == '\n') break;
            if (used + 1 >= capacity) return -2;
            line[used++] = ch;
        } else if (n == 0) {
            if (used == 0) return 0;
            break;
        } else {
            return -1;
        }
    }
    line[used] = '\0';
    return used;
}

static unsigned __stdcall receiver_thread(void *argument) {
    SOCKET sock = (SOCKET)argument;
    char line[MAX_LINE];
    for (;;) {
        int n = recv_line(sock, line, sizeof line);
        if (n == 0) {
            puts("server disconnected");
            break;
        }
        if (n == -2) {
            puts("received an overlong message");
            continue;
        }
        if (n < 0) {
            printf("recv failed: %d\n", WSAGetLastError());
            break;
        }
        printf("%s\n", line);
    }
    InterlockedExchange(&running, 0);
    shutdown(sock, SD_BOTH); /* 让后续网络操作尽快返回。 */
    return 0;
}
int main(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(sock, (struct sockaddr *)&address, sizeof address) == SOCKET_ERROR) {
        printf("connect failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    uintptr_t handle = _beginthreadex(NULL, 0, receiver_thread,
                                      (void *)sock, 0, NULL);
    if (handle == 0) {
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    CloseHandle((HANDLE)handle);
    puts("connected. type a line and press Enter; Ctrl-Z then Enter quits.");
    char line[MAX_LINE];
    while (InterlockedCompareExchange(&running, 1, 1) && fgets(line, sizeof line, stdin)) {
        size_t length = strlen(line);
        if (length == 0 || line[0] == '\n') continue;
        if (line[length - 1] != '\n') {
            if (length + 1 >= sizeof line) {
                puts("message too long");
                continue;
            }
            line[length++] = '\n';
            line[length] = '\0';
        }
        if (send_all(sock, line, (int)length) < 0) {
            printf("send failed: %d\n", WSAGetLastError());
            break;
        }
    }
    InterlockedExchange(&running, 0);
    shutdown(sock, SD_BOTH);
    closesocket(sock);
    WSACleanup();
    return 0;
}
```

### 4.3 Windows 编译和运行

使用 Visual Studio Developer Command Prompt：

```bat
cl /W4 /EHsc chat_server_win.c Ws2_32.lib
cl /W4 /EHsc chat_client_win.c Ws2_32.lib
```

使用安装了 MinGW-w64 的 PowerShell：

```powershell
gcc -Wall -Wextra -O2 chat_server_win.c -o chat_server_win.exe -lws2_32
gcc -Wall -Wextra -O2 chat_client_win.c -o chat_client_win.exe -lws2_32
```

打开三个 PowerShell 窗口，按顺序运行：

```powershell
.\chat_server_win.exe   # 窗口 1：先启动服务器
.\chat_client_win.exe   # 窗口 2：客户端 A
.\chat_client_win.exe   # 窗口 3：客户端 B
```

在 A 中输入 `hello`，B 应看到类似 `[1] hello` 的内容。Windows 控制台结束输入通常是 `Ctrl-Z` 后按回车；服务器停止可按 `Ctrl-C`。如果编译器提示找不到 `winsock2.h`，说明尚未安装 Visual Studio C++ 或 MinGW-w64 的 Windows 开发环境。

> 若要让局域网其他电脑连接，把服务器代码中的 `127.0.0.1` 改为 `0.0.0.0`，客户端改为服务器的局域网 IP，并在 Windows 防火墙中允许 TCP 入站端口 `8080`。练习结束后建议恢复为 `127.0.0.1`。

## 5. Linux 版本（POSIX Socket）

Linux 与 Windows 的业务逻辑相同，主要替换 API：`SOCKET` 变为 `int`，`closesocket()` 变为 `close()`，不需要 `WSAStartup()`；线程使用 POSIX `pthread`。

### 5.1 Linux 服务器 `chat_server_linux.c`

```c
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_PORT 8080
#define MAX_CLIENTS 32
#define MAX_LINE 1024
typedef struct {
    int fd;
    unsigned id;
    int active;
} Client;
static Client clients[MAX_CLIENTS];
static pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned next_id = 1;

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
static int recv_line(int fd, char *line, size_t capacity) {
    size_t used = 0;
    int too_long = 0;
    for (;;) {
        char ch;
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n == 1) {
            if (ch == '\n') break;
            if (used + 1 < capacity) line[used++] = ch;
            else too_long = 1;
        } else if (n == 0) {
            if (used == 0 && !too_long) return 0;
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    line[used] = '\0';
    return too_long ? -2 : (int)used;
}
static int add_client(int fd, unsigned *id_out) {
    int result = -1;
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i].active) {
            clients[i] = (Client){fd, next_id++, 1};
            *id_out = clients[i].id;
            result = 0;
            break;
        }
    }
    pthread_mutex_unlock(&clients_lock);
    return result;
}
static void remove_client(int fd) {
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active && clients[i].fd == fd) {
            clients[i].active = 0;
            break;
        }
    }
    pthread_mutex_unlock(&clients_lock);
}
static void broadcast_message(int sender, unsigned id, const char *line) {
    char packet[MAX_LINE + 64];
    int length = snprintf(packet, sizeof packet, "[%u] %s\n", id, line);
    if (length < 0 || (size_t)length >= sizeof packet) return;

    int targets[MAX_CLIENTS];
    int count = 0;
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active && clients[i].fd != sender)
            targets[count++] = clients[i].fd;
    }
    pthread_mutex_unlock(&clients_lock);

    for (int i = 0; i < count; ++i) {
        if (send_all(targets[i], packet, (size_t)length) < 0)
            remove_client(targets[i]);
    }
}
static void *client_thread(void *argument) {
    int fd = *(int *)argument;
    free(argument);

    unsigned id = 0;
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active && clients[i].fd == fd)
            id = clients[i].id;
    }
    pthread_mutex_unlock(&clients_lock);

    char line[MAX_LINE];
    for (;;) {
        int n = recv_line(fd, line, sizeof line);
        if (n == 0) break;
        if (n == -2) {
            const char error[] = "[server] message too long (max 1023 bytes)\n";
            send_all(fd, error, sizeof error - 1);
            continue;
        }
        if (n < 0) {
            perror("recv");
            break;
        }
        printf("[%u] %s\n", id, line);
        broadcast_message(fd, id, line);
    }
    remove_client(fd);
    shutdown(fd, SHUT_RDWR);
    close(fd);
    return NULL;
}
int main(void) {
    signal(SIGPIPE, SIG_IGN);
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(SERVER_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listen_fd, (struct sockaddr *)&address, sizeof address) < 0 ||
        listen(listen_fd, 16) < 0) {
        perror("bind/listen");
        close(listen_fd);
        return 1;
    }
    printf("chat server listening on 127.0.0.1:%d\n", SERVER_PORT);
    for (;;) {
        int fd = accept(listen_fd, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        unsigned id;
        if (add_client(fd, &id) < 0) {
            const char full[] = "[server] room is full\n";
            send_all(fd, full, sizeof full - 1);
            close(fd);
            continue;
        }

        int *argument = malloc(sizeof *argument);
        if (!argument) {
            remove_client(fd);
            close(fd);
            continue;
        }
        *argument = fd;

        pthread_t thread;
        if (pthread_create(&thread, NULL, client_thread, argument) != 0) {
            free(argument);
            remove_client(fd);
            close(fd);
        } else {
            pthread_detach(thread);
        }
    }
    close(listen_fd);
    return 0;
}
```

### 5.2 Linux 客户端 `chat_client_linux.c`

```c
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define SERVER_PORT 8080
#define MAX_LINE 1024
static volatile sig_atomic_t running = 1;
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

static int recv_line(int fd, char *line, size_t capacity) {
    size_t used = 0;
    for (;;) {
        char ch;
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n == 1) {
            if (ch == '\n') break;
            if (used + 1 >= capacity) return -2;
            line[used++] = ch;
        } else if (n == 0) {
            return 0;
        } else if (errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    line[used] = '\0';
    return (int)used;
}

static void *receiver_thread(void *argument) {
    int fd = *(int *)argument;
    char line[MAX_LINE];
    while (running) {
        int n = recv_line(fd, line, sizeof line);
        if (n == -2) {
            puts("received an overlong message");
            continue;
        }
        if (n <= 0) break;
        printf("%s\n", line);
    }
    running = 0;
    shutdown(fd, SHUT_RDWR);
    return NULL;
}

int main(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(fd, (struct sockaddr *)&address, sizeof address) < 0) {
        perror("connect");
        close(fd);
        return 1;
    }

    pthread_t thread;
    if (pthread_create(&thread, NULL, receiver_thread, &fd) != 0) {
        close(fd);
        return 1;
    }

    puts("connected. type a line and press Enter; Ctrl-D quits.");
    char line[MAX_LINE];
    while (running && fgets(line, sizeof line, stdin)) {
        size_t length = strlen(line);
        if (length == 0 || line[0] == '\n') continue;
        if (line[length - 1] != '\n') {
            if (length + 1 >= sizeof line) {
                puts("message too long");
                continue;
            }
            line[length++] = '\n';
            line[length] = '\0';
        }
        if (send_all(fd, line, length) < 0) {
            perror("send");
            break;
        }
    }

    running = 0;
    shutdown(fd, SHUT_RDWR);
    close(fd);
    pthread_join(thread, NULL);
    return 0;
}
```

编译和运行：

```bash
gcc -Wall -Wextra -O2 chat_server_linux.c -o chat_server_linux -pthread
gcc -Wall -Wextra -O2 chat_client_linux.c -o chat_client_linux -pthread
./chat_server_linux              # 终端 1
./chat_client_linux              # 终端 2
./chat_client_linux              # 终端 3
```

Linux 输入结束通常是 `Ctrl-D`。`127.0.0.1` 只允许同一台电脑连接；需要局域网访问时，把服务器地址改成 `INADDR_ANY`，客户端填写服务器的实际 IP，并检查防火墙。

## 6. 按一次聊天消息的顺序理解代码

以客户端 A 输入 `hello` 为例：

1. `fgets()` 读到 `hello\n`，客户端主线程调用 `send_all()`。
2. TCP 把字节可靠地送到服务器，但可能分成多次到达。
3. A 对应的服务器工作线程调用 `recv_line()`，持续读取直到 `\n`。
4. 工作线程构造 `[A的编号] hello\n`，调用 `broadcast_message()`。
5. 广播函数复制当前在线客户端的 Socket，然后逐个 `send_all()`。
6. 每个客户端的接收线程从 `recv_line()` 得到完整一行并打印。

注意，服务器把消息转发给“其他客户端”，没有回发给发送者；想让发送者也收到消息，只需删除广播代码中 `clients[i].sock != sender`（或 Linux 中 `clients[i].fd != sender`）这个条件。

## 7. 重要细节和常见错误

### 7.1 `recv() == 0` 不是“暂时没数据”

阻塞 Socket 在暂时没有数据时会继续等待。返回 `0` 表示对端已经有序关闭发送方向，不会再有新的字节；服务器应退出该客户端线程并关闭连接。

### 7.2 不要直接使用 `strlen(buffer)`

`recv()` 得到的是字节数量，并不会自动在末尾写 `\0`。本章先按字节读取，再由 `recv_line()` 手动补结束符；二进制协议则必须始终使用显式长度。

### 7.3 锁不能包住慢速网络发送

如果一个客户端不读取数据，向它 `send()` 可能阻塞。把 `send()` 放在全局客户端锁内，会让其他线程无法加入、退出或广播。本章先复制 Socket 列表再发送，已经避免了这个初学者常见错误；生产系统还应设置发送队列、超时和更严格的连接状态管理。

### 7.4 线程数量不是无限的

示例最多保留 32 个客户端。真实服务还要限制线程栈、消息频率、单条消息大小，并在客户端异常时及时释放资源。线程模型在几百或几千连接时通常不如 `select`、`poll`、`epoll` 或 Windows IOCP。

### 7.5 中文和字符编码

协议传输的是字节。UTF-8 中文一个字符通常占多个字节，长度限制按字节计算；不要在收到半个 UTF-8 字符时就做字符串解码。Windows 终端的代码页也可能影响显示，遇到乱码时统一使用 UTF-8 保存源码，并根据终端设置调整代码页。

## 8. 测试清单

1. 连接两个客户端，确认 A 的消息能出现在 B；B 的消息能出现在 A。
2. 连续快速发送多行，确认每一行都独立显示，没有粘在一起。
3. 关闭一个客户端，确认服务器仍能服务另一个客户端。
4. 发送超过 1023 字节的一行，确认收到错误提示后仍能继续发送下一行。
5. 启动两个服务器实例，观察第二个实例的 `bind` 失败，并用 `netstat -ano | findstr :8080`（Windows）或 `ss -ltnp | grep 8080`（Linux）找出占用端口的进程。
6. Windows 连接失败时，先确认服务器已启动、地址端口一致，再检查防火墙；不要一开始就怀疑 TCP 本身。

## 9. 可以怎样继续改进

- 增加用户名、加入/离开通知和私聊命令；先扩展文本协议，再考虑结构化 JSON 或长度前缀。
- 把“接收一行”和“发送全部字节”提取到公共模块，避免 Windows/Linux 代码重复。
- 为客户端增加发送锁或发送队列，避免多个线程同时写同一个 Socket。
- 使用 `select`/`poll`/`epoll`（Linux）或 IOCP（Windows）处理更多连接。
- 加入 TLS、身份认证、速率限制和日志；这些是生产聊天室必须补齐的安全能力。

## 关联

- [[TCP客户端]]
- [[TCP服务器]]
- [[连接生命周期]]
- [[发送与接收数据]]
- [[消息边界与拆包]]
- [[04-并发与I-O模型/线程与进程模型]]
- [[04-并发与I-O模型/I-O多路复用]]

## 10. Windows 代码详细讲解

本节专门解释前面的 Windows 服务器和客户端代码。建议先理解这里的整体流程，再逐个阅读代码中的辅助函数。

### 10.1 聊天室的整体结构

服务器和客户端承担不同职责。

```text
服务器主线程：
    创建监听 Socket
    等待客户端连接
    每连接一个客户端，就创建一个工作线程

服务器工作线程：
    接收某一个客户端的消息
    把消息广播给其他客户端

客户端主线程：
    等待键盘输入
    将输入内容发送给服务器

客户端接收线程：
    等待服务器转发消息
    收到消息后显示到屏幕
```

客户端为什么需要两个线程？如果只有一个线程，程序在执行 `fgets()` 等待键盘输入时，就没有机会调用 `recv()` 读取其他人发来的消息。现在的设计让两个等待操作彼此独立：主线程负责输入，接收线程负责网络消息。接收线程发现服务器断开后会设置运行状态并关闭网络方向；主线程会在下一次检查状态或读到输入结束时退出，最后统一清理 Socket。

### 10.2 Windows 头文件和链接库

```c
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
```

- `winsock2.h`：定义 `SOCKET`、`socket()`、`bind()`、`listen()`、`accept()`、`send()`、`recv()` 等 Winsock API。
- `windows.h`：提供 `CRITICAL_SECTION`、`InterlockedExchange()` 和 `CloseHandle()` 等 Windows 功能。
- `ws2tcpip.h`：提供 `inet_pton()` 等 IP 地址转换函数。
- `process.h`：提供适合 C 运行库的 `_beginthreadex()`。
- `stdio.h`：提供 `printf()`、`puts()` 和 `fgets()`。
- `string.h`：提供 `strlen()`。

```c
#pragma comment(lib, "Ws2_32.lib")
```

Winsock 的实现位于 `Ws2_32.lib`。Visual Studio 会根据这行指令自动链接；使用 MinGW-w64 时仍然要在命令末尾写 `-lws2_32`。

### 10.3 常量和客户端结构体

```c
#define SERVER_PORT 8080
#define MAX_CLIENTS 32
#define MAX_LINE 1024

typedef struct {
    SOCKET sock;
    unsigned id;
    int active;
} Client;
```

`MAX_LINE` 为 1024，但有效消息最多是 1023 字节，因为最后一个字节要存放 C 字符串结束符 `\\0`。

`Client` 结构体表示一个在线客户端：

- `sock`：和该客户端通信的连接 Socket。
- `id`：服务器分配的编号，例如 1、2、3。
- `active`：是否正在使用这个数组位置。

服务器通常同时拥有两类 Socket：

```text
listen_sock：只负责监听新连接
client.sock：只负责与某一个客户端收发数据
```

### 10.4 `send_all()`：处理部分发送

```c
static int send_all(SOCKET sock, const char *data, int length) {
    int sent = 0;

    while (sent < length) {
        int n = send(sock, data + sent, length - sent, 0);

        if (n == SOCKET_ERROR || n == 0) {
            return -1;
        }

        sent += n;
    }

    return 0;
}
```

一次 `send()` 不一定能发送完所有数据。例如要发送 1000 字节，第一次可能只发送 600 字节。因此用 `sent` 记录已经发送的数量，再从 `data + sent` 继续发送剩余部分。

```text
第一次：send(data, 1000) 返回 600
第二次：send(data + 600, 400)
```

`SOCKET_ERROR` 表示调用失败，返回 `-1` 让调用者关闭出问题的连接。

### 10.5 `recv_line()`：读取一整条消息

本项目规定“换行符就是消息结束标记”。由于 TCP 是字节流，客户端发送的 `hello\\n` 可能被拆成：

```text
第一次 recv：hel
第二次 recv：lo\\n
```

所以不能假设一次 `recv()` 就是一条完整消息。`recv_line()` 每次读取一个字节，直到遇到 `\\n` 才返回。

```c
static int recv_line(SOCKET sock, char *line, int capacity) {
    int used = 0;

    for (;;) {
        char ch;
        int n = recv(sock, &ch, 1, 0);

        if (n == 1) {
            if (ch == '\n') {
                break;
            }

            if (used + 1 >= capacity) {
                return -2;
            }

            line[used++] = ch;
        } else if (n == 0) {
            if (used == 0) {
                return 0;
            }

            break;
        } else {
            return -1;
        }
    }

    line[used] = '\0';
    return used;
}
```

返回值约定如下：

| 返回值 | 含义 |
| --- | --- |
| 大于 0 | 成功读到一条消息，返回消息长度 |
| `0` | 对端正常关闭连接 |
| `-1` | Socket 错误 |
| `-2` | 消息超过缓冲区限制 |

`recv()` 返回 `0` 不是“暂时没有数据”。阻塞 Socket 在暂时没有数据时会继续等待；返回 `0` 代表对端不会再发送新的字节。

### 10.6 客户端列表和临界区

```c
static Client clients[MAX_CLIENTS];
static CRITICAL_SECTION clients_lock;
static unsigned next_id = 1;
```

服务器的多个工作线程会同时访问 `clients`：某个线程可能正在加入客户端，另一个线程正在广播，第三个线程正在删除已断开的客户端。因此修改或遍历列表时必须使用：

```c
EnterCriticalSection(&clients_lock);
/* 访问 clients */
LeaveCriticalSection(&clients_lock);
```

程序启动时调用：

```c
InitializeCriticalSection(&clients_lock);
```

程序结束时调用：

```c
DeleteCriticalSection(&clients_lock);
```

进入锁后应该尽快离开，不能在锁中执行慢速的 `send()`、`recv()` 或 `sleep()`。

### 10.7 `add_client()`：加入客户端

```c
static int add_client(SOCKET sock, unsigned *id_out) {
    int result = -1;

    EnterCriticalSection(&clients_lock);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i].active) {
            clients[i].sock = sock;
            clients[i].id = next_id++;
            clients[i].active = 1;
            *id_out = clients[i].id;
            result = 0;
            break;
        }
    }

    LeaveCriticalSection(&clients_lock);
    return result;
}
```

函数从数组中寻找第一个空闲位置。找到后保存 Socket、分配编号，并设置 `active = 1`。如果 32 个位置都被占用，函数保持返回 `-1`，主线程会向新客户端发送 `room is full` 并关闭连接。

`id_out` 是输出参数。调用者传入 `&id`，函数通过 `*id_out = clients[i].id` 把编号带回去。

### 10.8 `remove_client()`：移除客户端

```c
static void remove_client(SOCKET sock) {
    EnterCriticalSection(&clients_lock);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active && clients[i].sock == sock) {
            clients[i].active = 0;
            break;
        }
    }

    LeaveCriticalSection(&clients_lock);
}
```

这个函数并不移动数组元素，只把对应位置标记为空闲。这样新客户端可以复用这个位置。

### 10.9 `broadcast_message()`：广播消息

首先把消息包装成带编号的格式：

```c
char packet[MAX_LINE + 64];

int length = _snprintf_s(
    packet,
    sizeof packet,
    _TRUNCATE,
    "[%u] %s\n",
    sender_id,
    line
);
```

例如：

```text
sender_id = 2
line = hello
packet = [2] hello\\n
```

接着复制当前在线客户端的 Socket：

```c
SOCKET targets[MAX_CLIENTS];
int count = 0;

EnterCriticalSection(&clients_lock);

for (int i = 0; i < MAX_CLIENTS; ++i) {
    if (clients[i].active && clients[i].sock != sender) {
        targets[count++] = clients[i].sock;
    }
}

LeaveCriticalSection(&clients_lock);
```

`clients[i].sock != sender` 表示不把消息发回发送者。如果希望发送者也看到自己的消息，可以去掉这个条件。

复制完成后，再解锁并发送：

```c
for (int i = 0; i < count; ++i) {
    if (send_all(targets[i], packet, length) < 0) {
        remove_client(targets[i]);
    }
}
```

这样做的原因是：某个客户端读取速度很慢时，`send()` 可能阻塞。如果一直持有全局锁，其他客户端就无法加入或退出。

### 10.10 服务器工作线程 `client_thread()`

服务器为每个客户端创建一个工作线程：

```c
static unsigned __stdcall client_thread(void *argument)
```

线程首先取得自己的 Socket，并查找客户端编号：

```c
SOCKET sock = (SOCKET)argument;
unsigned my_id = 0;
```

然后不断读取消息：

```c
for (;;) {
    int n = recv_line(sock, line, sizeof line);

    if (n == 0) {
        /* 客户端已经关闭 */
        break;
    }

    if (n == -2) {
        /* 消息太长，回复错误后继续处理下一行 */
        continue;
    }

    if (n < 0) {
        /* 发生 Socket 错误 */
        break;
    }

    printf("[%u] %s\n", my_id, line);
    broadcast_message(sock, my_id, line);
}
```

循环结束后进行清理：

```c
remove_client(sock);
shutdown(sock, SD_BOTH);
closesocket(sock);
return 0;
```

`shutdown()` 关闭连接的读写方向，`closesocket()` 释放 Windows Socket 资源。Windows 应使用 `closesocket()`，不能把 Socket 当成普通文件描述符调用 `close()`。

### 10.11 服务器 `main()` 的启动流程

#### 第一步：初始化 Winsock

```c
WSADATA wsa;
int result = WSAStartup(MAKEWORD(2, 2), &wsa);
```

Windows 在使用 Socket 前必须先初始化 Winsock。`MAKEWORD(2, 2)` 表示请求 Winsock 2.2。

#### 第二步：创建 Socket

```c
SOCKET listen_sock =
    socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
```

参数含义：

```text
AF_INET：IPv4
SOCK_STREAM：TCP 字节流
IPPROTO_TCP：TCP 协议
```

Windows 创建失败时要和 `INVALID_SOCKET` 比较，而不是使用 Linux 常见的 `fd < 0`。

#### 第三步：设置地址复用

```c
BOOL reuse = TRUE;

setsockopt(
    listen_sock,
    SOL_SOCKET,
    SO_REUSEADDR,
    (const char *)&reuse,
    sizeof reuse
);
```

它可以帮助服务器重启时重新绑定端口，但不代表多个普通程序可以随意同时占用同一个端口。

#### 第四步：设置地址并绑定

```c
struct sockaddr_in address = {0};
address.sin_family = AF_INET;
address.sin_port = htons(SERVER_PORT);

inet_pton(
    AF_INET,
    "127.0.0.1",
    &address.sin_addr
);

bind(
    listen_sock,
    (struct sockaddr *)&address,
    sizeof address
);
```

`htons(8080)` 把主机字节序的端口转换为网络字节序。绑定 `127.0.0.1` 表示只允许本机程序连接；局域网测试时才考虑改为 `0.0.0.0`，并配置防火墙。

#### 第五步：开始监听

```c
listen(listen_sock, 16);
```

从这一刻开始，系统会为服务器排队保存到来的连接请求。

#### 第六步：接受连接并创建线程

```c
SOCKET client = accept(listen_sock, NULL, NULL);
```

`accept()` 默认阻塞：没有客户端时等待，有客户端连接时返回一个新的 Socket。监听 Socket 继续保留，新的 `client` Socket 交给工作线程。

```c
uintptr_t handle = _beginthreadex(
    NULL,
    0,
    client_thread,
    (void *)client,
    0,
    NULL
);
```

`CloseHandle((HANDLE)handle)` 只关闭当前线程保存的线程句柄，不会终止工作线程；工作线程仍会继续执行。

### 10.12 Windows 客户端主线程

客户端先执行与服务器类似的初始化：

```c
WSAStartup(...);
socket(...);
connect(...);
```

然后创建接收线程：

```c
uintptr_t handle = _beginthreadex(
    NULL,
    0,
    receiver_thread,
    (void *)sock,
    0,
    NULL
);
```

主线程负责读取控制台：

```c
char line[MAX_LINE];

while (
    InterlockedCompareExchange(&running, 1, 1) &&
    fgets(line, sizeof line, stdin)
) {
    ...
}
```

`fgets()` 读取一行时，通常会把回车对应的 `\\n` 一起读入。如果用户输入没有换行符，代码会手动补上：

```c
if (line[length - 1] != '\n') {
    line[length++] = '\n';
    line[length] = '\0';
}
```

最后调用：

```c
send_all(sock, line, (int)length);
```

### 10.13 Windows 客户端接收线程

接收线程不断调用：

```c
int n = recv_line(sock, line, sizeof line);
```

成功读取一行后打印：

```c
printf("%s\n", line);
```

如果服务器关闭连接，`recv_line()` 返回 `0`，线程设置：

```c
InterlockedExchange(&running, 0);
```

这是一个原子操作，可以安全地修改被多个线程读取的运行状态。随后：

```c
shutdown(sock, SD_BOTH);
```

让网络操作尽快返回。

### 10.14 一条消息的完整路径

假设客户端 A 输入 `hello`：

1. 客户端主线程通过 `fgets()` 得到 `hello\\n`。
2. 客户端主线程调用 `send_all()`。
3. TCP 把字节送到服务器，可能拆成多次到达。
4. A 对应的服务器工作线程调用 `recv_line()`，一直读取到 `\\n`。
5. 服务器得到消息 `hello`。
6. 服务器构造 `[1] hello\\n`。
7. 服务器复制其他在线客户端的 Socket。
8. 服务器向这些 Socket 调用 `send_all()`。
9. B 客户端的接收线程调用 `recv_line()`。
10. B 屏幕显示 `[1] hello`。

### 10.15 代码的能力边界

这份代码适合学习，但不是生产级聊天室：

- 一个客户端对应一个线程，连接数量很大时线程开销明显。
- `recv_line()` 每次只读取一个字节，逻辑清楚但吞吐量不高。
- 没有用户名、身份认证、TLS 加密和持久化存储。
- 服务器停止时没有完整的优雅退出流程。
- 发送队列、超时、限流和日志还需要进一步补充。

学习顺序建议是：先读懂本章的 Socket 和消息边界，再学习 `select`、`poll`、IOCP、线程池、TLS 和应用层协议设计。
