#include "Acceptor.h"
#include "util/SocketUtils.h"
#include "util/Logger.h"
#include <unistd.h>
#include <stdexcept>

namespace coreactor {

Acceptor::Acceptor(EventLoop* loop, uint16_t port)
    : loop_(loop)
    , listenFd_(createListenFd(port))
    , acceptChannel_(loop, listenFd_)
    , listening_(false) {

    if (listenFd_ < 0) {
        // 让构造失败尽早暴露，避免把无效 fd 注册进 epoll。
        throw std::runtime_error("failed to create listening socket");
    }

    acceptChannel_.setReadCallback([this]() { handleRead(); });
}

Acceptor::~Acceptor() {
    stop();
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }
}

void Acceptor::listen() {
    if (listening_) {
        return;
    }
    listening_ = true;
    // enableReading 内部会把监听 socket 注册为 EPOLLIN|EPOLLET|EPOLLONESHOT。
    acceptChannel_.enableReading();
}

void Acceptor::stop() {
    if (!listening_) {
        return;
    }
    listening_ = false;
    // 关闭监听只需要从 epoll 移除，真正 close(listenFd_) 放在析构函数中。
    acceptChannel_.disableAll();
}

void Acceptor::handleRead() {
    // ET 模式只在“不可接受”变为“可接受”时通知一次，因此必须一直 accept，
    // 直到 EAGAIN；只 accept 一次会把尚未处理的连接留在内核队列里且不再通知。
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

        // 客户端 fd 也必须非阻塞，否则后续 read/write 可能阻塞整个 Reactor 线程。
        setNonBlock(connFd);

        if (newConnCallback_) {
            newConnCallback_(connFd);
        } else {
            close(connFd);
        }
    }
}

} // namespace coreactor
