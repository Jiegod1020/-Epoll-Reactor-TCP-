#include "Acceptor.h"
#include "util/SocketUtils.h"
#include "util/Logger.h"
#include <unistd.h>

namespace coreactor {

Acceptor::Acceptor(EventLoop* loop, uint16_t port)
    : loop_(loop)
    , listenFd_(createListenFd(port))
    , acceptChannel_(loop, listenFd_)
    , listening_(false) {

    acceptChannel_.setReadCallback([this]() { handleRead(); });
}

Acceptor::~Acceptor() {
    acceptChannel_.disableAll();
    close(listenFd_);
}

void Acceptor::listen() {
    listening_ = true;
    acceptChannel_.enableReading();
}

void Acceptor::handleRead() {
    // ET模式下循环accept直到EAGAIN
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int connFd = accept(listenFd_, (sockaddr*)&clientAddr, &len);

        if (connFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // 没有新连接了
            }
            LOG_ERROR("accept failed");
            break;
        }

        setNonBlock(connFd);

        if (newConnCallback_) {
            newConnCallback_(connFd);
        } else {
            close(connFd);
        }
    }
}

} // namespace coreactor
