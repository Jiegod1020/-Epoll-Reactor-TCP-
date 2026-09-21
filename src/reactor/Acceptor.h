#pragma once
#include <functional>
#include "Channel.h"
#include "EventLoop.h"
#include "util/noncopyable.h"

namespace coreactor {

// Acceptor 只负责监听 socket 和 accept 新连接，不负责处理客户端读写。
// 这是主 Reactor 的入口：新连接建立后通过回调交给 TcpServer 分发到从 Reactor。
class Acceptor : noncopyable {
public:
    using NewConnectionCallback = std::function<void(int sockfd)>;

    Acceptor(EventLoop* loop, uint16_t port);
    ~Acceptor();

    void setNewConnectionCallback(NewConnectionCallback cb) {
        newConnCallback_ = std::move(cb);
    }

    void listen();  // 将监听 fd 加入 epoll
    void stop();    // 从 epoll 移除监听 fd，但不销毁 EventLoop

private:
    void handleRead();

private:
    EventLoop* loop_;
    int listenFd_;
    Channel acceptChannel_;
    NewConnectionCallback newConnCallback_;
    bool listening_;  // 防止重复 listen 或重复 stop
};

} // namespace coreactor
