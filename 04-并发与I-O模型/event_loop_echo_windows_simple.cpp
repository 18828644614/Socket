// 教学版：只保留 select 事件循环和最基本的行回显。
// Windows / C++17

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <cstring>

const int PORT = 19090;
const int MAX_CLIENTS = 32;
const int BUFFER_SIZE = 4096;

struct Client {
    SOCKET socket = INVALID_SOCKET;
    char input[BUFFER_SIZE]{};
    int inputLength = 0;
    char output[BUFFER_SIZE]{};
    int outputLength = 0;
    bool peerClosed = false;
};

bool setNonblocking(SOCKET socket) {
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
}

void closeClient(Client& client, int slot, const char* reason) {
    std::printf("slot %d closed: %s\n", slot, reason);
    closesocket(client.socket);
    client = Client{};
}

// 找到已经收到的完整行，把它移到 output，剩余半行留在 input。
void makeOutput(Client& client) {
    int completeLength = 0;

    for (int i = 0; i < client.inputLength; ++i) {
        if (client.input[i] == '\n') {
            completeLength = i + 1;
        }
    }

    if (completeLength == 0) {
        return; // 还没有收到完整的一行
    }

    std::memcpy(client.output, client.input, completeLength);
    client.outputLength = completeLength;

    client.inputLength -= completeLength;
    std::memmove(client.input,
                 client.input + completeLength,
                 client.inputLength);
}

int main() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::printf("WSAStartup failed\n");
        return 1;
    }

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        std::printf("socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    if (!setNonblocking(listener)) {
        std::printf("listener nonblocking failed: %d\n", WSAGetLastError());
        closesocket(listener);
        WSACleanup();
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listener, (sockaddr*)&address, sizeof(address)) != 0 ||
        listen(listener, SOMAXCONN) != 0) {
        std::printf("bind/listen failed: %d\n", WSAGetLastError());
        closesocket(listener);
        WSACleanup();
        return 1;
    }

    Client clients[MAX_CLIENTS]{};
    std::printf("listening on 127.0.0.1:%d\n", PORT);

    while (true) {
        fd_set readSet;
        fd_set writeSet;
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);

        // 监听 Socket 可读，表示可以 accept 新连接。
        FD_SET(listener, &readSet);

        for (int i = 0; i < MAX_CLIENTS; ++i) {
            Client& client = clients[i];
            if (client.socket == INVALID_SOCKET) {
                continue;
            }

            // 有输出时先暂停读取，避免输出缓冲区不断增长。
            if (!client.peerClosed && client.outputLength == 0) {
                FD_SET(client.socket, &readSet);
            }

            if (client.outputLength > 0) {
                FD_SET(client.socket, &writeSet);
            }
        }

        // 只等待网络事件；定时器和 Ctrl+C 留到完整版本再加入。
        int ready = select(0, &readSet, &writeSet, nullptr, nullptr);
        if (ready == SOCKET_ERROR) {
            std::printf("select failed: %d\n", WSAGetLastError());
            break;
        }

        // 先处理本轮开始前就存在的客户端。
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            Client& client = clients[i];
            if (client.socket == INVALID_SOCKET) {
                continue;
            }

            if (FD_ISSET(client.socket, &readSet)) {
                int room = BUFFER_SIZE - client.inputLength;
                int count = recv(client.socket,
                                 client.input + client.inputLength,
                                 room,
                                 0);

                if (count > 0) {
                    client.inputLength += count;
                    makeOutput(client);

                    if (client.inputLength == BUFFER_SIZE) {
                        closeClient(client, i, "line too long");
                        continue;
                    }
                } else if (count == 0) {
                    client.peerClosed = true;
                    client.inputLength = 0; // 没有换行的残留数据直接丢弃
                    if (client.outputLength == 0) {
                        closeClient(client, i, "peer closed");
                        continue;
                    }
                } else {
                    int error = WSAGetLastError();
                    if (error != WSAEWOULDBLOCK && error != WSAEINTR) {
                        closeClient(client, i, "recv failed");
                        continue;
                    }
                }
            }

            if (client.socket != INVALID_SOCKET &&
                FD_ISSET(client.socket, &writeSet)) {
                int count = send(client.socket,
                                 client.output,
                                 client.outputLength,
                                 0);

                if (count > 0) {
                    client.outputLength -= count;
                    std::memmove(client.output,
                                 client.output + count,
                                 client.outputLength);

                    if (client.peerClosed && client.outputLength == 0) {
                        closeClient(client, i, "response sent");
                    }
                } else if (count == 0) {
                    closeClient(client, i, "send made no progress");
                } else {
                    int error = WSAGetLastError();
                    if (error != WSAEWOULDBLOCK && error != WSAEINTR) {
                        closeClient(client, i, "send failed");
                    }
                }
            }
        }

        // 监听 Socket 就绪后，每轮只接受一个新连接。
        if (FD_ISSET(listener, &readSet)) {
            SOCKET clientSocket = accept(listener, nullptr, nullptr);
            if (clientSocket == INVALID_SOCKET) {
                int error = WSAGetLastError();
                if (error != WSAEWOULDBLOCK && error != WSAEINTR) {
                    std::printf("accept failed: %d\n", error);
                }
                continue;
            }

            int slot = -1;
            for (int i = 0; i < MAX_CLIENTS; ++i) {
                if (clients[i].socket == INVALID_SOCKET) {
                    slot = i;
                    break;
                }
            }

            if (slot == -1 || !setNonblocking(clientSocket)) {
                closesocket(clientSocket);
                std::printf("connection rejected\n");
                continue;
            }

            clients[slot].socket = clientSocket;
            std::printf("slot %d accepted\n", slot);
        }
    }

    for (Client& client : clients) {
        if (client.socket != INVALID_SOCKET) {
            closesocket(client.socket);
        }
    }
    closesocket(listener);
    WSACleanup();
    return 0;
}
