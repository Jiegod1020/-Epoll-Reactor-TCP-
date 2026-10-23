#include "Buffer.h"
#include <sys/uio.h>

namespace coreactor {

ssize_t Buffer::readFd(int fd, int* savedErrno) {
    // 第一段直接写入 Buffer 尾部；第二段是 64 KiB 的临时空间。
    // 当 Buffer 尾部不够用时，先把超出的数据读到 extrabuf，再统一 append，
    // 这样通常只需要一次 readv 系统调用。
    char extrabuf[65536];
    iovec vec[2];

    // readv 的第一段目标是 Buffer 当前可写区，第二段目标是栈上的临时空间。
    size_t writable = writableBytes();
    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    // 一次系统调用可以填满两个 iovec；返回值 n 是总共读取的字节数。
    ssize_t n = readv(fd, vec, 2);
    if (n < 0) {
        *savedErrno = errno;
    } else if (static_cast<size_t>(n) <= writable) {
        // 所有数据都落在 Buffer 原有可写区。
        writerIndex_ += n;
    } else {
        // Buffer 原有空间写满，剩余数据位于 extrabuf，需要追加到 vector。
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable);
    }
    return n;
}

void Buffer::makeSpace(size_t len) {
    // 优先复用已经消费掉的前置空间；只有总空闲空间仍不足时才扩容。
    if (writableBytes() + prependableBytes() < len) {
        // 可写区与已消费区合起来仍不够，直接扩到“当前写位置 + 所需长度”。
        buffer_.resize(writerIndex_ + len);
    } else {
        // 总空间够，只是前面有垃圾字节；把未消费数据搬到开头再写。
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
