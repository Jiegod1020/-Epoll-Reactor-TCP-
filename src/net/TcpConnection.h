#pragma once
#include <memory>
#include <string>
#include "reactor/EventLoop.h"
#include "reactor/Channel.h"
#include "Buffer.h"
#include "util/noncopyable.h"

namespace coreactor {

// TcpConnection 封装一个已建立的非阻塞 TCP 连接。
// Channel 负责事件通知，Buffer 负责字节存储，协程负责把异步等待包装成
// coRead/coWrite 这样的同步风格接口。
class TcpConnection : noncopyable,
                      public std::enable_shared_from_this<TcpConnection> {
public:
    using ptr = std::shared_ptr<TcpConnection>;
    // Buffer 是本次 coRead 得到的数据，不等同于内部 inputBuffer_。
    using MessageCallback = std::function<void(const ptr&, Buffer*)>;
    using CloseCallback = std::function<void(const ptr&)>;

    TcpConnection(EventLoop* loop, int sockfd);
    ~TcpConnection();

    // 注册读事件并创建连接业务协程。
    void start();
    // 关闭连接并通知 TcpServer 回收它。
    void shutdown();

    // 没有数据时只挂起当前协程，不阻塞整个 I/O 线程。
    std::string coRead();
    bool coWrite(const std::string& data);

    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }

    EventLoop* getLoop() { return loop_; }
    int fd() const { return sockfd_; }

private:
    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();

    // 协程入口：处理连接生命周期
    void processInCo();

private:
    EventLoop* loop_;       // 连接所属的从 Reactor
    int sockfd_;            // 客户端 socket
    Channel channel_;       // socket 的 epoll 事件代理

    Buffer inputBuffer_;    // 内核读到但业务尚未消费的数据
    Buffer outputBuffer_;   // 尚未完全发送到内核的数据

    bool connected_;        // 连接是否仍可进行业务处理
    MessageCallback messageCallback_;
    CloseCallback closeCallback_;
};

} // namespace coreactor
