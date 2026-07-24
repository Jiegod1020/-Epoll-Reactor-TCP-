#include "EventLoop.h"
#include "util/Logger.h"
#include "util/SocketUtils.h"
#include <unistd.h>
#include <cassert>

namespace coreactor {

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false) {

    epollFd_ = epoll_create1(0);
    if (epollFd_ < 0) {
        LOG_ERROR("epoll_create1 failed");
        abort();
    }

    wakeupFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeupFd_ < 0) {
        LOG_ERROR("eventfd create failed");
        abort();
    }

    events_.resize(kMaxEvents);

    wakeupChannel_ = std::make_unique<Channel>(this, wakeupFd_);
    wakeupChannel_->setReadCallback([this]() { handleWakeup(); });
    wakeupChannel_->enableReading();

    scheduler_ = std::make_unique<Scheduler>(this);
}

EventLoop::~EventLoop() {
    close(wakeupFd_);
    close(epollFd_);
}

void EventLoop::loop() {
    assert(!looping_);
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop started");

    while (!quit_) {
        events_.clear();
        int numEvents = epoll_wait(epollFd_, events_.data(), kMaxEvents, -1);

        if (numEvents < 0 && errno != EINTR) {
            LOG_ERROR("epoll_wait failed");
            break;
        }

        // 处理所有就绪IO事件
        for (int i = 0; i < numEvents; ++i) {
            Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
            channel->setRevents(events_[i].events);
            channel->handleEvent();
        }

        // 执行跨线程投递的任务
        doPendingFunctors();

        // 调度就绪协程
        scheduler_->start();
    }

    looping_ = false;
    LOG_INFO("EventLoop stopped");
}

void EventLoop::quit() {
    quit_ = true;
    wakeup();
}

void EventLoop::updateChannel(Channel* channel) {
    epoll_event ev{};
    ev.data.ptr = channel;
    ev.events = channel->events();

    int fd = channel->fd();
    if (epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) == 0) {
        return;
    }

    // 修改失败则添加
    if (errno == ENOENT) {
        if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            LOG_ERROR("epoll_ctl add fd=" << fd << " failed");
        }
    } else {
        LOG_ERROR("epoll_ctl mod fd=" << fd << " failed");
    }
}

void EventLoop::removeChannel(Channel* channel) {
    int fd = channel->fd();
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
}

void EventLoop::runInLoop(Functor cb) {
    // 如果在当前线程，直接执行
    cb(); // 简化版，生产环境需判断线程ID
    // 跨线程场景调用queueInLoop
}

void EventLoop::queueInLoop(Functor cb) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }
    wakeup();
}

void EventLoop::wakeup() {
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one)) {
        LOG_ERROR("wakeup write failed");
    }
}

void EventLoop::handleWakeup() {
    uint64_t one;
    ssize_t n = read(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one)) {
        LOG_ERROR("handleWakeup read failed");
    }
}

void EventLoop::doPendingFunctors() {
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for (auto& func : functors) {
        func();
    }

    callingPendingFunctors_ = false;
}

} // namespace coreactor
