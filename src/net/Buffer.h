#pragma once
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include "memory/MemoryPool.h"
#include "util/noncopyable.h"

namespace coreactor {

// TCP 是无消息边界的字节流：一次 read 可能得到半条消息，也可能得到多条消息。
// Buffer 只负责保存字节，不负责定义协议边界；具体的换行、长度字段等协议解析
// 应该由业务层完成。
class Buffer : noncopyable {
public:
    static const size_t kInitialSize = 1024;

    // 初始给 1 KiB；vector 扩容时由 PoolAllocator 向 MemoryPool 申请空间。
    Buffer() : buffer_(kInitialSize), readerIndex_(0), writerIndex_(0) {}

    // 可读区范围是 [readerIndex_, writerIndex_)。
    size_t readableBytes() const { return writerIndex_ - readerIndex_; }
    // 可写区从 writerIndex_ 一直延伸到 vector 尾部。
    size_t writableBytes() const { return buffer_.size() - writerIndex_; }
    // readerIndex_ 之前的空间已经消费，可在空间不足时搬移数据后复用。
    size_t prependableBytes() const { return readerIndex_; }

    // peek 只观察数据，不移动 readerIndex_；retrieve 才会消费数据。
    const char* peek() const { return begin() + readerIndex_; }

    void retrieve(size_t len) {
        // 只移动读下标，不复制数据；len 超过可读量时视为全部消费。
        if (len < readableBytes()) {
            readerIndex_ += len;
        } else {
            retrieveAll();
        }
    }

    void retrieveAll() {
        // 重置两个下标即可复用整块内存，不需要释放再重新申请。
        readerIndex_ = 0;
        writerIndex_ = 0;
    }

    std::string retrieveAllAsString() {
        // 业务需要独立 string 时复制可读字节，然后清空 Buffer 的逻辑内容。
        std::string str(peek(), readableBytes());
        retrieveAll();
        return str;
    }

    // 写入用户数据，必要时自动扩容或搬移未消费数据。
    void append(const char* data, size_t len) {
        // 先确保尾部空间足够，再复制输入并推进写下标。
        ensureWritable(len);
        std::copy(data, data + len, beginWrite());
        writerIndex_ += len;
    }

    void append(const std::string& str) {
        append(str.data(), str.size());
    }

    char* beginWrite() { return begin() + writerIndex_; }
    const char* beginWrite() const { return begin() + writerIndex_; }

    // 使用 readv 把 socket 数据直接写入 Buffer，减少一次拷贝和系统调用。
    ssize_t readFd(int fd, int* savedErrno);

private:
    char* begin() { return &*buffer_.begin(); }
    const char* begin() const { return &*buffer_.begin(); }

    void ensureWritable(size_t len) {
        // 尾部空间不足时，makeSpace 会选择扩容或整理前置空闲区。
        if (writableBytes() < len) {
            makeSpace(len);
        }
    }

    void makeSpace(size_t len);

private:
    std::vector<char, PoolAllocator<char>> buffer_; // 实际存储空间，由三层内存池分配
    size_t readerIndex_;       // 第一个未消费字节
    size_t writerIndex_;       // 下一个可写位置
};

} // namespace coreactor
