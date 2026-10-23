# Linux 主从 Reactor TCP 长连接服务器

## 项目简介
基于 Linux 的 TCP 长连接服务器学习项目，包含非阻塞 socket、主从 Reactor、Epoll ET/ONESHOT、三层小对象内存池、长度前缀应用层协议和空闲连接心跳检测。I/O 和业务回调由从 Reactor 线程直接驱动，不为每个连接创建协程。项目实现了网络框架的关键路径；性能指标仍需在目标机器上压测确认。

## 核心技术栈
- **IO多路复用**：Epoll 边缘触发(ET) + EPOLLONESHOT，高性能事件通知
- **网络模型**：主从Reactor多线程架构（One Loop Per Thread），充分利用多核CPU
- **网络编程**：非阻塞IO、TCP粘包处理、端口复用、事件驱动回调
- **数据结构**：动态扩容读写缓冲区、跨线程任务队列
- **线程通信**：eventfd 跨线程唤醒机制 + 互斥保护的任务队列
- **内存分配**：线程本地缓存 -> 中央空闲链 -> 64 KiB 页批量切块；Buffer 使用池化 allocator
- **应用层协议**：4 字节网络序长度 + 1 字节类型 + 数据，最大载荷 1 MiB
- **连接保活**：空闲 30 秒发送 HEARTBEAT，10 秒未收到 ACK 则清理连接

并发能力会受文件描述符上限、内存、内核参数和压测模型影响；“万级连接”需要在目标 Linux 环境调优并通过压测验证，本项目默认不宣称已验证该指标。

## 构建与运行

项目依赖 Linux 的 `epoll` 和 `eventfd`，建议在 Linux 或 WSL 中构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/coreactor 8080 4
```

启动后服务器会回显客户端发送的 DATA 帧载荷，并自动处理心跳帧。按 `Ctrl+C` 可让主 Reactor 和所有从 Reactor 有序退出。

主服务器使用长度前缀二进制协议，不接受裸文本。可使用项目提供的 Python 客户端：

```bash
python3 examples/protocol_client.py 127.0.0.1 8080 "hello reactor"
```

帧格式为 `uint32 network-order body_length | uint8 type | payload`。类型 `1` 是业务数据，`2` 是心跳请求，`3` 是心跳应答；`body_length` 包含类型字节但不包含 4 字节长度字段。

运行协议拆包/粘包测试：

```bash
cmake --build build --target protocol_test
ctest --test-dir build --output-on-failure
```

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

在 `nc` 中输入文字，服务器会原样返回。这个旧式阻塞示例对应的源码是
`examples/simple_tcp_server.cpp`，可以按 `main()` 中的 1 到 8 步阅读。

## 架构设计
### 整体分层
