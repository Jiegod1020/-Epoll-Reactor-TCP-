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

    // createListenFd 失败会返回 -1；构造失败时不允许留下一个不可用的 Acceptor。
    if (listenFd_ < 0) {
        // 让构造失败尽早暴露，避免把无效 fd 注册进 epoll。
        throw std::runtime_error("failed to create listening socket");
    }

    acceptChannel_.setReadCallback([this]() { handleRead(); });
}

Acceptor::~Acceptor() {
    // 先从 epoll 注销，再 close 文件描述符，避免 epoll 持有已关闭 fd 的事件。
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
        // accept 的地址参数用于取得客户端 IP 和端口；长度必须先初始化为结构体大小。
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int connFd = accept(listenFd_, (sockaddr*)&clientAddr, &len);

        if (connFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // 没有新连接了
            }
            // 其他错误可能是暂时故障；记录后结束本轮，避免错误情况下死循环。
            LOG_ERROR("accept failed");
            break;
        }

        // 客户端 fd 也必须非阻塞，否则后续 read/write 可能阻塞整个 Reactor 线程。
        // listen fd 是非阻塞的，但 accept 出来的 fd 仍需显式设为非阻塞。
        setNonBlock(connFd);

        if (newConnCallback_) {
            newConnCallback_(connFd);
        } else {
            close(connFd);
        }
    }

    // EPOLLONESHOT 每次触发后都会停用该 fd。连接队列读空后重新 arm，
    // 否则服务器只能处理第一次 accept 事件。
    if (listening_) {
        acceptChannel_.enableReading();
    }
}

} // namespace coreactor
