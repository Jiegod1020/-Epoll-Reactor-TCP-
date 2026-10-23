#include "net/Buffer.h"
#include "net/Protocol.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
// 测试失败时输出原因并立即以非零状态结束；不受 Release 的 NDEBUG 影响。
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "test failed: " << message << '\n';
        std::abort();
    }
}
}

int main() {
    using namespace coreactor;

    // 准备两条连续 DATA 帧，用来模拟 TCP 将帧拆开或合并交付的情况。
    const std::string first = Protocol::encode(FrameType::DATA, "hello");
    const std::string second = Protocol::encode(FrameType::DATA, "world");
    Buffer input;
    Frame frame{};

    // 只追加第一帧的一部分，此时解码器应报告 INCOMPLETE 并保留这些字节。
    input.append(first.data(), 6);
        require(Protocol::decode(input, frame) == DecodeResult::INCOMPLETE,
            "partial frame must be incomplete");

    // 补齐第一帧并紧接着放入第二帧，测试半包恢复和粘包拆分。
    input.append(first.data() + 6, first.size() - 6);
    input.append(second);
        require(Protocol::decode(input, frame) == DecodeResult::COMPLETE,
            "first coalesced frame must decode");
        require(frame.type == FrameType::DATA, "first frame type");
        require(frame.payload == "hello", "first frame payload");

        require(Protocol::decode(input, frame) == DecodeResult::COMPLETE,
            "second coalesced frame must decode");
        require(frame.type == FrameType::DATA, "second frame type");
        require(frame.payload == "world", "second frame payload");
        require(input.readableBytes() == 0, "decoded frames consume input");

    // 长度为 0 的帧没有类型字节，必须被判为非法协议数据。
    const std::uint32_t invalidLength = 0;
    input.append(reinterpret_cast<const char*>(&invalidLength),
                 sizeof(invalidLength));
        require(Protocol::decode(input, frame) == DecodeResult::INVALID,
            "zero body length must be rejected");

    // 多线程创建/销毁 Buffer，覆盖线程本地缓存和中央池的并发路径。
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([]() {
            for (int iteration = 0; iteration < 100; ++iteration) {
                Buffer buffer;
                buffer.append(std::string(2048, 'x'));
                require(buffer.readableBytes() == 2048,
                    "pool-backed buffer retains appended bytes");
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    return 0;
}
