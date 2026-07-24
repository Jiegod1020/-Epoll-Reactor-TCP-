#include "Scheduler.h"
#include "reactor/EventLoop.h"
#include "util/Logger.h"

namespace coreactor {

Scheduler::Scheduler(EventLoop* loop)
    : loop_(loop)
    , running_(false) {
    getcontext(&mainCtx_);
}

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::start() {
    running_ = true;
    run();
}

void Scheduler::stop() {
    running_ = false;
}

void Scheduler::createCoroutine(Coroutine::Callback cb) {
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
    auto it = waitCoMap_.find(fd);
    if (it != waitCoMap_.end()) {
        addReady(it->second);
        waitCoMap_.erase(it);
    }
}

void Scheduler::registerFdCo(int fd, Coroutine::ptr co) {
    waitCoMap_[fd] = co;
}

void Scheduler::unregisterFdCo(int fd) {
    waitCoMap_.erase(fd);
}

void Scheduler::run() {
    while (running_) {
        // 先处理所有就绪协程
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
        // 没有就绪协程时，回到EventLoop处理IO事件
        break;
    }
}

} // namespace coreactor
