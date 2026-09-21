#include "Scheduler.h"
#include "reactor/EventLoop.h"
#include "util/Logger.h"

namespace coreactor {

Scheduler::Scheduler(EventLoop* loop)
    : loop_(loop)
    , running_(false) {
    // 保存当前线程的上下文，协程 yield/resume 都以它为返回目标。
    getcontext(&mainCtx_);
}

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::start() {
    // EventLoop 每轮处理完 I/O 后调用一次；run() 执行完当前所有 ready 协程
    // 就返回，让线程继续回到 epoll_wait。
    running_ = true;
    run();
}

void Scheduler::stop() {
    running_ = false;
}

void Scheduler::createCoroutine(Coroutine::Callback cb) {
    // shared_ptr 让 readyQueue_、fd 等待表和连接对象共同管理协程生命周期。
    auto co = std::make_shared<Coroutine>(this, std::move(cb));
    addReady(co);
}

void Scheduler::addReady(Coroutine::ptr co) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    co->setState(READY);
    readyQueue_.push(co);
}

void Scheduler::Yield() {
    Coroutine* co = Coroutine::GetCurrent();
    if (co) {
        co->yield();
    }
}

void Scheduler::wakeupCoByFd(int fd) {
    // 一个 fd 在当前设计中最多对应一个等待协程：连接业务协程按顺序执行读写。
    auto it = waitCoMap_.find(fd);
    if (it != waitCoMap_.end()) {
        addReady(it->second);
        waitCoMap_.erase(it);
    }
}

void Scheduler::registerFdCo(int fd, Coroutine::ptr co) {
    // co 即将 yield；保存 shared_ptr 可以保证它在等待期间不会被销毁。
    waitCoMap_[fd] = co;
}

void Scheduler::unregisterFdCo(int fd) {
    waitCoMap_.erase(fd);
}

void Scheduler::run() {
    while (running_) {
        // 先处理所有就绪协程。协程可能在这里再次 yield，或运行到 DEAD。
        while (!readyQueue_.empty()) {
            Coroutine::ptr co;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                co = readyQueue_.front();
                readyQueue_.pop();
            }
            if (co->getState() == READY) {
                co->resume();
            }
        }
        // 没有就绪协程时回到 EventLoop，由 epoll 等待新的 I/O 事件。
        break;
    }
}

} // namespace coreactor
