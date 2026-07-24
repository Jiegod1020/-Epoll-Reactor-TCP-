#pragma once
#include <memory>
#include <string>
#include "reactor/EventLoop.h"
#include "reactor/Channel.h"
#include "Buffer.h"
#include "util/noncopyable.h"

namespace coreactor {

class TcpConnection : noncopyable,
                      public std::enable_shared_from_this<TcpConnection> {
public:
    using ptr = std::shared_ptr<TcpConnection>;
    using MessageCallback = std::function<void(const ptr&, Buffer*)>;
    using CloseCallback = std::function<void(const ptr&)>;

    TcpConnection(EventLoop* loop, int sockfd);
    ~TcpConnection();

    // 启动连接处理（开启协程）
    void start();
    // 关闭连接
    void shutdown();

    // 协程化接口：同步风格读写
    std::string coRead();
    bool coWrite(const std::string& data);

    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }

    EventLoop* getLoop() { return loop_; }

private:
    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();

    // 协程入口：处理连接生命周期
    void processInCo();

private:
    EventLoop* loop_;
    int sockfd_;
    Channel channel_;

    Buffer inputBuffer_;
    Buffer outputBuffer_;

    bool connected_;
    MessageCallback messageCallback_;
    CloseCallback closeCallback_;
};

} // namespace coreactor
