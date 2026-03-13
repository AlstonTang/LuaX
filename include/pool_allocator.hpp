#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <array>

#if defined(__GNUC__) || defined(__clang__)
	#define LUA_POOL_LIKELY(x)   __builtin_expect(!!(x), 1)
	#define LUA_POOL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
	#define LUA_POOL_LIKELY(x)   (x)
	#define LUA_POOL_UNLIKELY(x) (x)
#endif

class LuaObjectPool {
public:
	// Increased alignment to max_align_t to ensure safety for all standard types
	static constexpr std::size_t ALIGNMENT = alignof(std::max_align_t);
	static constexpr std::size_t BUCKET_COUNT = 8;
	static constexpr std::size_t BUCKET_SIZES[BUCKET_COUNT] = {32, 64, 96, 128, 192, 256, 512, 1024};

	[[nodiscard]] static void* allocate(std::size_t n) {
		// Fallback to global new if TLS is dead or size is too large
		if (LUA_POOL_UNLIKELY(is_destroyed() || n > BUCKET_SIZES[BUCKET_COUNT - 1])) {
			return ::operator new(n);
		}

		const std::size_t index = get_bucket_index(n);
		auto& pool = get_thread_pool();
		
		if (pool.buckets[index] != nullptr) {
			FreeNode* node = pool.buckets[index];
			pool.buckets[index] = node->next;
			return static_cast<void*>(node);
		}

		// Allocate a new chunk of the specific bucket size
		return ::operator new(BUCKET_SIZES[index]);
	}

	static void deallocate(void* p, std::size_t n) noexcept {
		if (LUA_POOL_UNLIKELY(!p)) return;

		// If TLS is destroyed, we cannot access the buckets; return to OS
		if (LUA_POOL_UNLIKELY(is_destroyed() || n > BUCKET_SIZES[BUCKET_COUNT - 1])) {
			::operator delete(p);
			return;
		}

		const std::size_t index = get_bucket_index(n);
		auto& pool = get_thread_pool();

		// Intrusive linked list: Store the 'next' pointer in the freed memory itself
		FreeNode* node = static_cast<FreeNode*>(p);
		node->next = pool.buckets[index];
		pool.buckets[index] = node;
	}

	// Restored cleanup method
	static void cleanup() noexcept {
		if (LUA_POOL_UNLIKELY(is_destroyed())) return;
		
		auto& pool = get_thread_pool();
		for (std::size_t i = 0; i < BUCKET_COUNT; ++i) {
			FreeNode* current = pool.buckets[i];
			while (current) {
				FreeNode* next = current->next;
				::operator delete(current);
				current = next;
			}
			pool.buckets[i] = nullptr;
		}
	}

private:
	struct FreeNode {
		FreeNode* next;
	};

	struct ThreadPool {
		// Using raw pointers for a free-list to avoid std::vector overhead
		FreeNode* buckets[BUCKET_COUNT]{nullptr};

		~ThreadPool() {
			is_destroyed() = true;
			for (std::size_t i = 0; i < BUCKET_COUNT; ++i) {
				FreeNode* current = buckets[i];
				while (current) {
					FreeNode* next = current->next;
					::operator delete(current);
					current = next;
				}
				buckets[i] = nullptr;
			}
		}
	};

	static inline std::size_t get_bucket_index(std::size_t n) noexcept {
		// Manual unrolling/branching is faster than a loop for 8 constants.
		// This effectively creates a small search tree.
		if (n <= 128) {
			if (n <= 64) return (n <= 32) ? 0 : 1;
			return (n <= 96) ? 2 : 3;
		} else {
			if (n <= 256) return (n <= 192) ? 4 : 5;
			return (n <= 512) ? 6 : 7;
		}
	}

	static bool& is_destroyed() noexcept {
		thread_local bool destroyed = false;
		return destroyed;
	}

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
	template <typename U> PoolAllocator(const PoolAllocator<U>&) noexcept {}

	[[nodiscard]] T* allocate(std::size_t n) {
		if (LUA_POOL_UNLIKELY(n > static_cast<std::size_t>(-1) / sizeof(T))) {
			throw std::bad_alloc();
		}
		return static_cast<T*>(LuaObjectPool::allocate(n * sizeof(T)));
	}

	void deallocate(T* p, std::size_t n) noexcept {
		LuaObjectPool::deallocate(p, n * sizeof(T));
	}
};

template <typename T, typename U>
inline bool operator==(const PoolAllocator<T>&, const PoolAllocator<U>&) noexcept { return true; }
template <typename T, typename U>
inline bool operator!=(const PoolAllocator<T>&, const PoolAllocator<U>&) noexcept { return false; }