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

namespace coreactor {

// EventLoop 是 Reactor 的核心：一个线程通常拥有一个 EventLoop，
// 一个 EventLoop 内部拥有一个 epoll 实例。
//
// 它的工作顺序是：等待 I/O -> 分发 Channel 回调 -> 执行跨线程任务。
// 业务对象不应该自己调用 epoll_wait，而应把 fd 注册成 Channel。
class EventLoop : noncopyable {
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    // 在当前线程持续等待并分发 I/O 事件，直到 quit() 被调用。
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

private:
    // 写 eventfd，让可能阻塞在 epoll_wait 的线程醒来。
    void wakeup();
    // 读取并清空 eventfd 计数，然后重新注册 ONESHOT 读事件。
    void handleWakeup();
    // 取出其他线程投递的任务并在本 EventLoop 线程执行。
    void doPendingFunctors();

private:
    bool looping_;                 // 防止同一个 EventLoop 被重复进入 loop()
    std::atomic<bool> quit_;       // quit() 可从其他线程调用，因此使用原子变量
    std::thread::id threadId_;     // loop() 记录的所属线程 id

    int epollFd_;                  // epoll 实例 fd，管理本 EventLoop 的所有 Channel
    int wakeupFd_;                 // eventfd，用于跨线程唤醒 epoll_wait

    static const int kMaxEvents = 1024;
    std::vector<epoll_event> events_; // epoll_wait 写入的就绪事件数组

    std::unique_ptr<Channel> wakeupChannel_;

    std::mutex mutex_;                       // 保护任务队列
    std::vector<Functor> pendingFunctors_;   // 待执行的跨线程任务
    bool callingPendingFunctors_;            // 标记当前是否正在执行取出的任务
};

} // namespace coreactor
