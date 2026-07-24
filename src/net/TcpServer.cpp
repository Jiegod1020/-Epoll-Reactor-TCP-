#include "TcpServer.h"
#include "util/Logger.h"
#include <thread>

namespace coreactor {

TcpServer::TcpServer(uint16_t port, int subLoopNum)
    : subLoopNum_(subLoopNum)
    , nextLoopIdx_(0) {

    mainLoop_ = std::make_unique<EventLoop>();
    acceptor_ = std::make_unique<Acceptor>(mainLoop_.get(), port);
    acceptor_->setNewConnectionCallback(
        [this](int fd) { onNewConnection(fd); }
    );

    // 启动从Reactor线程
    for (int i = 0; i < subLoopNum_; ++i) {
        auto loop = std::make_unique<EventLoop>();
        subLoops_.push_back(std::move(loop));
    }
}

TcpServer::~TcpServer() {
    for (auto& t : subThreads_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void TcpServer::start() {
    // 启动所有从Reactor线程
    for (int i = 0; i < subLoopNum_; ++i) {
        subThreads_.emplace_back([this, i]() {
            subLoops_[i]->loop();
        });
    }

    acceptor_->listen();
    LOG_INFO("TcpServer started, sub reactor num: " << subLoopNum_);

    // 主Reactor运行
    mainLoop_->loop();
}

void TcpServer::onNewConnection(int sockfd) {
    // 轮询分发到从Reactor
    int idx = nextLoopIdx_++ % subLoopNum_;
    EventLoop* ioLoop = subLoops_[idx].get();

    auto conn = std::make_shared<TcpConnection>(ioLoop, sockfd);
    connections_[sockfd] = conn;

    conn->setMessageCallback(messageCallback_);
    conn->setCloseCallback(
        [this](const TcpConnection::ptr& c) { onCloseConnection(c); }
    );

    if (connCallback_) {
        connCallback_(conn);
    }

    // 投递到从Reactor线程启动连接
    ioLoop->queueInLoop([conn]() {
        conn->start();
    });
}

void TcpServer::onCloseConnection(const TcpConnection::ptr& conn) {
    mainLoop_->queueInLoop([this, conn]() {
        connections_.erase(conn->getLoop() ? 0 : 0); // 简化，实际按fd删除
        // 完整实现需在对应loop中移除
    });
    LOG_INFO("connection closed, fd=" << conn->getLoop());
}

} // namespace coreactor
