#include "TcpConnection.h"
#include "util/Logger.h"
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <sys/socket.h>

namespace coreactor {

namespace {
std::int64_t monotonicMilliseconds() {
    // steady_clock 不受系统时间/NTP 校准影响，适合计算时间间隔。
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

TcpConnection::TcpConnection(EventLoop* loop, int sockfd)
    : loop_(loop)
    , sockfd_(sockfd)
    , channel_(loop, sockfd)
    , connected_(true)
    , lastActivityMs_(monotonicMilliseconds())
    , pingSentAtMs_(0)
    , awaitingHeartbeatAck_(false) {

    // 注册成员函数回调。lambda 捕获 this，因为 Channel 的生命周期由本连接管理。
    channel_.setReadCallback([this]() { handleRead(); });
    channel_.setWriteCallback([this]() { handleWrite(); });
    channel_.setCloseCallback([this]() { handleClose(); });
    channel_.setErrorCallback([this]() { handleError(); });
}

TcpConnection::~TcpConnection() {
    // Channel 已在自身析构时从 epoll 移除；这里最后关闭底层 socket。
    close(sockfd_);
}

void TcpConnection::start() {
    // EPOLLONESHOT 的读事件每次处理后都会重新注册。
    channel_.enableReading();
}

void TcpConnection::shutdown() {
    if (connected_) {
        connected_ = false;
        // 不再接收任何事件；closeCallback 会通知 TcpServer 从连接表删除它。
        channel_.disableAll();
        if (closeCallback_) {
            closeCallback_(shared_from_this());
        }
    }
}

bool TcpConnection::sendData(const std::string& data) {
    // 连接已关闭时不再接受新业务数据。
    if (!connected_) {
        return false;
    }
    // 业务层只传载荷，协议模块负责增加长度头和 DATA 类型字节。
    const std::string encoded = Protocol::encode(FrameType::DATA, data);
    if (encoded.empty() && !data.empty()) {
        return false;
    }
    // 先排队，再尽力立即写；若内核缓冲区已满，handleWrite 会注册 EPOLLOUT。
    outputBuffer_.append(encoded);
    handleWrite();
    return connected_;
}

void TcpConnection::handleRead() {
    // ET 只通知状态变化，必须循环读取直到 readv 返回 EAGAIN。
    bool receivedData = false;
    bool peerClosed = false;
    bool readFailed = false;
    while (connected_) {
        int savedErrno = 0;
        ssize_t n = inputBuffer_.readFd(sockfd_, &savedErrno);
        if (n > 0) {
            // 记录有数据到达；继续读，直到本轮内核可读数据取尽。
            receivedData = true;
            lastActivityMs_ = monotonicMilliseconds();
            continue;
        }
        if (n == 0) {
            // read 返回 0 是 EOF：对端已经有序关闭发送方向。
            peerClosed = true;
            break;
        }

        // EINTR 是信号打断系统调用，数据并未读完，直接重试。
        if (savedErrno == EINTR) {
            continue;
        }
        // EAGAIN/EWOULDBLOCK 表示当前读空，是 ET 循环的正常结束条件。
        if (savedErrno != EAGAIN && savedErrno != EWOULDBLOCK) {
            errno = savedErrno;
            LOG_ERROR("TcpConnection read error");
            readFailed = true;
        }
        break;
    }

    // 先解析本轮收到的字节；即使随后发现 EOF，也不丢掉已到达的数据。
    if (receivedData) {
        // ET 必须读到 EAGAIN；随后批量解析当前收到的所有完整协议帧。
        processInputFrames();
    }
    if (peerClosed || readFailed) {
        // 先处理 EOF 前已经读取到的字节，再通知业务连接关闭。
        handleClose();
        return;
    }
    // ONESHOT 已在本次事件后停用，连接仍有效时重新订阅读事件。
    if (connected_) {
        channel_.enableReading();
    }
}

void TcpConnection::handleWrite() {
    // send 可能只写一部分；循环处理直到缓冲区清空或内核发送缓冲区已满。
    while (outputBuffer_.readableBytes() > 0) {
        ssize_t n = send(sockfd_, outputBuffer_.peek(),
                         outputBuffer_.readableBytes(), MSG_NOSIGNAL);
        if (n > 0) {
            outputBuffer_.retrieve(static_cast<size_t>(n));
            continue;
        }
        // 被信号打断时重试，不丢弃输出缓冲区中的字节。
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            handleError();
            return;
        }
        break;
    }

    if (outputBuffer_.readableBytes() == 0) {
        // 没有待发字节时关闭可写监听；socket 通常一直可写，持续监听会造成空转。
        channel_.disableWriting();
    } else {
        // 还有字节没发送完，等待下一次可写事件后继续 flush。
        channel_.enableWriting();
    }
}

void TcpConnection::handleClose() {
    if (!connected_) {
        return;
    }
    // 先改变状态，保证重入时下面的关闭逻辑只执行一次。
    connected_ = false;
    channel_.disableAll();

    if (closeCallback_) {
        closeCallback_(shared_from_this());
    }
}

void TcpConnection::queueControlFrame(FrameType type) {
    // 心跳是控制帧，不经过业务消息回调，但复用相同的输出队列和写逻辑。
    outputBuffer_.append(Protocol::encode(type));
    handleWrite();
}

void TcpConnection::checkHeartbeat(std::int64_t nowMs,
                                  std::int64_t intervalMs,
                                  std::int64_t timeoutMs) {
    if (!connected_) {
        return;
    }
    // 已经发过 PING 时只检查 ACK 超时，不重复发送新的 PING。
    if (awaitingHeartbeatAck_) {
        if (nowMs - pingSentAtMs_ >= timeoutMs) {
            LOG_INFO("heartbeat timeout, fd=" << sockfd_);
            handleClose();
        }
        return;
    }
    // 长时间没有客户端活动时发起一次心跳探测。
    if (nowMs - lastActivityMs_.load() >= intervalMs) {
        awaitingHeartbeatAck_ = true;
        pingSentAtMs_ = nowMs;
        queueControlFrame(FrameType::HEARTBEAT);
    }
}

void TcpConnection::handleError() {
    LOG_ERROR("TcpConnection error");
    handleClose();
}

void TcpConnection::processInputFrames() {
    // 把本轮 socket 字节并入协议缓存；半帧留在 protocolBuffer_ 等下一轮补全。
    protocolBuffer_.append(inputBuffer_.retrieveAllAsString());
    while (connected_) {
        Frame frame{};
        // decode 每次最多消费一帧；循环继续解析同一批数据中的后续粘连帧。
        const DecodeResult result = Protocol::decode(protocolBuffer_, frame);
        if (result == DecodeResult::INCOMPLETE) {
            return;
        }
        if (result == DecodeResult::INVALID) {
            LOG_ERROR("invalid protocol frame, fd=" << sockfd_);
            handleClose();
            return;
        }

        // 按类型区分业务消息和连接控制消息。
        switch (frame.type) {
            case FrameType::DATA: {
                if (messageCallback_) {
                    // 构造只包含载荷的临时 Buffer，回调返回后它会自动销毁。
                    Buffer message;
                    message.append(frame.payload);
                    messageCallback_(shared_from_this(), &message);
                }
                break;
            }
            case FrameType::HEARTBEAT:
                queueControlFrame(FrameType::HEARTBEAT_ACK);
                break;
            case FrameType::HEARTBEAT_ACK:
                // 收到 ACK 后清除等待状态，下一次空闲超时才会再发心跳。
                awaitingHeartbeatAck_ = false;
                break;
        }
    }
}

} // namespace coreactor
