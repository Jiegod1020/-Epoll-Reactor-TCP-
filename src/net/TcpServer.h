#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>
#include "Acceptor.h"
#include "TcpConnection.h"
#include "EventLoop.h"
#include "util/noncopyable.h"

namespace coreactor {

// 主从 Reactor TCP 服务器。
//
// mainLoop_ 只监听 listen fd 并 accept 新连接；subLoops_ 组成从 Reactor 池，
// 每个从 Reactor 绑定一个线程，负责客户端连接的读写和协程调度。
class TcpServer : noncopyable {
public:
    // 回调运行在连接所属的从 Reactor 线程中。
    using MessageCallback = TcpConnection::MessageCallback;
    using ConnectionCallback = std::function<void(const TcpConnection::ptr&)>;

    TcpServer(uint16_t port, int subLoopNum = 4);
    ~TcpServer();

    // 启动所有从 Reactor，然后在当前线程运行主 Reactor。
    void start();
    // 停止监听、唤醒所有 EventLoop，并等待从 Reactor 线程退出。
    void stop();

    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setConnectionCallback(ConnectionCallback cb) { connCallback_ = std::move(cb); }

private:
    // 新连接回调
    void onNewConnection(int sockfd);
    // 连接关闭回调
    void onCloseConnection(const TcpConnection::ptr& conn);

private:
    std::unique_ptr<EventLoop> mainLoop_;   // 主 Reactor：只负责 accept
    std::vector<std::unique_ptr<EventLoop>> subLoops_; // 从 Reactor：处理连接读写
    std::vector<std::thread> subThreads_;   // 每个从 Reactor 一个线程

    std::unique_ptr<Acceptor> acceptor_;

    std::unordered_map<int, TcpConnection::ptr> connections_; // 保持连接对象存活
    std::atomic<int> nextLoopIdx_;  // 新连接的轮询分发索引
    int subLoopNum_;

    MessageCallback messageCallback_;
    ConnectionCallback connCallback_;
    std::atomic<bool> started_;      // 防止重复 start/stop
};

} // namespace coreactor
