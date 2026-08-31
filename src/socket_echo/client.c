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

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        WSACleanup(); return 1;
    }

    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(sock, (const struct sockaddr *)&server,
                sizeof server) == SOCKET_ERROR) {
        printf("connect failed: %d\n", WSAGetLastError());
        closesocket(sock); WSACleanup(); return 1;
    }

    char message[1024];
    printf("输入一行文字：");
    if (fgets(message, (int)sizeof message, stdin) == NULL) {
        closesocket(sock); WSACleanup(); return 0;
    }
    int length = (int)strlen(message);
    if (length == 0 || message[length - 1] != '\n') {
        if (length + 1 >= (int)sizeof message) {
            printf("输入太长\n"); closesocket(sock); WSACleanup(); return 1;
        }
        message[length++] = '\n'; message[length] = '\0';
    }
    if (send_all(sock, message, length) < 0) {
        printf("send failed: %d\n", WSAGetLastError());
        closesocket(sock); WSACleanup(); return 1;
    }

    char reply[1024];
    int result = recv_line(sock, reply, (int)sizeof reply);
    if (result > 0) printf("服务器回显：%s", reply);
    else if (result == 0) printf("服务器已关闭连接\n");
    else if (result == -2) printf("回显行太长\n");
    else printf("recv failed: %d\n", WSAGetLastError());

    closesocket(sock);
    WSACleanup();
    return result < 0 ? 1 : 0;
}