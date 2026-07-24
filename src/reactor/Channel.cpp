#include "Channel.h"
#include "EventLoop.h"
#include "util/Logger.h"

namespace coreactor {

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , events_(0)
    , revents_(0) {}

Channel::~Channel() {
    disableAll();
    remove();
}

void Channel::handleEvent() {
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
        if (closeCallback_) closeCallback_();
        return;
    }

    if (revents_ & EPOLLERR) {
        if (errorCallback_) errorCallback_();
        return;
    }

    if (revents_ & (EPOLLIN | EPOLLPRI)) {
        if (readCallback_) readCallback_();
    }

    if (revents_ & EPOLLOUT) {
        if (writeCallback_) writeCallback_();
    }
}

void Channel::enableReading() {
    events_ |= EPOLLIN | EPOLLET | EPOLLONESHOT;
    update();
}

void Channel::enableWriting() {
    events_ |= EPOLLOUT | EPOLLET | EPOLLONESHOT;
    update();
}

void Channel::disableWriting() {
    events_ &= ~EPOLLOUT;
    update();
}

void Channel::disableAll() {
    events_ = 0;
    update();
}

void Channel::remove() {
    // 从epoll中移除
}

void Channel::update() {
    // 调用EventLoop更新epoll事件
    loop_->updateChannel(this);
}

} // namespace coreactor
