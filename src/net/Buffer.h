#pragma once
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include "util/noncopyable.h"

namespace coreactor {

// 动态扩容读写缓冲区，解决TCP粘包问题
class Buffer : noncopyable {
public:
    static const size_t kInitialSize = 1024;

    Buffer() : buffer_(kInitialSize), readerIndex_(0), writerIndex_(0) {}

    size_t readableBytes() const { return writerIndex_ - readerIndex_; }
    size_t writableBytes() const { return buffer_.size() - writerIndex_; }
    size_t prependableBytes() const { return readerIndex_; }

    // 读取数据
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

    // 写入数据
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

    // 从fd读取数据到缓冲区
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
    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};

} // namespace coreactor
