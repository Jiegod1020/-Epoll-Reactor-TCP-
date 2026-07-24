#pragma once
#include <functional>
#include <sys/epoll.h>
#include "util/noncopyable.h"

namespace coreactor {

class EventLoop;

// Channel：封装fd与对应事件回调
class Channel : noncopyable {
public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    // 处理就绪事件
    void handleEvent();

    // 设置事件回调
    void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 开启/关闭监听事件
    void enableReading();
    void enableWriting();
    void disableWriting();
    void disableAll();
    void remove();

    int fd() const { return fd_; }
    uint32_t events() const { return events_; }
    void setRevents(uint32_t rev) { revents_ = rev; }

private:
    void update();

private:
    EventLoop* loop_;
    int fd_;
    uint32_t events_;   // 监听的事件
    uint32_t revents_;  // 实际就绪的事件

    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};

} // namespace coreactor
