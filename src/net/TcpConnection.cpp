#include "TcpConnection.h"
#include "coroutine/Scheduler.h"
#include "util/Logger.h"
#include <unistd.h>
#include <cassert>

namespace coreactor {

TcpConnection::TcpConnection(EventLoop* loop, int sockfd)
    : loop_(loop)
    , sockfd_(sockfd)
    , channel_(loop, sockfd)
    , connected_(true) {

    channel_.setReadCallback([this]() { handleRead(); });
    channel_.setWriteCallback([this]() { handleWrite(); });
    channel_.setCloseCallback([this]() { handleClose(); });
    channel_.setErrorCallback([this]() { handleError(); });
}

TcpConnection::~TcpConnection() {
    close(sockfd_);
}

void TcpConnection::start() {
    channel_.enableReading();
    // 启动协程处理业务
    loop_->getScheduler()->createCoroutine(
        [this]() { processInCo(); }
    );
}

void TcpConnection::shutdown() {
    if (connected_) {
        connected_ = false;
        channel_.disableAll();
        if (closeCallback_) {
            closeCallback_(shared_from_this());
        }
    }
}

std::string TcpConnection::coRead() {
    // 有数据直接返回
    if (inputBuffer_.readableBytes() > 0) {
        return inputBuffer_.retrieveAllAsString();
    }

    // 无数据则挂起协程，等待IO就绪
    auto co = Coroutine::GetCurrent();
    loop_->getScheduler()->registerFdCo(sockfd_, co->shared_from_this());

    // 重新注册读事件
    channel_.enableReading();

    // 让出CPU
    Scheduler::Yield();

    // 被唤醒后返回数据
    return inputBuffer_.retrieveAllAsString();
}

bool TcpConnection::coWrite(const std::string& data) {
    outputBuffer_.append(data);

    // 尝试直接写
    ssize_t n = write(sockfd_, outputBuffer_.peek(), outputBuffer_.readableBytes());
    if (n > 0) {
        outputBuffer_.retrieve(n);
        if (outputBuffer_.readableBytes() == 0) {
            channel_.disableWriting();
            return true;
        }
    } else if (n < 0 && errno != EAGAIN) {
        LOG_ERROR("write error");
        return false;
    }

    // 没写完，挂起等待可写
    auto co = Coroutine::GetCurrent();
    loop_->getScheduler()->registerFdCo(sockfd_, co->shared_from_this());
    channel_.enableWriting();

    Scheduler::Yield();

    return outputBuffer_.readableBytes() == 0;
}

void TcpConnection::handleRead() {
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(sockfd_, &savedErrno);

    if (n > 0) {
        // 唤醒等待读的协程
        loop_->getScheduler()->wakeupCoByFd(sockfd_);
    } else if (n == 0) {
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
        ssize_t n = write(sockfd_, outputBuffer_.peek(), outputBuffer_.readableBytes());
        if (n > 0) {
            outputBuffer_.retrieve(n);
            if (outputBuffer_.readableBytes() == 0) {
                channel_.disableWriting();
            }
        }
    }
    // 唤醒等待写的协程
    loop_->getScheduler()->wakeupCoByFd(sockfd_);
}

void TcpConnection::handleClose() {
    connected_ = false;
    channel_.disableAll();
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
    // 协程内同步处理业务，代码线性可读
    while (connected_) {
        std::string data = coRead();
        if (data.empty()) {
            break;
        }

        if (messageCallback_) {
            messageCallback_(shared_from_this(), &inputBuffer_);
        }
    }
    shutdown();
}

} // namespace coreactor
