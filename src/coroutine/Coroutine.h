#pragma once
#include <ucontext.h>
#include <functional>
#include <memory>
#include "util/noncopyable.h"

namespace coreactor {

class Scheduler;

enum CoState {
    READY,      // 就绪，等待调度
    RUNNING,    // 运行中
    WAITING,    // 等待IO事件
    DEAD        // 执行结束
};

class Coroutine : noncopyable, public std::enable_shared_from_this<Coroutine> {
public:
    using ptr = std::shared_ptr<Coroutine>;
    using Callback = std::function<void()>;

    Coroutine(Scheduler* scheduler, Callback cb, size_t stackSize = 128 * 1024);
    ~Coroutine();

    // 恢复协程运行
    void resume();
    // 让出CPU，回到调度器主协程
    void yield();

    CoState getState() const { return state_; }
    void setState(CoState state) { state_ = state; }

    // 获取当前运行的协程
    static Coroutine* GetCurrent();

private:
    // 协程入口函数
    static void MainFunc(uint32_t low, uint32_t high);

private:
    Scheduler* scheduler_;     // 所属调度器
    ucontext_t ctx_;           // 协程上下文
    void* stack_;              // 协程栈指针
    size_t stackSize_;         // 栈大小
    CoState state_;            // 协程状态
    Callback callback_;        // 协程执行的业务函数
};

} // namespace coreactor
