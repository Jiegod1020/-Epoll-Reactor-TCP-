#include "TcpConnection.h"
#include "coroutine/Scheduler.h"
#include "util/Logger.h"
#include <unistd.h>
#include <cassert>
#include <cerrno>
#include <sys/socket.h>

namespace coreactor {

TcpConnection::TcpConnection(EventLoop* loop, int sockfd)
    : loop_(loop)
    , sockfd_(sockfd)
    , channel_(loop, sockfd)
    , connected_(true) {

    // Channel 只负责事件分发，具体读写动作由 TcpConnection 完成。
    channel_.setReadCallback([this]() { handleRead(); });
    channel_.setWriteCallback([this]() { handleWrite(); });
    channel_.setCloseCallback([this]() { handleClose(); });
    channel_.setErrorCallback([this]() { handleError(); });
}

TcpConnection::~TcpConnection() {
    // Channel 已在自身析构时从 epoll 移除；这里最后关闭底层 socket。
    close(sockfd_);
}

void TcpConnection::start() {
    channel_.enableReading();
    // 协程 lambda 捕获 shared_ptr，而不是裸 this，保证协程等待期间连接对象存活。
    auto self = shared_from_this();
    loop_->getScheduler()->createCoroutine(
        [self]() { self->processInCo(); }
    );
}

void TcpConnection::shutdown() {
    if (connected_) {
        connected_ = false;
        // 不再接收任何事件；closeCallback 会通知 TcpServer 从连接表删除它。
        channel_.disableAll();
        if (closeCallback_) {
            closeCallback_(shared_from_this());
        }
    }
}

std::string TcpConnection::coRead() {
    // 如果之前的 read 已经读到了数据，直接从用户态 Buffer 返回，避免再次进入内核。
    if (inputBuffer_.readableBytes() > 0) {
        return inputBuffer_.retrieveAllAsString();
    }

    // 当前没有数据：记录“fd -> 当前协程”的映射，等 handleRead 收到数据后唤醒它。
    auto co = Coroutine::GetCurrent();
    if (!co) {
        return {};
    }
    loop_->getScheduler()->registerFdCo(sockfd_, co->shared_from_this());

    // EPOLLONESHOT 事件被触发后已经失效，需要重新 arm 读事件。
    channel_.enableReading();

    // 只让出当前协程，线程仍然可以继续处理其他连接。
    Scheduler::Yield();

    if (!connected_) {
        return {};
    }

    // 可能是读事件唤醒，也可能是连接关闭唤醒；关闭时返回空字符串。
    return inputBuffer_.retrieveAllAsString();
}

bool TcpConnection::coWrite(const std::string& data) {
    outputBuffer_.append(data);

    // send 可能只发送一部分，因此循环直到发送完或需要等待 EPOLLOUT。
    while (connected_ && outputBuffer_.readableBytes() > 0) {
        ssize_t n = send(sockfd_, outputBuffer_.peek(),
                         outputBuffer_.readableBytes(), MSG_NOSIGNAL);
        if (n > 0) {
            outputBuffer_.retrieve(static_cast<size_t>(n));
            continue;
        }

        // 系统调用被信号中断时不代表 socket 出错，直接重试。
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("write error");
            handleError();
            return false;
        }

        // 内核发送缓冲区满：让当前协程等待可写事件。
        auto co = Coroutine::GetCurrent();
        if (!co) {
            return false;
        }
        loop_->getScheduler()->registerFdCo(sockfd_, co->shared_from_this());
        channel_.enableWriting();
        Scheduler::Yield();
    }

    return outputBuffer_.readableBytes() == 0;
}

void TcpConnection::handleRead() {
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(sockfd_, &savedErrno);

    if (n > 0) {
        // readFd 把数据放入 inputBuffer_；唤醒正在 coRead 中等待的协程。
        loop_->getScheduler()->wakeupCoByFd(sockfd_);
    } else if (n == 0) {
        // read 返回 0 表示对端执行了有序关闭（EOF）。
        handleClose();
    } else {
        errno = savedErrno;
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("TcpConnection read error");
            handleError();
        }
    }
}

void TcpConnection::handleWrite() {
    if (outputBuffer_.readableBytes() > 0) {
        ssize_t n = send(sockfd_, outputBuffer_.peek(),
                         outputBuffer_.readableBytes(), MSG_NOSIGNAL);
        if (n > 0) {
            outputBuffer_.retrieve(static_cast<size_t>(n));
            if (outputBuffer_.readableBytes() == 0) {
                channel_.disableWriting();
            }
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                   errno != EINTR) {
            handleError();
            return;
        }
    }
    // 唤醒等待写的协程；协程会继续检查 outputBuffer_ 是否已经清空。
    loop_->getScheduler()->wakeupCoByFd(sockfd_);
}

void TcpConnection::handleClose() {
    if (!connected_) {
        return;
    }
    connected_ = false;
    // 关闭事件也必须唤醒等待中的协程，否则它可能永久停在 coRead/coWrite。
    channel_.disableAll();
    loop_->getScheduler()->wakeupCoByFd(sockfd_);
    loop_->getScheduler()->unregisterFdCo(sockfd_);

    if (closeCallback_) {
        closeCallback_(shared_from_this());
    }
}

void TcpConnection::handleError() {
    LOG_ERROR("TcpConnection error");
    handleClose();
}

void TcpConnection::processInCo() {
    // 业务层看到的是线性代码，但每次 coRead/coWrite 内部都可能让出协程。
    while (connected_) {
        std::string data = coRead();
        if (data.empty()) {
            break;
        }

        if (messageCallback_) {
            // coRead 返回 string 后会清空内部 inputBuffer_，因此构造独立 Buffer
            // 交给回调，避免业务回调拿到空数据。
            Buffer message;
            message.append(data);
            messageCallback_(shared_from_this(), &message);
        }
    }
    shutdown();
}

} // namespace coreactor
