#include "TcpServer.h"
#include "util/Logger.h"
#include <chrono>
#include <thread>

namespace coreactor {

TcpServer::TcpServer(uint16_t port, int subLoopNum)
    : nextLoopIdx_(0)
    , subLoopNum_(subLoopNum)
    , started_(false)
    , stopHeartbeat_(false)
    , heartbeatIntervalMs_(30000)
    , heartbeatTimeoutMs_(10000) {

    // 分发连接时会对 subLoopNum_ 取模，因此至少需要一个从 Reactor。
    if (subLoopNum_ <= 0) {
        subLoopNum_ = 1;
    }

    // 主 EventLoop 负责监听 socket 的 I/O 事件；Acceptor 拥有监听 fd。
    // accept 成功后，Acceptor 的回调进入 onNewConnection 分发客户端 fd。
    mainLoop_ = std::make_unique<EventLoop>();
    acceptor_ = std::make_unique<Acceptor>(mainLoop_.get(), port);
    acceptor_->setNewConnectionCallback(
        [this](int fd) { onNewConnection(fd); }
    );

    // 先创建从 EventLoop 对象；线程在 start() 中启动后才调用各自的 loop()。
    for (int i = 0; i < subLoopNum_; ++i) {
        auto loop = std::make_unique<EventLoop>();
        subLoops_.push_back(std::move(loop));
    }
}

TcpServer::~TcpServer() {
    stop();
}

void TcpServer::stop() {
    // exchange 原子地把 started_ 设为 false；若此前已停止，就不重复清理。
    if (!started_.exchange(false)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(heartbeatMutex_);
        stopHeartbeat_ = true;
    }
    heartbeatCv_.notify_all();
    if (heartbeatThread_.joinable() &&
        heartbeatThread_.get_id() != std::this_thread::get_id()) {
        heartbeatThread_.join();
    }

    // 停止心跳线程并等待它退出，避免它在服务器销毁后继续投递任务。
    // 先停止 accept，再唤醒主、从 EventLoop 离开 epoll_wait。
    mainLoop_->queueInLoop([this]() { acceptor_->stop(); });
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
    // compare_exchange 确保 start 只成功执行一次。
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return;
    }
    // 一个从 Reactor 对应一个线程；该线程独占处理分配给它的连接状态。
    for (int i = 0; i < subLoopNum_; ++i) {
        subThreads_.emplace_back([this, i]() {
            subLoops_[i]->loop();
        });
    }

    {
        std::lock_guard<std::mutex> lock(heartbeatMutex_);
        stopHeartbeat_ = false;
    }
    heartbeatThread_ = std::thread([this]() { heartbeatLoop(); });

    acceptor_->listen();
    LOG_INFO("TcpServer started, sub reactor num: " << subLoopNum_);

    // 主线程开始 accept 事件循环；stop() 会通过 eventfd 唤醒它并令其返回。
    mainLoop_->loop();
}

void TcpServer::setHeartbeatTimeouts(std::int64_t idleIntervalMs,
                                     std::int64_t ackTimeoutMs) {
    // 配置只允许在服务器启动前修改，且两个时间都必须为正数。
    if (started_.load() || idleIntervalMs <= 0 || ackTimeoutMs <= 0) {
        return;
    }
    heartbeatIntervalMs_ = idleIntervalMs;
    heartbeatTimeoutMs_ = ackTimeoutMs;
}

void TcpServer::onNewConnection(int sockfd) {
    // 使用轮询策略在从 Reactor 之间均匀分配连接；这里只分配连接，
    // 真正的 socket 事件处理仍在目标从 Reactor 线程中完成。
    // 原子递增后取余，按顺序把新连接均匀发给从 Reactor。
    int idx = nextLoopIdx_++ % subLoopNum_;
    EventLoop* ioLoop = subLoops_[idx].get();

    // 创建连接对象并绑定目标 EventLoop。map 持有 shared_ptr，确保连接存活。
    auto conn = std::make_shared<TcpConnection>(ioLoop, sockfd);
    connections_[sockfd] = conn;

    conn->setMessageCallback(messageCallback_);
    conn->setCloseCallback(
        [this](const TcpConnection::ptr& c) { onCloseConnection(c); }
    );

    if (connCallback_) {
        connCallback_(conn);
    }

    // Channel 注册必须在所属线程完成，所以把 conn->start() 投递到目标 loop。
    ioLoop->queueInLoop([conn]() {
        conn->start();
    });
}

void TcpServer::onCloseConnection(const TcpConnection::ptr& conn) {
    // 关闭回调发生在从 Reactor 线程；connections_ 属于主 Reactor，
    // 必须把删除操作投递回主 Reactor，不能在这里直接访问 map。
    // 关闭回调可能来自从线程；主线程独占 connections_，因此通过队列删除。
    mainLoop_->queueInLoop([this, conn]() {
        auto it = connections_.find(conn->fd());
        // fd 可能已被系统复用；确认 map 中仍是同一个连接，避免误删新连接。
        if (it != connections_.end() && it->second == conn) {
            connections_.erase(it);
        }
    });
    LOG_INFO("connection closed, fd=" << conn->fd());
}

void TcpServer::heartbeatLoop() {
    // wait_for 让线程每秒醒一次；condition variable 可在 stop 时立刻打断等待。
    std::unique_lock<std::mutex> lock(heartbeatMutex_);
    while (!heartbeatCv_.wait_for(lock, std::chrono::seconds(1),
                                  [this]() { return stopHeartbeat_; })) {
        lock.unlock();
        // 心跳线程不直接读写连接表，定时任务回到主 Reactor 执行。
        mainLoop_->queueInLoop([this]() { onHeartbeatTick(); });
        lock.lock();
    }
}

void TcpServer::onHeartbeatTick() {
    // 使用 steady_clock 的单调时间，避免系统时间校准导致超时计算倒退。
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    // 先按从 EventLoop 分组，每个 loop 只投递一个任务，减少跨线程任务数量。
    std::unordered_map<EventLoop*, std::vector<TcpConnection::ptr>> byLoop;
    for (const auto& entry : connections_) {
        byLoop[entry.second->getLoop()].push_back(entry.second);
    }
    for (auto& entry : byLoop) {
        EventLoop* loop = entry.first;
        auto connections = std::move(entry.second);
        // 连接的状态检查必须在连接所属线程执行，保证与 I/O 回调串行。
        loop->queueInLoop([this, connections = std::move(connections), now]() {
            for (const auto& conn : connections) {
                conn->checkHeartbeat(now, heartbeatIntervalMs_, heartbeatTimeoutMs_);
            }
        });
    }
}

} // namespace coreactor
