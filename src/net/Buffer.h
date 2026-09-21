#pragma once
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include "util/noncopyable.h"

namespace coreactor {

// TCP 是无消息边界的字节流：一次 read 可能得到半条消息，也可能得到多条消息。
// Buffer 只负责保存字节，不负责定义协议边界；具体的换行、长度字段等协议解析
// 应该由业务层完成。
class Buffer : noncopyable {
public:
    static const size_t kInitialSize = 1024;

    Buffer() : buffer_(kInitialSize), readerIndex_(0), writerIndex_(0) {}

    size_t readableBytes() const { return writerIndex_ - readerIndex_; }
    size_t writableBytes() const { return buffer_.size() - writerIndex_; }
    size_t prependableBytes() const { return readerIndex_; }

    // peek 只观察数据，不移动 readerIndex_；retrieve 才会消费数据。
    const char* peek() const { return begin() + readerIndex_; }

    void retrieve(size_t len) {
        if (len < readableBytes()) {
            readerIndex_ += len;
        } else {
            retrieveAll();
        }
    }

    void retrieveAll() {
        readerIndex_ = 0;
        writerIndex_ = 0;
    }

    std::string retrieveAllAsString() {
        std::string str(peek(), readableBytes());
        retrieveAll();
        return str;
    }

    // 写入用户数据，必要时自动扩容或搬移未消费数据。
    void append(const char* data, size_t len) {
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
        if (writableBytes() < len) {
            makeSpace(len);
        }
    }

    void makeSpace(size_t len);

private:
    std::vector<char> buffer_; // 实际存储空间
    size_t readerIndex_;       // 第一个未消费字节
    size_t writerIndex_;       // 下一个可写位置
};

} // namespace coreactor
