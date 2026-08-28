# `server.c` 代码说明

本文解释同目录下的 [`server.c`](server.c)。这段程序使用 **Windows Winsock** 创建一个 TCP 监听 Socket。

需要先注意两点：

1. 这不是 Linux 版本。它包含 `winsock2.h`，使用 `WSAStartup()`、`SOCKET`、`closesocket()` 等 Windows API。
2. 这段代码目前只完成了“创建 Socket、绑定端口、开始监听”。它没有调用 `accept()`，因此还不能真正接收客户端连接。

## 一、TCP 服务端的整体流程

一个最基本的 TCP 服务端通常按下面的顺序运行：

```text
WSAStartup()       初始化 Windows 网络库
       |
socket()           创建 TCP Socket
       |
bind()             绑定本机 IP 和端口
       |
listen()           进入监听状态
       |
accept()           接收客户端连接
       |
recv()/send()      收发数据
       |
closesocket()      关闭连接
       |
WSACleanup()       清理 Windows 网络库
```

当前代码只执行到 `listen()`，然后直接关闭监听 Socket 并退出。

## 二、头文件

```c
#include <winsock2.h>
#include <stdio.h>
```

### `winsock2.h`

提供 Windows Socket 编程所需的内容，包括：

- `WSAStartup()` 和 `WSACleanup()`；
- `SOCKET`、`INVALID_SOCKET`、`SOCKET_ERROR`；
- `socket()`、`bind()`、`listen()` 等函数；
- `sockaddr_in`、`AF_INET`、`SOCK_STREAM` 等类型和常量；
- `WSAGetLastError()` 错误查询函数。

### `stdio.h`

提供 `printf()`，用于打印启动信息和错误信息。

## 三、程序入口和 Winsock 初始化

```c
int main(void) {
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
```

### `WSADATA wsa_data`

`WSADATA` 是 Winsock 用来返回初始化信息的结构体。这里声明变量 `wsa_data`，然后把它的地址传给 `WSAStartup()`。

### `MAKEWORD(2, 2)`

表示请求 Winsock 2.2 版本：

```text
主版本号 = 2
次版本号 = 2
```

### `WSAStartup()`

Windows 程序在使用 Socket API 前必须先调用它。Linux 通常不需要类似的全局初始化步骤，创建 Socket 前直接调用 `socket()` 即可。

```c
    if (result != 0) {
        printf("WSAStartup failed: %d\n", result);
        return 1;
    }
```

`WSAStartup()` 返回 `0` 表示成功，非 `0` 表示失败。失败时程序不能继续使用 Winsock，所以直接返回 `1`。

这里的 `return 1` 表示程序异常结束；`return 0` 通常表示正常结束。

## 四、创建 TCP Socket

```c
SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
```

`socket()` 创建一个 Socket。可以把它理解成操作系统内核中的一个通信对象，程序通过它来操作 TCP 连接。

三个参数的含义如下：

| 参数 | 当前值 | 含义 |
| --- | --- | --- |
| 地址族 | `AF_INET` | 使用 IPv4 |
| Socket 类型 | `SOCK_STREAM` | 使用面向连接的字节流，即 TCP |
| 协议 | `IPPROTO_TCP` | 明确指定 TCP |

`AF_INET + SOCK_STREAM + IPPROTO_TCP` 就是一个 IPv4 TCP Socket。

### 检查创建是否成功

```c
if (listen_sock == INVALID_SOCKET) {
    printf("socket failed: %d\n", WSAGetLastError());
    WSACleanup();
    return 1;
}
```

Windows 创建 Socket 失败时返回 `INVALID_SOCKET`，不能用 Linux 中常见的 `-1` 判断。

`WSAGetLastError()` 获取 Winsock 错误码。例如，内存不足或参数错误都可能导致 `socket()` 失败。

由于 Winsock 已经初始化过，所以退出前要调用 `WSACleanup()`。

## 五、准备服务器地址

```c
struct sockaddr_in address = {0};
address.sin_family = AF_INET;
address.sin_port = htons(8080);
address.sin_addr.s_addr = htonl(INADDR_ANY);
```

### `sockaddr_in`

`sockaddr_in` 是 IPv4 专用的地址结构，主要字段如下：

| 字段 | 作用 | 当前值 |
| --- | --- | --- |
| `sin_family` | 地址族 | `AF_INET` |
| `sin_port` | 监听端口 | `8080` |
| `sin_addr.s_addr` | 监听 IP 地址 | `INADDR_ANY` |

