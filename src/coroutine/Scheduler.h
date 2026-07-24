#pragma once
#include "Coroutine.h"
#include <queue>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace coreactor {

class EventLoop;

// 协程调度器：每个EventLoop线程对应一个调度器
class Scheduler : noncopyable {
public:
    Scheduler(EventLoop* loop);
    ~Scheduler();

    // 启动调度器
    void start();
    // 停止调度器
    void stop();

    // 创建并调度一个新协程
    void createCoroutine(Coroutine::Callback cb);

    // 将协程加入就绪队列
    void addReady(Coroutine::ptr co);

    // 当前协程让出CPU
    static void Yield();

    // 唤醒等待fd事件的协程
    void wakeupCoByFd(int fd);

    // 注册fd与等待协程的映射
    void registerFdCo(int fd, Coroutine::ptr co);
    void unregisterFdCo(int fd);

    ucontext_t& getMainCtx() { return mainCtx_; }
    EventLoop* getEventLoop() { return loop_; }

private:
    // 调度主循环
    void run();

private:
    EventLoop* loop_;
    ucontext_t mainCtx_;       // 主协程上下文（线程原生上下文）
    bool running_;

    std::queue<Coroutine::ptr> readyQueue_;  // 就绪协程队列
    std::mutex queueMutex_;

    // fd -> 等待该fd事件的协程
    std::unordered_map<int, Coroutine::ptr> waitCoMap_;
};

} // namespace coreactor
