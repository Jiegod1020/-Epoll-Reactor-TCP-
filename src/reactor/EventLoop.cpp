#include "EventLoop.h"
#include "util/Logger.h"
#include "util/SocketUtils.h"
#include <unistd.h>
#include <cassert>

namespace coreactor {

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false) {

    // epoll_create1 返回一个 epoll 文件描述符，后续所有 Channel 都注册到这里。
    epollFd_ = epoll_create1(0);
    if (epollFd_ < 0) {
        LOG_ERROR("epoll_create1 failed");
        abort();
    }

    // eventfd 本质上是一个内核计数器：其他线程写入 1，epoll_wait 就会返回；
    // 非阻塞模式保证清空 eventfd 时不会卡住，CLOEXEC 防止 fd 泄漏到 exec 后的进程。
    wakeupFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeupFd_ < 0) {
        LOG_ERROR("eventfd create failed");
        abort();
    }

    events_.resize(kMaxEvents);

    // 把 eventfd 也包装成 Channel，这样 EventLoop 可以用同一套事件分发机制
    // 处理“真实 socket 事件”和“跨线程任务唤醒”。
    wakeupChannel_ = std::make_unique<Channel>(this, wakeupFd_);
    wakeupChannel_->setReadCallback([this]() { handleWakeup(); });
    wakeupChannel_->enableReading();

    scheduler_ = std::make_unique<Scheduler>(this);
}

EventLoop::~EventLoop() {
    // 先销毁 wakeupChannel_，让它有机会从 epoll 中删除；然后再关闭 epoll fd。
    // 如果先 close(epollFd_)，Channel 析构时的 EPOLL_CTL_DEL 就会操作无效 fd。
    wakeupChannel_.reset();
    scheduler_.reset();
    close(wakeupFd_);
    close(epollFd_);
}

void EventLoop::loop() {
    assert(!looping_);
    looping_ = true;
    threadId_ = std::this_thread::get_id();
    quit_ = false;

    LOG_INFO("EventLoop started");

    while (!quit_) {
        // vector 必须保持足够的 size，epoll_wait 会直接把结果写入 data()。
        // 不能使用 clear()，否则下方按下标访问 events_[i] 会越界。
        events_.resize(kMaxEvents);
        int numEvents = epoll_wait(epollFd_, events_.data(), kMaxEvents, -1);

        if (numEvents < 0 && errno != EINTR) {
            LOG_ERROR("epoll_wait failed");
            break;
        }

        // 第一阶段：处理本轮已经就绪的 I/O 事件。
        for (int i = 0; i < numEvents; ++i) {
            Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
            channel->setRevents(events_[i].events);
            channel->handleEvent();
        }

        // 第二阶段：处理其他线程投递的任务，例如把新连接交给从 Reactor。
        doPendingFunctors();

        // 第三阶段：运行因 I/O 就绪而被唤醒的协程。
        scheduler_->start();
    }

    looping_ = false;
    LOG_INFO("EventLoop stopped");
}

void EventLoop::quit() {
    // 仅设置标志位还不够，因为 EventLoop 可能正阻塞在 epoll_wait；
    // 必须同时唤醒它，让它尽快观察到 quit_。
    quit_ = true;
    wakeup();
}

void EventLoop::updateChannel(Channel* channel) {
    // epoll_event 的 data.ptr 保存 Channel 地址，事件返回时可以通过它找回
    // 对应的回调对象。
    epoll_event ev{};
    ev.data.ptr = channel;
    ev.events = channel->events();

    int fd = channel->fd();
    if (channel->events() == 0) {
        removeChannel(channel);
        return;
    }

    // 首次注册用 ADD，修改已有关注集合用 MOD。
    int operation = channel->added() ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    if (epoll_ctl(epollFd_, operation, fd, &ev) < 0) {
        LOG_ERROR("epoll_ctl " << (operation == EPOLL_CTL_ADD ? "add" : "mod")
                               << " fd=" << fd << " failed");
        return;
    }
    channel->setAdded(true);
}

void EventLoop::removeChannel(Channel* channel) {
    if (!channel->added()) {
        return;
    }
    if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, channel->fd(), nullptr) < 0 &&
        errno != ENOENT) {
        LOG_ERROR("epoll_ctl del fd=" << channel->fd() << " failed");
    }
    channel->setAdded(false);
}

bool EventLoop::isInLoopThread() const {
    // 通过线程 id 判断是否可以直接执行回调，避免业务对象跨线程触碰状态。
    return threadId_ == std::this_thread::get_id();
}

void EventLoop::runInLoop(Functor cb) {
    if (isInLoopThread()) {
        cb();
    } else {
        queueInLoop(std::move(cb));
    }
}

void EventLoop::queueInLoop(Functor cb) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }
    // 无论任务是由哪个线程投递，都唤醒目标 EventLoop；多个写入会在 eventfd
    // 中合并为计数，EventLoop 被唤醒后一次性取走当前任务快照。
    wakeup();
}

void EventLoop::wakeup() {
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one) && errno != EAGAIN) {
        LOG_ERROR("wakeup write failed");
    }
}

void EventLoop::handleWakeup() {
    // eventfd 可能累积多个唤醒请求，循环读取直到 EAGAIN，把计数器清空。
    uint64_t value;
    while (read(wakeupFd_, &value, sizeof(value)) == sizeof(value)) {
    }
    // ONESHOT 事件被消费后必须重新 arm，否则下一次 queueInLoop 无法再次唤醒。
    wakeupChannel_->enableReading();
}

void EventLoop::doPendingFunctors() {
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        // 只在交换 vector 时持锁，真正执行回调放在锁外，避免回调相互阻塞。
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for (auto& func : functors) {
        func();
    }

    callingPendingFunctors_ = false;
}

} // namespace coreactor
