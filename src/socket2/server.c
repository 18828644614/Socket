#include <stdio.h>
#include <winsock2.h>
#include <winuser.h>

#pragma comment(lib, "ws2_32.lib")

static int send_all(SOCKET sock, const char *data, int length) {
  int sent = 0;

  while (sent < length) {
    int n = send(sock, data + sent, length - sent, 0);
    if (n > 0) {
      sent += n;
    } else if (n == 0) {
      printf("sent returned 0\n");
      return -1;
    } else {
      printf("sent failed: %d\n", WSAGetLastError());
      return -1;
    }
  }

  return 0;
}

// 为了便于学习，这里每次读取一个字节，直到读到换行符
static int recv_line(SOCKET sock, char *buffer, int capacity) {
  int used = 0;

  while (used + 1 < capacity) {
    char ch;
    int n = recv(sock, buffer, 1, 0);
    if (n > 0) {
      buffer[used++] = ch;
      if (ch == '\n') {
        break;
      }
    } else if (n == 0) {
      break;
    } else if (WSAGetLastError() == WSAEINTR) {
      continue;
    } else {
      printf("recv failed: %d\n", WSAGetLastError());
      return -1;
    }
  }
  buffer[used] = '\0';
  return used;
}

int main(void) {
  WSADATA wsa_data;
  int result = WSAStartup(MAKEWPARAM(2, 2), &wsa_data);
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

  BOOL reuse = TRUE;
  setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse,
             sizeof(reuse));

  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_port = htons(8080);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (bind(listen_sock, (const struct sockaddr *)&address, sizeof address) ==
      SOCKET_ERROR) {
    printf("bind failed: %d\n", WSAGetLastError());
    closesocket(listen_sock);
    WSACleanup();
    return 1;
  }

  if (listen(listen_sock, 10) == SOCKET_ERROR) {
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

  char buffer[1024];
  int n = recv_line(client_sock, buffer, (int)sizeof buffer);
  if (n > 0) {
    printf("received: %s", buffer);
    if (send_all(client_sock, buffer, n) < 0) {
      closesocket(client_sock);
      closesocket(client_sock);
      WSACleanup();
      return 1;
    }
  } else if (n == 0) {
    printf("client closed before sending a line\n");
  }

  shutdown(client_sock, SD_BOTH);
  closesocket(client_sock);
  closesocket(listen_sock);
  WSACleanup();
  return n < 0 ? 1 : 0;
}
