#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

namespace coreactor {

// 面向小对象的三层分配器：线程本地缓存 -> 中央空闲块池 -> 页级批量申请。
// 超过最大 size class 的大块直接交给全局 new/delete。
class MemoryPool {
public:
    static MemoryPool& instance();

    void* allocate(std::size_t bytes);
    void deallocate(void* ptr, std::size_t bytes) noexcept;

private:
    // size class 为 16、32、64、...、4096 字节，共九档。
    static constexpr std::size_t kClassCount = 9;
    static constexpr std::size_t kMinBlockSize = 16;
    static constexpr std::size_t kMaxBlockSize = 4096;
    static constexpr std::size_t kPageSize = 64 * 1024; // 每次向系统申请 64 KiB
    static constexpr std::size_t kLocalCacheLimit = 64;  // 每线程每档最多缓存块数
    static constexpr std::size_t kRefillBatch = 32;      // 中央池一次补给线程缓存的块数

    struct ThreadCache {
        // 空闲块首地址直接存放在块内，避免为链表节点额外分配内存。
        std::array<void*, kClassCount> heads{};
        std::array<std::size_t, kClassCount> counts{};
        ~ThreadCache();
    };

    MemoryPool() = default;
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    static std::size_t classIndex(std::size_t bytes);
    static std::size_t blockSize(std::size_t index);
    static ThreadCache& localCache();

    void refill(ThreadCache& cache, std::size_t index);
    void releaseLocalCache(ThreadCache& cache) noexcept;
    void releaseToCentral(std::size_t index, void* ptr);
    void allocatePageBlocks(std::size_t index);

    std::array<void*, kClassCount> centralHeads_{}; // 每个尺寸档中央空闲链表的头
    std::vector<void*> pages_;                      // 保留页地址，进程结束时由 OS 回收
    std::mutex mutex_;                              // 保护中央链表和 pages_
};

template <typename T>
class PoolAllocator {
public:
    using value_type = T;

    PoolAllocator() noexcept = default;

    template <typename U>
    PoolAllocator(const PoolAllocator<U>&) noexcept {}

    // STL 容器申请 count 个 T 时，由 allocator 转换成字节数交给内存池。
    T* allocate(std::size_t count) {
        if (count > static_cast<std::size_t>(-1) / sizeof(T)) {
            throw std::bad_array_new_length();
        }
        return static_cast<T*>(MemoryPool::instance().allocate(count * sizeof(T)));
    }

    // 容器释放时把原始数量传回来，以便内存池找到对应 size class。
    void deallocate(T* ptr, std::size_t count) noexcept {
        MemoryPool::instance().deallocate(ptr, count * sizeof(T));
    }

    template <typename U>
    bool operator==(const PoolAllocator<U>&) const noexcept { return true; }

    template <typename U>
    bool operator!=(const PoolAllocator<U>&) const noexcept { return false; }
};

}  // namespace coreactor