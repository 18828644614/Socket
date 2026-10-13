#include <winsock2.h>
#include <stdio.h>

int main(void) {
    WSADATA wsa;
    SOCKET listener = INVALID_SOCKET;
    SOCKET clients[2] = {INVALID_SOCKET, INVALID_SOCKET};
    const char *names[2] = {"A", "B"};
    int result = 1;
    int alive = 2;
    setvbuf(stdout, NULL, _IONBF, 0);

    int startup = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (startup != 0) {
        printf("WSAStartup failed: %d\n", startup);
        return 1;
    }
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        goto cleanup;
    }
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(19100);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (const struct sockaddr *)&address,
             sizeof address) == SOCKET_ERROR) {
        printf("bind failed: %d\n", WSAGetLastError());
        goto cleanup;
    }
    if (listen(listener, 2) == SOCKET_ERROR) {
        printf("listen failed: %d\n", WSAGetLastError());
        goto cleanup;
    }
    puts("Listening on 127.0.0.1:19100");

    /* 教学简化：先阻塞地接受两个连接，再一起观察它们。 */
    for (int i = 0; i < 2; ++i) {
        printf("Waiting for client %s...\n", names[i]);
        clients[i] = accept(listener, NULL, NULL);
        if (clients[i] == INVALID_SOCKET) {
            printf("accept failed: %d\n", WSAGetLastError());
            goto cleanup;
        }
        /* 第一步：真正开启这个连接的非阻塞模式。 */
        u_long enabled = 1;
        if (ioctlsocket(clients[i], FIONBIO, &enabled) == SOCKET_ERROR) {
            printf("ioctlsocket failed: %d\n", WSAGetLastError());
            goto cleanup;
        }
        printf("Client %s connected; nonblocking enabled\n", names[i]);
    }
    closesocket(listener);
    listener = INVALID_SOCKET;
    puts("Both connected. Now watch A and B together.");

    while (alive > 0) {
        /* 每轮重新放入所有尚未关闭的连接。 */
        fd_set reads;
        FD_ZERO(&reads);
        for (int i = 0; i < 2; ++i) {
            if (clients[i] != INVALID_SOCKET) FD_SET(clients[i], &reads);
        }
        struct timeval timeout = {2, 0};

        /* 第二步：等任意连接可读，最多两秒；Winsock 忽略参数 0。 */
        puts("select: waiting for any readable client...");
        int ready = select(0, &reads, NULL, NULL, &timeout);
        if (ready == SOCKET_ERROR) {
            printf("select failed: %d\n", WSAGetLastError());
            goto cleanup;
        }
        if (ready == 0) {
            printf("timeout: no readable client; keep %d remaining connection(s)\n", alive);
            continue;
        }

        for (int i = 0; i < 2; ++i) {
            if (clients[i] == INVALID_SOCKET) continue;
            /* 第三步：只对本轮就绪的连接尝试接收。 */
            if (!FD_ISSET(clients[i], &reads)) {
                printf("[%s] not readable: skip recv\n", names[i]);
                continue;
            }
            char buffer[128];
            int n = recv(clients[i], buffer, sizeof buffer, 0);
            if (n > 0) {
                printf("[%s] received %d bytes: ", names[i], n);
                fwrite(buffer, 1, (size_t)n, stdout);
                putchar('\n');
            } else if (n == 0) {
                printf("[%s] EOF: close this client\n", names[i]);
                closesocket(clients[i]);
                clients[i] = INVALID_SOCKET;
                --alive;
            } else {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    printf("[%s] temporarily unreadable: retry later\n", names[i]);
                    continue;
                }
                printf("[%s] recv failed: %d\n", names[i], err);
                goto cleanup;
            }
        }
    }
    result = 0;

cleanup:
    for (int i = 0; i < 2; ++i) {
        if (clients[i] != INVALID_SOCKET) closesocket(clients[i]);
    }
    if (listener != INVALID_SOCKET) closesocket(listener);
    WSACleanup();
    puts("server finished");
    return result;
}
