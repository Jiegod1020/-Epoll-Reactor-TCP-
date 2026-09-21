#include "Coroutine.h"
#include "Scheduler.h"
#include <cstdlib>
#include <cstring>
#include <cassert>

namespace coreactor {

// 每个线程都有自己的“当前协程”指针；一个线程只运行自己所属 EventLoop 的协程。
static thread_local Coroutine* t_currentCo = nullptr;

Coroutine::Coroutine(Scheduler* scheduler, Callback cb, size_t stackSize)
    : scheduler_(scheduler)
    , stackSize_(stackSize)
    , state_(READY)
    , callback_(std::move(cb)) {

    // ucontext 不会自动分配栈，必须为每个协程准备独立的栈空间。
    stack_ = malloc(stackSize_);
    if (!stack_) {
        LOG_ERROR("malloc coroutine stack failed");
        abort();
    }

    // 先取得基础上下文，再替换为协程自己的栈和入口函数。
    getcontext(&ctx_);
    ctx_.uc_stack.ss_sp = stack_;
    ctx_.uc_stack.ss_size = stackSize_;
    ctx_.uc_link = nullptr;

    // makecontext 的可变参数接口按 int 传参。为了兼容 64 位指针，
    // 把 this 拆成低、高两个 32 位整数，在 MainFunc 中重新拼接。
    uintptr_t ptr = reinterpret_cast<uintptr_t>(this);
    makecontext(&ctx_, (void(*)())MainFunc, 2,
                static_cast<uint32_t>(ptr),
                static_cast<uint32_t>(ptr >> 32));
}

Coroutine::~Coroutine() {
    if (stack_) {
        free(stack_);
        stack_ = nullptr;
    }
}

void Coroutine::resume() {
    // 只有 READY 或 WAITING 的协程可以恢复；RUNNING 协程不能被递归 resume。
    assert(state_ == READY || state_ == WAITING);
    t_currentCo = this;
    state_ = RUNNING;
    swapcontext(&scheduler_->getMainCtx(), &ctx_);
}

void Coroutine::yield() {
    // WAITING 表示当前协程主动等待某个 fd，后续由 Scheduler 重新置为 READY。
    assert(state_ == RUNNING);
    t_currentCo = nullptr;
    state_ = WAITING;
    swapcontext(&ctx_, &scheduler_->getMainCtx());
}

Coroutine* Coroutine::GetCurrent() {
    return t_currentCo;
}

void Coroutine::MainFunc(uint32_t low, uint32_t high) {
    uintptr_t ptr = static_cast<uintptr_t>(low) |
                    (static_cast<uintptr_t>(high) << 32);
    Coroutine* co = reinterpret_cast<Coroutine*>(ptr);
    t_currentCo = co;

    co->callback_();

    // callback 正常返回意味着协程完成。不能直接从入口函数 return，
    // 因为 ctx_.uc_link 为空；必须显式切回 Scheduler 的主上下文。
    co->state_ = DEAD;
    t_currentCo = nullptr;
    swapcontext(&co->ctx_, &co->scheduler_->getMainCtx());
}

} // namespace coreactor
