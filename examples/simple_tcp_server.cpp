#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <string>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

void fail(const char* message) {
    std::cerr << message << ": " << std::strerror(errno) << '\n';
    std::exit(EXIT_FAILURE);
}

}  // namespace

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc >= 2) {
        port = std::stoi(argv[1]);
    }

    // 1. 创建监听 socket。AF_INET 表示 IPv4，SOCK_STREAM 表示 TCP。
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        fail("socket");
    }

    // 允许程序重启后快速重新绑定刚刚释放的端口。
    int reuse = 1;
    if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) < 0) {
        close(listenFd);
        fail("setsockopt");
    }

    // 2. 准备服务器地址：监听本机所有网卡的 port 端口。
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons(static_cast<uint16_t>(port));

    // 3. 将 socket 绑定到地址和端口。
    if (bind(listenFd, reinterpret_cast<sockaddr*>(&serverAddress),
             sizeof(serverAddress)) < 0) {
        close(listenFd);
        fail("bind");
    }

    // 4. 进入监听状态。128 是等待 accept 的连接队列大小。
    if (listen(listenFd, 128) < 0) {
        close(listenFd);
        fail("listen");
    }

    std::cout << "simple TCP server listening on 0.0.0.0:" << port << '\n';

    while (true) {
        // 5. accept 会阻塞，直到有客户端连接。
        sockaddr_in clientAddress{};
        socklen_t clientLength = sizeof(clientAddress);
        int clientFd = accept(listenFd,
                              reinterpret_cast<sockaddr*>(&clientAddress),
                              &clientLength);
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail("accept");
        }

        char clientIp[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &clientAddress.sin_addr,
                  clientIp, sizeof(clientIp));
        std::cout << "client connected: " << clientIp
                  << ':' << ntohs(clientAddress.sin_port) << '\n';

        // 当前是单线程示例：一个客户端断开后，才会 accept 下一个客户端。
        while (true) {
            char buffer[4096];

            // 6. read 从客户端 socket 读取字节。
            ssize_t received = read(clientFd, buffer, sizeof(buffer));
            if (received == 0) {
                std::cout << "client disconnected\n";
                break;
            }
            if (received < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::cerr << "read: " << std::strerror(errno) << '\n';
                break;
            }

            // 7. Echo：把收到的数据完整写回客户端。
            ssize_t sentTotal = 0;
            while (sentTotal < received) {
                ssize_t sent = write(clientFd, buffer + sentTotal,
                                     static_cast<size_t>(received - sentTotal));
                if (sent < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    std::cerr << "write: " << std::strerror(errno) << '\n';
                    break;
                }
                sentTotal += sent;
            }
        }

        // 8. 一个客户端处理完毕后关闭它的连接 socket。
        close(clientFd);
    }

    close(listenFd);
    return 0;
}
