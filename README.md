# -Epoll-Reactor-TCP-
针对传统多线程阻塞 TCP 服务器高并发场景下线程切换开销高、IO 阻塞导致 CPU 利用率低的问题，自研主从 Reactor 架构高性能长连接服务器，结合 Epoll 边缘触发与用户态协程调度，支撑万级并发连接，可作为网关、长连接通信 场景的基础网络组件。
# CoReactor - 基于 Epoll + 主从Reactor + 用户态协程的高性能TCP服务器

## 项目简介
自主实现的轻量级高性能TCP网络框架，采用 **主从Reactor多线程模型 + 用户态有栈协程**，将异步IO转化为同步编程风格，在保证高并发性能的同时极大提升业务代码可读性。

## 核心技术栈
- **IO多路复用**：Epoll 边缘触发(ET) + EPOLLONESHOT，高性能事件通知
- **网络模型**：主从Reactor多线程架构（One Loop Per Thread），充分利用多核CPU
- **协程实现**：基于 ucontext 自主实现用户态有栈协程，纳秒级上下文切换
- **网络编程**：非阻塞IO、TCP粘包处理、端口复用、事件驱动回调
- **数据结构**：动态扩容读写缓冲区、任务队列、协程调度队列
- **线程通信**：eventfd 跨线程唤醒机制 + 无锁任务队列

## 构建与运行

项目依赖 Linux 的 `epoll`、`eventfd` 和 `ucontext`，建议在 Linux 或 WSL 中构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/coreactor 8080 4
```

启动后服务器会原样回显客户端发送的数据。按 `Ctrl+C` 可让主 Reactor 和所有从 Reactor 有序退出。

### 先运行最小 TCP 服务器

学习时建议先运行不包含 Reactor 的阻塞式示例：

```bash
cmake --build build --target simple_tcp_server
./build/simple_tcp_server 8080
```

另开一个终端测试：

```bash
nc 127.0.0.1 8080
```

在 `nc` 中输入文字，服务器会原样返回。这个示例对应的源码是
`examples/simple_tcp_server.cpp`，可以按 `main()` 中的 1 到 8 步阅读。

## 架构设计
### 整体分层
