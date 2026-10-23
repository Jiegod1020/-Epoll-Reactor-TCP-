#include "Protocol.h"

#include "Buffer.h"

#include <arpa/inet.h>
#include <cstring>

namespace coreactor {

std::string Protocol::encode(FrameType type, const std::string& payload) {
    // 编码前先限制载荷；长度字段用 uint32 表示，但项目仍主动采用更小上限。
    if (payload.size() > kMaxPayloadSize) {
        return {};
    }

    // body 长度包括 1 字节类型，不包括前面的 4 字节长度字段。
    // htonl 把主机字节序转换为网络字节序，保证不同 CPU 架构可互通。
    const std::uint32_t bodyLength =
        htonl(static_cast<std::uint32_t>(payload.size() + 1));
    // 一次分配完整帧大小，布局为 [4 字节长度][1 字节类型][载荷]。
    std::string frame(sizeof(bodyLength) + 1 + payload.size(), '\0');
    std::memcpy(frame.data(), &bodyLength, sizeof(bodyLength));
    frame[sizeof(bodyLength)] = static_cast<char>(type);
    if (!payload.empty()) {
        std::memcpy(frame.data() + sizeof(bodyLength) + 1,
                    payload.data(), payload.size());
    }
    return frame;
}

DecodeResult Protocol::decode(Buffer& input, Frame& frame) {
    // 第一步必须先收齐固定长度头部，才能知道后续正文还需要多少字节。
    constexpr std::size_t kHeaderSize = sizeof(std::uint32_t);
    if (input.readableBytes() < kHeaderSize) {
        return DecodeResult::INCOMPLETE;
    }

    // 使用 memcpy 读取整数，避免把未对齐的 char* 强转成 uint32_t*。
    std::uint32_t networkLength = 0;
    std::memcpy(&networkLength, input.peek(), kHeaderSize);
    // ntohl 将网络序长度还原到本机字节序。
    const std::uint32_t bodyLength = ntohl(networkLength);
    // body 至少有类型字节，且不能超过约定的最大载荷加类型字节。
    if (bodyLength == 0 || bodyLength > kMaxPayloadSize + 1) {
        return DecodeResult::INVALID;
    }

    // 头部完整但正文没到齐时，不能消费 Buffer；下次 read 后继续尝试。
    const std::size_t frameLength = kHeaderSize + bodyLength;
    if (input.readableBytes() < frameLength) {
        return DecodeResult::INCOMPLETE;
    }

    // 只有确认整帧到齐后才检查类型和复制正文。
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(input.peek());
    const std::uint8_t rawType = bytes[kHeaderSize];
    if (rawType < static_cast<std::uint8_t>(FrameType::DATA) ||
        rawType > static_cast<std::uint8_t>(FrameType::HEARTBEAT_ACK)) {
        return DecodeResult::INVALID;
    }

    // 填充输出对象，然后推进 Buffer 读位置，留下后面的粘连帧供下一次 decode。
    frame.type = static_cast<FrameType>(rawType);
    frame.payload.assign(input.peek() + kHeaderSize + 1, bodyLength - 1);
    input.retrieve(frameLength);
    return DecodeResult::COMPLETE;
}

}  // namespace coreactor