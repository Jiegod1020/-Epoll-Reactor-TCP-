#pragma once
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <cerrno>
#include "Logger.h"

namespace coreactor {

// 把 fd 设置为非阻塞模式。Reactor 线程绝不能被单个客户端的 read/write 卡住。
inline int setNonBlock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        LOG_ERROR("fcntl F_GETFL failed");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_ERROR("fcntl F_SETFL nonblock failed");
        return -1;
    }
    return 0;
}

inline int setReuseAddr(int fd) {
    // 允许服务器重启后快速重新绑定刚刚释放的端口。
    int opt = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

inline int createListenFd(uint16_t port) {
    // 创建 IPv4 TCP 监听 socket，并完成 bind/listen 前的基础配置。
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        LOG_ERROR("create socket failed");
        return -1;
    }

    if (setReuseAddr(listenFd) < 0 || setNonBlock(listenFd) < 0) {
        LOG_ERROR("configure listen socket failed");
        close(listenFd);
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    // bind 把 socket 绑定到本机所有网卡的指定端口。
    if (bind(listenFd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind port " << port << " failed");
        close(listenFd);
        return -1;
    }

    // listen backlog 表示内核暂存尚未被 accept 的连接数量上限。
    if (listen(listenFd, 1024) < 0) {
        LOG_ERROR("listen failed");
        close(listenFd);
        return -1;
    }

    LOG_INFO("Server listening on port " << port);
    return listenFd;
}

} // namespace coreactor
