#include "Coroutine.h"
#include "Scheduler.h"
#include <cstdlib>
#include <cstring>
#include <cassert>

namespace coreactor {

// 线程局部变量：当前正在运行的协程
static thread_local Coroutine* t_currentCo = nullptr;

Coroutine::Coroutine(Scheduler* scheduler, Callback cb, size_t stackSize)
    : scheduler_(scheduler)
    , stackSize_(stackSize)
    , state_(READY)
    , callback_(std::move(cb)) {

    stack_ = malloc(stackSize_);
    if (!stack_) {
        LOG_ERROR("malloc coroutine stack failed");
        abort();
    }

    getcontext(&ctx_);
    ctx_.uc_stack.ss_sp = stack_;
    ctx_.uc_stack.ss_size = stackSize_;
    ctx_.uc_link = nullptr;

    // 将this指针拆分为两个32位传入入口函数
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
    assert(state_ == READY || state_ == WAITING);
    t_currentCo = this;
    state_ = RUNNING;
    swapcontext(&scheduler_->getMainCtx(), &ctx_);
}

void Coroutine::yield() {
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

    co->state_ = DEAD;
    t_currentCo = nullptr;
}

} // namespace coreactor
