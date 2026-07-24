#pragma once
#include <functional>
#include "Channel.h"
#include "EventLoop.h"
#include "util/noncopyable.h"

namespace coreactor {

// Acceptor：负责监听端口，接受新连接
class Acceptor : noncopyable {
public:
    using NewConnectionCallback = std::function<void(int sockfd)>;

    Acceptor(EventLoop* loop, uint16_t port);
    ~Acceptor();

    void setNewConnectionCallback(NewConnectionCallback cb) {
        newConnCallback_ = std::move(cb);
    }

    void listen();

private:
    void handleRead();

private:
    EventLoop* loop_;
    int listenFd_;
    Channel acceptChannel_;
    NewConnectionCallback newConnCallback_;
    bool listening_;
};

} // namespace coreactor
