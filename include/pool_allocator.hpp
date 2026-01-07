#pragma once
#include <memory>
#include <vector>
#include <cstddef>

// Simple thread-local object pool
// BlockSize covers sizeof(LuaObject) (~80) + shared_ptr control block (~24)
// and small vectors (up to 12-24 elements) and unordered_map nodes.
constexpr size_t POOL_BUCKET_COUNT = 5;
constexpr size_t POOL_BUCKET_SIZES[POOL_BUCKET_COUNT] = {64, 128, 512, 1024, 2048};

class LuaObjectPool {
public:
    static void* allocate(size_t n) {
        for (size_t i = 0; i < POOL_BUCKET_COUNT; ++i) {
            if (n <= POOL_BUCKET_SIZES[i]) {
                auto& pool = get_pool(i);
                if (!pool.empty()) {
                    void* ptr = pool.back();
                    pool.pop_back();
                    return ptr;
                }
                return ::operator new(POOL_BUCKET_SIZES[i]);
            }
        }
        return ::operator new(n);
    }

    static void deallocate(void* p, size_t n) {
        for (size_t i = 0; i < POOL_BUCKET_COUNT; ++i) {
            if (n <= POOL_BUCKET_SIZES[i]) {
                get_pool(i).push_back(p);
                return;
            }
        }
        ::operator delete(p);
    }

private:
    static std::vector<void*>& get_pool(size_t bucket_idx) {
        static thread_local std::vector<void*> pools[POOL_BUCKET_COUNT];
        auto& pool = pools[bucket_idx];
        if (pool.capacity() < 128) pool.reserve(128);
        return pool;
    }
};

template <typename T>
struct PoolAllocator {
    using value_type = T;

    PoolAllocator() = default;
    template <typename U> PoolAllocator(const PoolAllocator<U>&) {}

    T* allocate(std::size_t n) {
        // We allocate raw bytes
        return static_cast<T*>(LuaObjectPool::allocate(n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t n) {
        LuaObjectPool::deallocate(p, n * sizeof(T));
    }
};

template <typename T, typename U>
bool operator==(const PoolAllocator<T>&, const PoolAllocator<U>&) { return true; }
template <typename T, typename U>
bool operator!=(const PoolAllocator<T>&, const PoolAllocator<U>&) { return false; }
