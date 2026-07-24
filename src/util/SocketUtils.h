#pragma once
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include "Logger.h"

namespace coreactor {

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
    int opt = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

inline int createListenFd(uint16_t port) {
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        LOG_ERROR("create socket failed");
        return -1;
    }

    setReuseAddr(listenFd);
    setNonBlock(listenFd);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listenFd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind port " << port << " failed");
        close(listenFd);
        return -1;
    }

    if (listen(listenFd, 1024) < 0) {
        LOG_ERROR("listen failed");
        close(listenFd);
        return -1;
    }

    LOG_INFO("Server listening on port " << port);
    return listenFd;
}

} // namespace coreactor
