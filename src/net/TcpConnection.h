#pragma once
#include <memory>
#include <string>
#include "reactor/EventLoop.h"
#include "reactor/Channel.h"
#include "Buffer.h"
#include "Protocol.h"
#include "util/noncopyable.h"
#include <atomic>
#include <cstdint>

namespace coreactor {

// TcpConnection 封装一个已建立的非阻塞 TCP 连接；所有 I/O 和业务回调
// 都由所属 EventLoop 线程串行处理，不为每个连接创建协程。
class TcpConnection : noncopyable,
                      public std::enable_shared_from_this<TcpConnection> {
public:
    using ptr = std::shared_ptr<TcpConnection>;
    // Buffer 是本次完整 DATA 帧的载荷；它不等同于内部网络接收缓冲区。
    using MessageCallback = std::function<void(const ptr&, Buffer*)>;
    using CloseCallback = std::function<void(const ptr&)>;

    TcpConnection(EventLoop* loop, int sockfd);
    ~TcpConnection();

    // 注册连接读事件；后续读写都由 Channel 回调驱动。
    void start();
    // 关闭连接并通知 TcpServer 回收它。
    void shutdown();

    // 在所属 EventLoop 线程中调用；封装 DATA 帧并入队，由 EPOLLOUT 异步续发。
    bool sendData(const std::string& data);

    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }

    EventLoop* getLoop() { return loop_; }
    int fd() const { return sockfd_; }
    // 返回最近一次收到客户端字节的单调时钟时间（毫秒）。
    std::int64_t lastActivityMs() const { return lastActivityMs_.load(); }
    // 由服务器定时任务在连接所属线程调用，决定是否发送心跳或关闭超时连接。
    void checkHeartbeat(std::int64_t nowMs,
                        std::int64_t intervalMs,
                        std::int64_t timeoutMs);

private:
    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();
    void queueControlFrame(FrameType type);
    void processInputFrames();

private:
    EventLoop* loop_;       // 连接所属的从 Reactor
    int sockfd_;            // 客户端 socket
    Channel channel_;       // socket 的 epoll 事件代理

    Buffer inputBuffer_;    // readv 本轮读到的数据
    Buffer protocolBuffer_; // 保留跨多轮 read 的半帧，并等待完整帧
    Buffer outputBuffer_;   // send 尚未写入内核的帧字节

    bool connected_;                    // 本连接是否仍接受读写和业务处理
    std::atomic<std::int64_t> lastActivityMs_; // 定时线程可读取，故使用 atomic
    std::int64_t pingSentAtMs_;          // 最近一次发出心跳请求的时间
    bool awaitingHeartbeatAck_;          // 是否正在等待客户端回应心跳
    MessageCallback messageCallback_;    // DATA 帧解码成功后的业务回调
    CloseCallback closeCallback_;        // 连接关闭后通知 TcpServer 回收对象
};

} // namespace coreactor
