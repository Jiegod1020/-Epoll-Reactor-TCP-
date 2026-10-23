#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include "Acceptor.h"
#include "TcpConnection.h"
#include "EventLoop.h"
#include "util/noncopyable.h"

namespace coreactor {

// 主从 Reactor TCP 服务器。
//
// mainLoop_ 只监听 listen fd 并 accept 新连接；subLoops_ 组成从 Reactor 池，
// 每个从 Reactor 绑定一个线程，负责所属客户端连接的 I/O 和业务回调。
class TcpServer : noncopyable {
public:
    // 消息回调收到已解码的 DATA 载荷，运行在连接所属的从 Reactor 线程中。
    using MessageCallback = TcpConnection::MessageCallback;
    // 新连接创建后调用；参数是共享管理生命周期的连接对象。
    using ConnectionCallback = std::function<void(const TcpConnection::ptr&)>;

    TcpServer(uint16_t port, int subLoopNum = 4);
    ~TcpServer();

    // 启动所有从 Reactor，然后在当前线程运行主 Reactor。
    void start();
    // 停止监听、唤醒所有 EventLoop，并等待从 Reactor 线程退出。
    void stop();

    // 保存业务回调，后续每个新连接都会复制这两个回调。
    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setConnectionCallback(ConnectionCallback cb) { connCallback_ = std::move(cb); }
    void setHeartbeatTimeouts(std::int64_t idleIntervalMs,
                              std::int64_t ackTimeoutMs);

private:
    // 新连接回调
    void onNewConnection(int sockfd);
    // 连接关闭回调
    void onCloseConnection(const TcpConnection::ptr& conn);
    void onHeartbeatTick();
    void heartbeatLoop();

private:
    std::unique_ptr<EventLoop> mainLoop_;   // 主线程运行的事件循环
    std::vector<std::unique_ptr<EventLoop>> subLoops_; // 每个从线程一个事件循环
    std::vector<std::thread> subThreads_;   // 保存线程对象，以便 stop 时 join
    std::thread heartbeatThread_;           // 周期性触发心跳检查

    std::unique_ptr<Acceptor> acceptor_;

    std::unordered_map<int, TcpConnection::ptr> connections_; // 主线程维护，持有活动连接
    std::atomic<int> nextLoopIdx_;  // 连接分发轮询计数器
    int subLoopNum_;                // 从 Reactor 数量

    MessageCallback messageCallback_;
    ConnectionCallback connCallback_;
    std::atomic<bool> started_;      // 防止重复 start/stop，并允许其他线程请求停止
    std::mutex heartbeatMutex_;
    std::condition_variable heartbeatCv_;
    bool stopHeartbeat_;
    std::int64_t heartbeatIntervalMs_;
    std::int64_t heartbeatTimeoutMs_;
};

} // namespace coreactor
