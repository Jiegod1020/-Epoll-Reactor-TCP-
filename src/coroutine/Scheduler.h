#pragma once
#include "Coroutine.h"
#include <queue>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace coreactor {

class EventLoop;

// 每个 EventLoop 线程对应一个 Scheduler。
// Scheduler 不负责等待 socket，它只负责管理协程；socket 是否就绪由 EventLoop
// 感知，再通过 fd -> coroutine 映射把等待中的协程放回 readyQueue_。
class Scheduler : noncopyable {
public:
    Scheduler(EventLoop* loop);
    ~Scheduler();

    // 执行当前这一轮的就绪协程。
    void start();
    // 停止调度器
    void stop();

    // 创建并调度一个新协程
    void createCoroutine(Coroutine::Callback cb);

    // 将协程放入 readyQueue_，等待下一次 start() 调度。
    void addReady(Coroutine::ptr co);

    // 当前协程让出执行权，回到 Scheduler 的主上下文。
    static void Yield();

    // 当 EventLoop 收到 fd 事件时，唤醒对应协程。
    void wakeupCoByFd(int fd);

    // 协程执行 coRead/coWrite 前登记自己正在等待哪个 fd。
    void registerFdCo(int fd, Coroutine::ptr co);
    void unregisterFdCo(int fd);

    ucontext_t& getMainCtx() { return mainCtx_; }
    EventLoop* getEventLoop() { return loop_; }

private:
    // 调度主循环
    void run();

private:
    EventLoop* loop_;
    ucontext_t mainCtx_;       // 调度器上下文，也就是线程原生执行栈
    bool running_;

    std::queue<Coroutine::ptr> readyQueue_;  // 等待运行的协程
    std::mutex queueMutex_;

    // fd -> 等待该 fd 事件的协程；该表由所属 EventLoop 线程访问。
    std::unordered_map<int, Coroutine::ptr> waitCoMap_;
};

} // namespace coreactor
