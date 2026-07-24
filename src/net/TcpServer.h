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

// 主从Reactor TCP服务器
class TcpServer : noncopyable {
public:
    using MessageCallback = TcpConnection::MessageCallback;
    using ConnectionCallback = std::function<void(const TcpConnection::ptr&)>;

    TcpServer(uint16_t port, int subLoopNum = 4);
    ~TcpServer();

    // 启动服务器
    void start();

    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setConnectionCallback(ConnectionCallback cb) { connCallback_ = std::move(cb); }

private:
    // 新连接回调
    void onNewConnection(int sockfd);
    // 连接关闭回调
    void onCloseConnection(const TcpConnection::ptr& conn);

private:
    std::unique_ptr<EventLoop> mainLoop_;   // 主Reactor：只负责accept
    std::vector<std::unique_ptr<EventLoop>> subLoops_; // 从Reactor池：处理连接读写
    std::vector<std::thread> subThreads_;

    std::unique_ptr<Acceptor> acceptor_;

    std::unordered_map<int, TcpConnection::ptr> connections_;
    std::atomic<int> nextLoopIdx_;  // 轮询分发索引
    int subLoopNum_;

    MessageCallback messageCallback_;
    ConnectionCallback connCallback_;
};

} // namespace coreactor
