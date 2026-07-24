#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include "util/noncopyable.h"
#include "Channel.h"
#include "coroutine/Scheduler.h"

namespace coreactor {

// EventLoop：事件循环，封装Epoll，One Loop Per Thread
class EventLoop : noncopyable {
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    // 启动事件循环
    void loop();
    // 退出循环
    void quit();

    // 更新/删除Channel对应的epoll事件
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

    // 跨线程投递任务，线程安全
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
    bool quit_;

    int epollFd_;
    int wakeupFd_;  // eventfd，用于跨线程唤醒epoll

    static const int kMaxEvents = 1024;
    std::vector<epoll_event> events_;

    std::unique_ptr<Channel> wakeupChannel_;
    std::unique_ptr<Scheduler> scheduler_;

    std::mutex mutex_;
    std::vector<Functor> pendingFunctors_;  // 待执行任务队列
    bool callingPendingFunctors_;
};

} // namespace coreactor
