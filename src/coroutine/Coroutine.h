#pragma once
#include <ucontext.h>
#include <functional>
#include <memory>
#include "util/noncopyable.h"

namespace coreactor {

class Scheduler;

// 协程状态由调度器和 Coroutine::resume/yield 共同维护。
enum CoState {
    READY,      // 就绪，等待调度
    RUNNING,    // 运行中
    WAITING,    // 等待IO事件
    DEAD        // 执行结束
};

// 用户态协程：每个协程拥有独立栈和 ucontext 上下文。
// 协程切换不需要进入内核，适合把非阻塞 I/O 封装成同步风格代码。
class Coroutine : noncopyable, public std::enable_shared_from_this<Coroutine> {
public:
    using ptr = std::shared_ptr<Coroutine>;
    using Callback = std::function<void()>;

    Coroutine(Scheduler* scheduler, Callback cb, size_t stackSize = 128 * 1024);
    ~Coroutine();

    // 从上次暂停位置恢复运行；第一次调用则从 MainFunc 开始。
    void resume();
    // 保存当前寄存器和栈位置，回到所属 Scheduler 的主上下文。
    void yield();

    CoState getState() const { return state_; }
    void setState(CoState state) { state_ = state; }

    // 获取当前运行的协程
    static Coroutine* GetCurrent();

private:
    // makecontext 要求的入口函数签名；用两个 32 位整数传递 this 指针。
    static void MainFunc(uint32_t low, uint32_t high);

private:
    Scheduler* scheduler_;     // 所属调度器，协程最终切回它的 mainCtx_
    ucontext_t ctx_;           // 保存寄存器、指令位置和栈信息
    void* stack_;              // 协程私有栈
    size_t stackSize_;         // 栈大小
    CoState state_;            // 协程状态
    Callback callback_;        // 协程执行的业务函数
};

} // namespace coreactor
