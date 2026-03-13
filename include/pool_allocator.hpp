#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>

class LuaObjectPool {
public:
    static constexpr std::size_t BUCKET_COUNT = 8;
    static constexpr std::size_t BUCKET_SIZES[BUCKET_COUNT] = {32, 64, 96, 128, 192, 256, 512, 1024};

    [[nodiscard]] static void* allocate(std::size_t n) {
        // If thread is shutting down, skip the pool to prevent accessing dead vectors
        if (is_destroyed()) {
            return ::operator new(n);
        }

        for (std::size_t i = 0; i < BUCKET_COUNT; ++i) {
            if (n <= BUCKET_SIZES[i]) {
                auto& pool = get_thread_pool().buckets[i];
                if (!pool.empty()) {
                    void* ptr = pool.back();
                    pool.pop_back();
                    return ptr;
                }
                return ::operator new(BUCKET_SIZES[i]);
            }
        }
        return ::operator new(n);
    }

    static void deallocate(void* p, std::size_t n) noexcept {
        if (!p) return;

        // CRITICAL FIX: If the ThreadPool is destroyed, vectors are dead. 
        // Send memory directly to the OS to avoid memory corruption.
        if (is_destroyed()) {
            ::operator delete(p);
            return;
        }

        for (std::size_t i = 0; i < BUCKET_COUNT; ++i) {
            if (n <= BUCKET_SIZES[i]) {
                auto& pool = get_thread_pool().buckets[i];
                try {
                    pool.push_back(p);
                } catch (...) {
                    ::operator delete(p);
                }
                return;
            }
        }
        ::operator delete(p);
    }

    static void cleanup() noexcept {
        if (is_destroyed()) return;
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
    // This flag ensures we know when C++ has destroyed our ThreadPool object.
    static bool& is_destroyed() {
        thread_local bool destroyed = false;
        return destroyed;
    }

    struct ThreadPool {
        std::vector<void*> buckets[BUCKET_COUNT];

        ThreadPool() {
            for (auto& b : buckets) {
                b.reserve(128);
            }
        }

        ~ThreadPool() {
            // Immediately mark as destroyed so subsequent destructors bypass us
            is_destroyed() = true; 
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
    using is_always_equal = std::true_type; 

    PoolAllocator() noexcept = default;
    
    template <typename U> 
    PoolAllocator(const PoolAllocator<U>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t n) {
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