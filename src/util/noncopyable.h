#pragma once

namespace coreactor {

// 继承这个基类的对象不能被复制，只能在明确的生命周期内由一个所有者管理。
// EventLoop、Channel、TcpConnection 等对象都绑定了 fd、线程或上下文，
// 随意复制会导致两个对象误操作同一份底层资源。
class noncopyable {
public:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator=(const noncopyable&) = delete;

protected:
    noncopyable() = default;
    ~noncopyable() = default;
};

} // namespace coreactor
