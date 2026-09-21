#include "TcpServer.h"
#include "util/Logger.h"
#include <thread>

namespace coreactor {

TcpServer::TcpServer(uint16_t port, int subLoopNum)
    : subLoopNum_(subLoopNum)
    , nextLoopIdx_(0)
    , started_(false) {

    // 至少保留一个从 Reactor，避免后续取模运算出现除零。
    if (subLoopNum_ <= 0) {
        subLoopNum_ = 1;
    }

    // 主 Reactor 和 Acceptor 绑定在一起；新连接最终通过回调进入
    // onNewConnection，再投递给某个从 Reactor。
    mainLoop_ = std::make_unique<EventLoop>();
    acceptor_ = std::make_unique<Acceptor>(mainLoop_.get(), port);
    acceptor_->setNewConnectionCallback(
        [this](int fd) { onNewConnection(fd); }
    );

    // 这里只创建 EventLoop，还没有启动线程；线程在 start() 中创建。
    for (int i = 0; i < subLoopNum_; ++i) {
        auto loop = std::make_unique<EventLoop>();
        subLoops_.push_back(std::move(loop));
    }
}

TcpServer::~TcpServer() {
    stop();
}

void TcpServer::stop() {
    if (!started_.exchange(false)) {
        return;
    }
    // 先停止接收新连接，再停止事件循环，避免 shutdown 过程中继续产生连接。
    acceptor_->stop();
    mainLoop_->quit();
    for (auto& loop : subLoops_) {
        loop->quit();
    }
    for (auto& t : subThreads_) {
        if (t.joinable() && t.get_id() != std::this_thread::get_id()) {
            t.join();
        }
    }
    subThreads_.clear();
}

void TcpServer::start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return;
    }
    // 每个从 Reactor 在自己的线程中执行 loop()，保证同一个连接的 Channel、
    // Buffer 和协程只由一个 I/O 线程访问。
    for (int i = 0; i < subLoopNum_; ++i) {
        subThreads_.emplace_back([this, i]() {
            subLoops_[i]->loop();
        });
    }

    acceptor_->listen();
    LOG_INFO("TcpServer started, sub reactor num: " << subLoopNum_);

    // start() 会阻塞在这里，直到 stop() 设置 quit_ 并通过 eventfd 唤醒它。
    mainLoop_->loop();
}

void TcpServer::onNewConnection(int sockfd) {
    // 使用轮询策略在从 Reactor 之间均匀分配连接；这里只分配连接，
    // 真正的 socket 事件处理仍在目标从 Reactor 线程中完成。
    int idx = nextLoopIdx_++ % subLoopNum_;
    EventLoop* ioLoop = subLoops_[idx].get();

    auto conn = std::make_shared<TcpConnection>(ioLoop, sockfd);
    // 主线程的 map 持有一个 shared_ptr，防止连接投递到从线程前就析构。
    connections_[sockfd] = conn;

    conn->setMessageCallback(messageCallback_);
    conn->setCloseCallback(
        [this](const TcpConnection::ptr& c) { onCloseConnection(c); }
    );

    if (connCallback_) {
        connCallback_(conn);
    }

    // queueInLoop 会把任务放入目标 EventLoop 的队列，并写 eventfd 唤醒它。
    ioLoop->queueInLoop([conn]() {
        conn->start();
    });
}

void TcpServer::onCloseConnection(const TcpConnection::ptr& conn) {
    // 关闭回调发生在从 Reactor 线程；connections_ 属于主 Reactor，
    // 必须把删除操作投递回主 Reactor，不能在这里直接访问 map。
    mainLoop_->queueInLoop([this, conn]() {
        connections_.erase(conn->fd());
    });
    LOG_INFO("connection closed, fd=" << conn->fd());
}

} // namespace coreactor