`= {0}` 会把整个结构体初始化为零，避免未初始化字段带来问题。

### `sin_family = AF_INET`

告诉系统这个地址结构保存的是 IPv4 地址。它必须与创建 Socket 时使用的 `AF_INET` 一致。

### `sin_port = htons(8080)`

端口号是一个 16 位整数。不同 CPU 可能使用不同的字节排列方式，而网络协议统一使用“网络字节序”。

`htons()` 的含义是：

```text
host to network short
主机字节序 -> 网络字节序，处理 16 位整数
```

所以端口号应写成 `htons(8080)`，而不是直接写 `8080`。

对应地，显示收到的网络端口时通常使用 `ntohs()`：

```text
network to host short
网络字节序 -> 主机字节序
```

### `sin_addr.s_addr = htonl(INADDR_ANY)`

`INADDR_ANY` 表示“本机所有 IPv4 网卡地址”。因此该服务端可以接受发送到本机任意 IPv4 网卡、端口 `8080` 的连接。

常见地址的区别：

| 地址 | 含义 |
| --- | --- |
| `INADDR_ANY` / `0.0.0.0` | 监听本机所有 IPv4 网卡 |
| `INADDR_LOOPBACK` / `127.0.0.1` | 只允许本机访问 |
| `192.168.1.20` | 只监听指定的本机网卡地址 |

如果只是本机练习，可以使用回环地址，避免服务暴露到局域网：

```c
address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
```

## 六、绑定 IP 和端口：`bind()`

```c
if (bind(listen_sock,
         (const struct sockaddr *)&address,
         sizeof address) == SOCKET_ERROR) {
```

`bind()` 把 Socket 与本机地址和端口关联起来。执行成功后，操作系统知道这个 Socket 要使用本机的 `8080` 端口。

三个参数分别是：

| 参数 | 含义 |
| --- | --- |
| `listen_sock` | 要绑定的 Socket |
| `(const struct sockaddr *)&address` | 地址结构指针 |
| `sizeof address` | 地址结构长度 |

### 为什么要转换成 `struct sockaddr *`

`address` 的实际类型是 `struct sockaddr_in`，但 `bind()` 使用通用的 `struct sockaddr *` 参数，以便同时支持 IPv4、IPv6 等地址结构。

这里的强制类型转换不会复制数据，只是把 IPv4 地址结构按通用地址结构传给函数。

### 为什么比较 `SOCKET_ERROR`

Winsock 的 `bind()` 失败时返回 `SOCKET_ERROR`，而不是 Linux 中常见的 `-1`。判断失败后调用：

```c
WSAGetLastError()
```

获得具体错误码。

### `bind()` 失败后的清理

```c
    printf("bind failed: %d\n", WSAGetLastError());
    closesocket(listen_sock);
    WSACleanup();
    return 1;
```

程序在这里按照以下顺序清理：

1. 打印错误；
2. 关闭已经创建的 Socket；
3. 清理 Winsock；
4. 返回失败状态。

常见原因包括：

- `8080` 端口已经被其他程序占用；
- 地址无效；
- 没有权限绑定该端口；
- 之前启动的服务端还没有退出。

## 七、开始监听：`listen()`

```c
if (listen(listen_sock, 16) == SOCKET_ERROR) {
    printf("listen failed: %d\n", WSAGetLastError());
    closesocket(listen_sock);
    WSACleanup();
    return 1;
}
```

`listen()` 将普通 Socket 转换成监听 Socket。成功后，操作系统开始在 `8080` 端口等待客户端发起 TCP 连接。

第二个参数 `16` 是 backlog，表示连接等待队列的提示长度。它不是“最多只能连接 16 个客户端”，也不是并发客户端数量。真正能同时处理多少客户端，取决于程序是否使用线程、进程或 I/O 多路复用。

## 八、成功提示和当前程序的实际行为

```c
printf("listening on port 8080\n");
```

这只表示 `listen()` 调用成功，服务端已经进入监听状态。

但是下一段代码是注释：

```c
/* 后续在这里调用 accept()，接收客户端连接 */
```

注释不会执行。程序会继续执行：

```c
closesocket(listen_sock);
WSACleanup();
return 0;
```

因此当前程序的行为是：

```text
启动
  -> 初始化 Winsock
  -> 创建 Socket
  -> 绑定 8080 端口
  -> 开始监听
  -> 打印 listening
  -> 立即关闭 Socket
  -> 退出
```

