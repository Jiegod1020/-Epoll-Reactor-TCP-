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
    // accept 成功后把新客户端 fd 交给 TcpServer；Acceptor 不保存客户端连接。
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
    EventLoop* loop_;                    // 主 Reactor，不拥有它
    int listenFd_;                       // 监听 socket，Acceptor 负责关闭
    Channel acceptChannel_;              // 监听 fd 对应的 epoll 事件代理
    NewConnectionCallback newConnCallback_; // 新客户端交接函数
    bool listening_;                     // 防止重复 listen 或重复 stop
};

} // namespace coreactor
