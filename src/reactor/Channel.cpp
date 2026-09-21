#include "Channel.h"
#include "EventLoop.h"
#include "util/Logger.h"

namespace coreactor {

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , events_(0)
    , revents_(0)
    , added_(false) {}

Channel::~Channel() {
    // Channel 销毁前必须从 epoll 中移除，否则 epoll 仍可能返回一个已经
    // 失效的 data.ptr。真正的删除工作交给 EventLoop 统一完成。
    remove();
}

void Channel::handleEvent() {
    // 对于“挂起但不可读”的情况，连接通常已经被对端关闭。
    // 如果同时有 EPOLLIN，仍优先读，因为读操作可能读到最后一批数据。
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
        if (closeCallback_) closeCallback_();
        return;
    }

    // EPOLLERR 表示 fd 出错。错误处理通常会关闭连接，因此这里直接返回，
    // 避免同一轮事件继续触发读写回调。
    if (revents_ & EPOLLERR) {
        if (errorCallback_) errorCallback_();
        return;
    }

    // EPOLLPRI 用于带外数据；这里和普通可读事件走同一个回调。
    if (revents_ & (EPOLLIN | EPOLLPRI)) {
        if (readCallback_) readCallback_();
    }

    if (revents_ & EPOLLOUT) {
        if (writeCallback_) writeCallback_();
    }
}

void Channel::enableReading() {
    // ET（边缘触发）只在状态从“不可读”变成“可读”时通知一次；
    // ONESHOT 则要求每次处理完事件后显式重新 arm。
    events_ |= EPOLLIN | EPOLLET | EPOLLONESHOT;
    update();
}

void Channel::enableWriting() {
    // 只有发送缓冲区写不完时才监听 EPOLLOUT，避免 socket 一直可写造成
    // EventLoop 忙循环。
    events_ |= EPOLLOUT | EPOLLET | EPOLLONESHOT;
    update();
}

void Channel::disableWriting() {
    events_ &= ~EPOLLOUT;
    update();
}

void Channel::disableAll() {
    // events_ 变成 0 后，EventLoop 会执行 EPOLL_CTL_DEL。
    events_ = 0;
    update();
}

void Channel::remove() {
    if (added_) {
        loop_->removeChannel(this);
    }
}

void Channel::update() {
    // Channel 只维护状态，具体使用 EPOLL_CTL_ADD/MOD/DEL 由 EventLoop 判断。
    loop_->updateChannel(this);
}

} // namespace coreactor