这也是为什么客户端可能看到“连接被拒绝”：服务端已经退出，`8080` 端口不再监听。

## 九、`accept()` 应该放在哪里

要真正接收客户端，至少需要添加：

```c
SOCKET client_sock = accept(listen_sock, NULL, NULL);
if (client_sock == INVALID_SOCKET) {
    printf("accept failed: %d\n", WSAGetLastError());
    closesocket(listen_sock);
    WSACleanup();
    return 1;
}

printf("client connected\n");

const char message[] = "hello from server\n";
send(client_sock, message, (int)strlen(message), 0);

closesocket(client_sock);
closesocket(listen_sock);
WSACleanup();
```

要使用 `strlen()`，还需要添加：

```c
#include <string.h>
```

`accept()` 的作用是从监听 Socket 中取出一个客户端连接，并返回一个新的 Socket：

| Socket | 用途 |
| --- | --- |
| `listen_sock` | 继续等待其他客户端 |
| `client_sock` | 与当前客户端 `send()`、`recv()` |

不能使用 `listen_sock` 直接收发客户端业务数据。监听 Socket 的职责是接收新连接。

默认情况下，如果暂时没有客户端，`accept()` 会阻塞等待。这是正常行为，不是程序无响应。此时在另一个终端运行客户端即可唤醒它。

## 十、一个更完整的 Winsock 服务端主体

下面的示例在原代码基础上加入了 `accept()` 和一次收发数据：

```c
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

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

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

    printf("listening on 127.0.0.1:8080\n");

    SOCKET client_sock = accept(listen_sock, NULL, NULL);
    if (client_sock == INVALID_SOCKET) {
        printf("accept failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    const char response[] = "hello from server\n";
    int response_len = (int)strlen(response);
    send(client_sock, response, response_len, 0);

    closesocket(client_sock);
    closesocket(listen_sock);
    WSACleanup();
    return 0;
}
```

这个版本只处理一个客户端。客户端连接成功后，服务端发送一次消息，然后关闭客户端 Socket 并退出。

## 十一、Linux 版本与当前代码的区别

如果目标是 Linux，不能直接编译当前文件。主要替换如下：

| Windows Winsock | Linux/POSIX |
| --- | --- |
| `#include <winsock2.h>` | `#include <sys/socket.h>` 等头文件 |
| `WSAStartup()` | 通常不需要 |
| `SOCKET` | `int` 文件描述符 |
| `INVALID_SOCKET` | `-1` |
| `SOCKET_ERROR` | `-1` |
| `WSAGetLastError()` | `errno` / `perror()` |
| `closesocket()` | `close()` |
| `WSACleanup()` | 通常不需要 |

Linux 服务端的核心形式是：

```c
int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
bind(listen_fd, ...);
listen(listen_fd, 16);

int client_fd = accept(listen_fd, ...);
recv(client_fd, ...);
send(client_fd, ...);

close(client_fd);
close(listen_fd);
```

## 十二、如何测试

### Windows 下使用 PowerShell

先运行服务端。看到：

```text
listening on 127.0.0.1:8080
```

然后在另一个终端测试端口：

```powershell
Test-NetConnection 127.0.0.1 -Port 8080
```

如果使用上面的完整版本，客户端连接成功后服务端会发送 `hello from server`。

也可以查看端口是否正在监听：

```powershell
Get-NetTCPConnection -LocalPort 8080
```

### Linux 下使用 `nc`

如果是 Linux 服务器，运行：

```bash
nc -l 8080
```

命令没有输出并不代表失败。它通常是在阻塞等待客户端连接。另开一个终端运行：

```bash
nc 127.0.0.1 8080
```

然后在客户端终端输入文字并按回车，监听端终端就能看到数据。

## 十三、总结

当前 `server.c` 做了这些事情：

1. 初始化 Windows Winsock；
2. 创建 IPv4 TCP Socket；
3. 准备 `0.0.0.0:8080` 地址；
4. 将 Socket 绑定到本机 `8080` 端口；
5. 进入监听状态；
6. 打印提示后立即关闭并退出。

它还缺少 TCP 服务端真正的核心部分：

```text
accept() -> recv()/send() -> closesocket(client_sock)
```

另外，实际项目还需要处理 `send()` 的部分发送、`recv()` 返回 `0`、客户端断开、多个客户端并发、超时和消息边界等问题。
