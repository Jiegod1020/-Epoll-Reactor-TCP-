#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <thread>
#include <atomic>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include "util/noncopyable.h"
#include "Channel.h"
#include "coroutine/Scheduler.h"

namespace coreactor {

// EventLoop 是 Reactor 的核心：一个线程通常拥有一个 EventLoop，
// 一个 EventLoop 内部拥有一个 epoll 实例。
//
// 它的工作顺序是：等待 I/O -> 分发 Channel 回调 -> 执行跨线程任务 ->
// 调度就绪协程。业务对象不应该自己调用 epoll_wait，而应把 fd 注册成 Channel。
class EventLoop : noncopyable {
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    // 启动事件循环
    void loop();
    // 退出循环
    void quit();

    // 同步 Channel 的关注事件；events 为 0 时会从 epoll 删除。
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    bool isInLoopThread() const;

    // 在当前线程直接执行，否则放入任务队列并通过 eventfd 唤醒循环。
    void runInLoop(Functor cb);
    void queueInLoop(Functor cb);

    Scheduler* getScheduler() { return scheduler_.get(); }

private:
    // 唤醒epoll_wait
    void wakeup();
    // 处理唤醒事件
    void handleWakeup();
    // 执行任务队列
    void doPendingFunctors();

private:
    bool looping_;
    std::atomic<bool> quit_;       // quit() 可从其他线程调用，因此需要原子变量
    std::thread::id threadId_;

    int epollFd_;
    int wakeupFd_;                 // eventfd，用于跨线程唤醒 epoll_wait

    static const int kMaxEvents = 1024;
    std::vector<epoll_event> events_;

    std::unique_ptr<Channel> wakeupChannel_;
    std::unique_ptr<Scheduler> scheduler_;

    std::mutex mutex_;
    std::vector<Functor> pendingFunctors_;  // 待执行的跨线程任务
    bool callingPendingFunctors_;
};

} // namespace coreactor
