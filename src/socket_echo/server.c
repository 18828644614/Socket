#include <winsock2.h>
#include <stdio.h>
#include <string.h>

static int send_all(SOCKET sock, const char *data, int length) {
    int sent = 0;
    while (sent < length) {
        int n = send(sock, data + sent, length - sent, 0);
        if (n > 0) sent += n;
        else return -1;
    }
    return 0;
}

static int recv_line(SOCKET sock, char *buffer, int capacity) {
    int used = 0;
    while (used + 1 < capacity) {
        char ch;
        int n = recv(sock, &ch, 1, 0);
        if (n > 0) {
            buffer[used++] = ch;
            if (ch == '\n') { buffer[used] = '\0'; return used; }
        } else if (n == 0) {
            buffer[used] = '\0';
            return used == 0 ? 0 : used;
        } else return -1;
    }
    buffer[used] = '\0';
    return -2;
}

int main(void) {
    WSADATA wsa_data;
    int startup = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (startup != 0) {
        printf("WSAStartup failed: %d\n", startup); return 1;
    }

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        WSACleanup(); return 1;
    }

    BOOL reuse = TRUE;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&reuse, sizeof reuse);

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listen_sock, (const struct sockaddr *)&address,
             sizeof address) == SOCKET_ERROR) {
        printf("bind failed: %d\n", WSAGetLastError());
        closesocket(listen_sock); WSACleanup(); return 1;
    }
    if (listen(listen_sock, 16) == SOCKET_ERROR) {
        printf("listen failed: %d\n", WSAGetLastError());
        closesocket(listen_sock); WSACleanup(); return 1;
    }

    printf("listening on 127.0.0.1:8080\n");
    for (;;) {
        SOCKET client_sock = accept(listen_sock, NULL, NULL);
        if (client_sock == INVALID_SOCKET) {
            printf("accept failed: %d\n", WSAGetLastError()); break;
        }

        char buffer[1024];
        int result = recv_line(client_sock, buffer, (int)sizeof buffer);
        if (result > 0) {
            printf("received %d bytes: %s", result, buffer);
            if (send_all(client_sock, buffer, result) < 0)
                printf("send failed: %d\n", WSAGetLastError());
        } else if (result == 0) {
            printf("client closed without sending a line\n");
        } else if (result == -2) {
            printf("line is too long (limit: 1023 bytes)\n");
        } else {
            printf("recv failed: %d\n", WSAGetLastError());
        }
        closesocket(client_sock);
    }

    closesocket(listen_sock);
    WSACleanup();
    return 0;
}