#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>

class LuaObjectPool {
public:
    // Moved constants inside the class to avoid polluting the global namespace
    static constexpr std::size_t BUCKET_COUNT = 8;
    static constexpr std::size_t BUCKET_SIZES[BUCKET_COUNT] = {32, 64, 96, 128, 192, 256, 512, 1024};

    [[nodiscard]] static void* allocate(std::size_t n) {
        for (std::size_t i = 0; i < BUCKET_COUNT; ++i) {
            if (n <= BUCKET_SIZES[i]) {
                auto& pool = get_thread_pool().buckets[i];
                if (!pool.empty()) {
                    void* ptr = pool.back();
                    pool.pop_back();
                    return ptr;
                }
                // Allocate the full bucket size to allow safe, predictable reuse
                return ::operator new(BUCKET_SIZES[i]);
            }
        }
        return ::operator new(n);
    }

    static void deallocate(void* p, std::size_t n) noexcept {
        if (!p) return;

        for (std::size_t i = 0; i < BUCKET_COUNT; ++i) {
            if (n <= BUCKET_SIZES[i]) {
                auto& pool = get_thread_pool().buckets[i];
                try {
                    pool.push_back(p);
                } catch (...) {
                    // std::vector::push_back can throw std::bad_alloc.
                    // If we cannot store the pointer for reuse, we must gracefully delete it.
                    ::operator delete(p);
                }
                return;
            }
        }
        ::operator delete(p);
    }

    static void cleanup() noexcept {
        auto& pool = get_thread_pool();
        for (std::size_t i = 0; i < BUCKET_COUNT; ++i) {
            auto& bucket = pool.buckets[i];
            for (void* ptr : bucket) {
                ::operator delete(ptr);
            }
            bucket.clear();
        }
    }

private:
    // A wrapper object ensures that when a thread exits, the pooled memory is actually freed.
    // The original code leaked the allocations because the vector destructor only frees the pointers.
    struct ThreadPool {
        std::vector<void*> buckets[BUCKET_COUNT];

        ThreadPool() {
            // Reserve capacity once at thread startup, removing branch overhead in get_pool()
            for (auto& b : buckets) {
                b.reserve(128);
            }
        }

        ~ThreadPool() {
            for (auto& b : buckets) {
                for (void* ptr : b) {
                    ::operator delete(ptr);
                }
            }
        }
    };

    static ThreadPool& get_thread_pool() {
        thread_local ThreadPool pool;
        return pool;
    }
};

template <typename T>
struct PoolAllocator {
    using value_type = T;
    // Signals to STL containers that allocators of this type can be safely swapped/moved
    using is_always_equal = std::true_type; 

    PoolAllocator() noexcept = default;
    
    template <typename U> 
    PoolAllocator(const PoolAllocator<U>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t n) {
        // Prevent multiplication integer overflow
        if (n > static_cast<std::size_t>(-1) / sizeof(T)) {
            throw std::bad_alloc();
        }
        return static_cast<T*>(LuaObjectPool::allocate(n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t n) noexcept {
        LuaObjectPool::deallocate(p, n * sizeof(T));
    }
};

template <typename T, typename U>
inline bool operator==(const PoolAllocator<T>&, const PoolAllocator<U>&) noexcept { 
    return true; 
}

template <typename T, typename U>
inline bool operator!=(const PoolAllocator<T>&, const PoolAllocator<U>&) noexcept { 
    return false; 
}