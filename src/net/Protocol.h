#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace coreactor {

class Buffer;

enum class FrameType : std::uint8_t {
    DATA = 1,          // 业务数据帧
    HEARTBEAT = 2,     // 服务端探测客户端是否存活
    HEARTBEAT_ACK = 3  // 客户端对探测的确认
};

struct Frame {
    FrameType type;       // 类型决定 payload 是业务数据还是控制数据
    std::string payload;  // DATA 帧的正文；心跳帧通常为空
};

enum class DecodeResult {
    INCOMPLETE,  // 缓冲区还没收到完整帧，数据应保留等待下次追加
    COMPLETE,    // 成功解出一帧，并从输入 Buffer 消费该帧
    INVALID      // 长度或类型非法，应关闭连接
};

// 帧格式：4 字节网络序 body 长度 + 1 字节类型 + payload。
// body 长度包括类型字节，因此最小长度为 1。
class Protocol {
public:
    // 限制单帧最大正文，避免对端声明巨型长度导致无界内存占用。
    static constexpr std::size_t kMaxPayloadSize = 1024 * 1024;

    // 把类型和载荷封成 wire format；超出大小限制返回空字符串。
    static std::string encode(FrameType type, const std::string& payload = {});
    // 从 Buffer 的可读区尝试解一帧；只有 COMPLETE 才会消费输入字节。
    static DecodeResult decode(Buffer& input, Frame& frame);
};

}  // namespace coreactor