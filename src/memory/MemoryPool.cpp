#include "MemoryPool.h"

#include <mutex>

namespace coreactor {

MemoryPool& MemoryPool::instance() {
    // 故意保持到进程退出，避免其他线程的 thread_local cache 析构时
    // 访问一个已析构的全局池对象。
    // 进程内只创建一个池。指针故意不 delete，避免 TLS 析构顺序不确定时
    // 某个线程缓存回收空闲块却访问到已析构的单例。
    static MemoryPool* pool = new MemoryPool();
    return *pool;
}

MemoryPool::ThreadCache& MemoryPool::localCache() {
    // 每个线程只创建自己的缓存；普通 allocate/deallocate 可先走无锁本地链表。
    static thread_local ThreadCache cache;
    return cache;
}

MemoryPool::ThreadCache::~ThreadCache() {
    MemoryPool::instance().releaseLocalCache(*this);
}

std::size_t MemoryPool::classIndex(std::size_t bytes) {
    // 找到第一个“不小于请求字节数”的 2 的幂尺寸档。
    std::size_t size = kMinBlockSize;
    std::size_t index = 0;
    while (size < bytes && index + 1 < kClassCount) {
        size <<= 1;
        ++index;
    }
    return index;
}

std::size_t MemoryPool::blockSize(std::size_t index) {
    return kMinBlockSize << index;
}

void* MemoryPool::allocate(std::size_t bytes) {
    // 零字节请求按 1 字节处理，保证后续有合法尺寸档。
    if (bytes == 0) {
        bytes = 1;
    }
    // 大块不适合切进小对象页，直接使用全局分配器。
    if (bytes > kMaxBlockSize) {
        return ::operator new(bytes);
    }

    const std::size_t index = classIndex(bytes);
    ThreadCache& cache = localCache();
    // 本地空闲链表命中时只改指针；没有空闲块时才进入中央池补货。
    if (cache.heads[index] == nullptr) {
        refill(cache, index);
    }

    // 取出链表头。每个空闲块的前 sizeof(void*) 字节暂存下一块地址。
    void* ptr = cache.heads[index];
    cache.heads[index] = *static_cast<void**>(ptr);
    --cache.counts[index];
    return ptr;
}

void MemoryPool::deallocate(void* ptr, std::size_t bytes) noexcept {
    // delete nullptr 与 free(nullptr) 一样是安全的空操作。
    if (ptr == nullptr) {
        return;
    }
    if (bytes == 0) {
        bytes = 1;
    }
    // 必须与 allocate 的分支对应：大块由全局 delete 释放。
    if (bytes > kMaxBlockSize) {
        ::operator delete(ptr);
        return;
    }

    const std::size_t index = classIndex(bytes);
    ThreadCache& cache = localCache();
    // 优先把块归还当前线程，避免每次释放都争用中央锁。
    if (cache.counts[index] < kLocalCacheLimit) {
        *static_cast<void**>(ptr) = cache.heads[index];
        cache.heads[index] = ptr;
        ++cache.counts[index];
    } else {
        releaseToCentral(index, ptr);
    }
}

void MemoryPool::refill(ThreadCache& cache, std::size_t index) {
    // 只有本地缓存耗尽才加中央锁；一次搬一批，摊薄锁开销。
    std::lock_guard<std::mutex> lock(mutex_);
    if (centralHeads_[index] == nullptr) {
        // 中央链也空时，先申请一页并将它切成该档尺寸的小块。
        allocatePageBlocks(index);
    }

    std::size_t count = 0;
    // 从中央链摘下最多 32 块，逐个接到当前线程缓存链表。
    while (count < kRefillBatch && centralHeads_[index] != nullptr) {
        void* ptr = centralHeads_[index];
        centralHeads_[index] = *static_cast<void**>(ptr);
        *static_cast<void**>(ptr) = cache.heads[index];
        cache.heads[index] = ptr;
        ++cache.counts[index];
        ++count;
    }
}

void MemoryPool::allocatePageBlocks(std::size_t index) {
    // 按该尺寸档计算一页可切出的块数，例如 16 字节档可切 4096 块。
    const std::size_t size = blockSize(index);
    const std::size_t count = kPageSize / size;
    // nothrow 版本用 nullptr 表示失败，再统一转换成标准 bad_alloc 异常。
    void* page = ::operator new(kPageSize, std::nothrow);
    if (page == nullptr) {
        throw std::bad_alloc();
    }

    try {
        pages_.push_back(page);
    } catch (...) {
        ::operator delete(page);
        throw;
    }
    // 每块首地址写入中央链表当前头指针，使整页所有小块串成空闲链。
    auto* bytes = static_cast<unsigned char*>(page);
    for (std::size_t i = 0; i < count; ++i) {
        void* block = bytes + i * size;
        *static_cast<void**>(block) = centralHeads_[index];
        centralHeads_[index] = block;
    }
}

void MemoryPool::releaseToCentral(std::size_t index, void* ptr) {
    // 本地缓存已满时，把额外块放回全局中央链表。
    std::lock_guard<std::mutex> lock(mutex_);
    *static_cast<void**>(ptr) = centralHeads_[index];
    centralHeads_[index] = ptr;
}

void MemoryPool::releaseLocalCache(ThreadCache& cache) noexcept {
    // 线程退出时把仍缓存的空闲块整体拼接回中央链，避免它们永久困在 TLS 中。
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t index = 0; index < kClassCount; ++index) {
        void* head = cache.heads[index];
        if (head == nullptr) {
            continue;
        }
        void* tail = head;
        while (*static_cast<void**>(tail) != nullptr) {
            tail = *static_cast<void**>(tail);
        }
        *static_cast<void**>(tail) = centralHeads_[index];
        centralHeads_[index] = head;
        cache.heads[index] = nullptr;
        cache.counts[index] = 0;
    }
}

}  // namespace coreactor