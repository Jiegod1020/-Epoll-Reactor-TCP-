#include "Buffer.h"
#include <sys/uio.h>

namespace coreactor {

ssize_t Buffer::readFd(int fd, int* savedErrno) {
    char extrabuf[65536];
    iovec vec[2];

    size_t writable = writableBytes();
    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    // 使用readv分散读，减少系统调用
    ssize_t n = readv(fd, vec, 2);
    if (n < 0) {
        *savedErrno = errno;
    } else if (static_cast<size_t>(n) <= writable) {
        writerIndex_ += n;
    } else {
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable);
    }
    return n;
}

void Buffer::makeSpace(size_t len) {
    if (writableBytes() + prependableBytes() < len) {
        buffer_.resize(writerIndex_ + len);
    } else {
        // 内部腾挪，把可读数据移到缓冲区头部
        size_t readable = readableBytes();
        std::copy(begin() + readerIndex_,
                  begin() + writerIndex_,
                  begin());
        readerIndex_ = 0;
        writerIndex_ = readable;
    }
}

} // namespace coreactor
