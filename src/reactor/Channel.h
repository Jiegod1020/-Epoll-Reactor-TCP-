#pragma once
#include <functional>
#include <sys/epoll.h>
#include "util/noncopyable.h"

namespace coreactor {

class EventLoop;

// Channel 是“一个 fd 在某个 EventLoop 中的事件代理”。
//
// epoll 只负责告诉我们“哪个 fd 就绪了”，并不关心这个 fd 属于监听 socket、
// 客户端连接还是 eventfd。Channel 把 fd 和读/写/关闭/错误回调绑定起来，
// 这样 EventLoop 不需要知道具体业务，只负责找到 Channel 并转发事件。
class Channel : noncopyable {
public:
    // 所有事件回调都不需要参数，因为回调通常通过 lambda 捕获所属对象。
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    // 根据 epoll 返回的 revents_ 分发事件。
    void handleEvent();

    // 设置四类回调。回调的真正执行发生在 EventLoop 所在线程。
    void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 修改“希望监听”的事件集合，然后同步到所属 EventLoop 的 epoll 实例。
    void enableReading();
    void enableWriting();
    void disableWriting();
    void disableAll();
    void remove();

    int fd() const { return fd_; }
    uint32_t events() const { return events_; }
    void setRevents(uint32_t rev) { revents_ = rev; }
    bool added() const { return added_; }

private:
    friend class EventLoop;

    void update();
    void setAdded(bool added) { added_ = added; }

private:
    EventLoop* loop_;
    int fd_;
    uint32_t events_;   // 我们希望 epoll 监听的事件
    uint32_t revents_;  // epoll 最近一次返回的实际就绪事件
    bool added_;        // 当前 fd 是否已经加入所属 epoll 实例

    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};

} // namespace coreactor
