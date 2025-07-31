/* this file is a part of Acheron library which is under MIT license; see LICENSE for more info */

/* this file is edited from https://github.com/deviceix/acheron/blob/main/include/acheron/__memory/allocator.hpp
* by Al (@alpluspluss) */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <thread>
#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))
/* we compile for Unix e.g. Linux, macOS and so on */
#include <sys/mman.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else /* not supported at the moment, but will be soon; hopefully */
#error "your OS not supported at the moment, but will be soon; hopefully"
#endif

namespace ach
{
	/**
	 * @brief Allocation policy for the allocator
	 */
	enum class AllocationPolicy
	{
		THREAD_LOCAL, /* fast thread-local allocation; also the default */
		SHARED        /* thread-safe shared allocation */
	};

	/**
	 * @brief Memory allocator with pool-based allocation strategy
	 *
	 * @note This allocator implements the C++ standard allocator interface while providing
	 *  optimized memory allocation through size-based pools for small allocations and
	 *  direct mmap for larger ones.
	 * @tparam T Type of objects to allocate
	 * @tparam Policy Allocation policy. Default to `AllocationPolicy::THREAD_LOCAL` for fast
	 */
	template<typename T, AllocationPolicy Policy = AllocationPolicy::THREAD_LOCAL>
	class allocator
	{
	public:
		using value_type = T;
		using pointer = T *;
		using const_pointer = const T *;
		using reference = T &;
		using const_reference = const T &;
		using size_type = std::size_t;
		using difference_type = std::ptrdiff_t;
		using propagate_on_container_move_assignment = std::true_type;
		using is_always_equal = std::true_type;

		/**
		 * @brief Rebinds the allocator to a different type
		 */
		template<typename U>
		struct rebind
		{
			using other = allocator<U, Policy>;
		};

		/**
		 * @brief Default constructor
		 */
		constexpr allocator() noexcept = default;

		/**
		 * @brief Copy constructor
		 */
		constexpr allocator(const allocator &other) noexcept = default;

		/**
		 * @brief Converting constructor
		 */
		template<typename U>
		constexpr allocator(const allocator<U, Policy> &) noexcept {}

		/**
		 * @brief Allocate memory for n objects of type T
		 */
		[[nodiscard]] pointer allocate(size_type n)
		{
			if (n == 0)
				return nullptr;

			const size_type bytes_needed = n * sizeof(T);
			void *result = nullptr;

			if (bytes_needed >= LARGE_THRESHOLD)
				result = allocate_large(bytes_needed);
			else
				result = allocate_from_size_class(get_size_class(bytes_needed));

			if (!result)
				throw std::bad_alloc();

			return static_cast<pointer>(result);
		}

		/**
		 * @brief Deallocate memory previously allocated with allocate
		 */
		void deallocate(pointer p, size_type) noexcept
		{
			if (!p)
				return;

			if (!BlockHeader::is_aligned(p))
				return;

			auto *header = reinterpret_cast<BlockHeader *>(
				reinterpret_cast<unsigned char *>(p) - sizeof(BlockHeader)
			);

			if (!header->is_valid())
				return;

			if (header->is_mmap())
			{
				const std::size_t total_size = header->size() + sizeof(BlockHeader);
				const std::size_t aligned_size = (total_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))
				munmap(header, aligned_size);
#elif defined(_WIN32) || defined(_WIN64)
				VirtualFree(header, 0, MEM_RELEASE);
#endif
				return;
			}

			std::uint8_t size_class = header->size_class();
			if (size_class >= SIZE_CLASSES)
				return;

			header->set_free(true);

			if constexpr (Policy == AllocationPolicy::THREAD_LOCAL)
			{
				/* we assume fully thread-safe access for thread local allocations */
				auto &state = tl_state();
				header->next = state.free_lists[size_class];
				state.free_lists[size_class] = header;
			}
			else
			{
				auto &state = shared_state();
				auto expected =
						state.free_lists[size_class].load(std::memory_order_acquire);
				do
				{
					header->next = expected;
				}
				while (!state.free_lists[size_class].compare_exchange_weak(
					expected, header, std::memory_order_release, std::memory_order_acquire));
			}
		}

		/**
		 * @brief Returns the maximum number of objects that can be allocated
		 */
		[[nodiscard]] size_type max_size() const noexcept
		{
			return std::numeric_limits<size_type>::max() / sizeof(T);
		}

		/**
		 * @brief Construct an object at the given address
		 */
		template<typename U, typename... Args>
		void construct(U *p, Args &&... args) noexcept(std::is_nothrow_constructible_v<U, Args...>)
		{
			::new(static_cast<void *>(p)) U(std::forward<Args>(args)...);
		}

		/**
		 * @brief Destroy an object at the given address
		 */
		template<typename U>
		void destroy(U *p) noexcept(std::is_nothrow_destructible_v<U>)
		{
			p->~U();
		}

	private:
		static constexpr std::size_t PAGE_SIZE = 4096;
		static constexpr std::size_t ALIGNMENT = alignof(T);
		static constexpr std::size_t LARGE_THRESHOLD = 1024 * 1024;
		static constexpr std::size_t SIZE_CLASSES = 32;
		static constexpr std::uint64_t SIZE_MASK = 0x0000FFFFFFFFFFFF;
		static constexpr std::uint64_t CLASS_MASK = 0x00FF000000000000;
		static constexpr std::uint64_t FREE_FLAG = 1ULL << 63;
		static constexpr std::uint64_t MMAP_FLAG = 1ULL << 62;
		static constexpr auto HEADER_MAGIC = 0xDEADBEEF12345678;
		static constexpr auto MAGIC_VALUE = 0xA000000000000000;

		class BlockHeader
		{
		public:
			std::uint64_t data;
			std::uint64_t magic;
			BlockHeader *next;

			void init(const std::size_t size, std::uint8_t size_class, const bool is_free)
			{
				magic = HEADER_MAGIC;
				data = (size & SIZE_MASK) |
				       (static_cast<std::uint64_t>(size_class) << 48) |
				       (static_cast<std::uint64_t>(is_free) << 63) |
				       MAGIC_VALUE;
				next = nullptr;
			}

			[[nodiscard]] bool is_valid() const
			{
				return (magic == HEADER_MAGIC) && (size() <= (1ULL << 47));
			}

			[[nodiscard]] bool is_free() const
			{
				return (data & FREE_FLAG) != 0;
			}

			[[nodiscard]] bool is_mmap() const
			{
				return (data & MMAP_FLAG) != 0;
			}

			[[nodiscard]] size_t size() const
			{
				return data & SIZE_MASK;
			}

			[[nodiscard]] uint8_t size_class() const
			{
				return (data & CLASS_MASK) >> 48;
			}

			void set_free(const bool is_free)
			{
				data = (data & ~FREE_FLAG) | (static_cast<uint64_t>(is_free) << 63);
			}

			void set_mmap(const bool is_mmap)
			{
				data = (data & ~MMAP_FLAG) | (static_cast<uint64_t>(is_mmap) << 62);
			}

			static bool is_aligned(const void *ptr)
			{
				if ((reinterpret_cast<uintptr_t>(ptr) & (ALIGNMENT - 1)) != 0)
					return false;

				const auto *header = reinterpret_cast<const BlockHeader *>(
					static_cast<const char *>(ptr) - sizeof(BlockHeader));

				return header->magic == HEADER_MAGIC;
			}
		};

		struct SizeClass
		{
			std::uint16_t size;
			std::uint16_t slot;
			std::uint16_t blocks;
		};

		class Pool
		{
		public:
			std::size_t cap;
			BlockHeader *free_list;
			Pool *next;
			std::uint8_t *memory;

			explicit Pool(std::uint8_t *mem, const size_t size) : cap(size), free_list(nullptr), next(nullptr),
			                                                      memory(mem) {}

			~Pool() = default;
		};

		struct ThreadLocalState
		{
			SizeClass size_classes[SIZE_CLASSES];
			BlockHeader *free_lists[SIZE_CLASSES];
			Pool *pools[SIZE_CLASSES];

			ThreadLocalState()
			{
				for (std::size_t i = 0; i < SIZE_CLASSES; ++i)
				{
					const std::size_t size = 1ULL << (i + 3);
					const std::size_t alignment = (size > ALIGNMENT) ? size : ALIGNMENT;
					constexpr auto header_size = sizeof(BlockHeader);
					const std::size_t padding = (alignment - (header_size % alignment)) % alignment;
					const std::size_t slot_size = (size + header_size + padding + alignment - 1) & ~(alignment - 1);

					size_classes[i].size = static_cast<std::uint16_t>(size);
					size_classes[i].slot = static_cast<std::uint16_t>(slot_size);
					size_classes[i].blocks = PAGE_SIZE / slot_size;
				}

				for (std::size_t i = 0; i < SIZE_CLASSES; ++i)
				{
					free_lists[i] = nullptr;
					pools[i] = nullptr;
				}
			}

			~ThreadLocalState()
			{
				for (auto pool: pools)
				{
					while (pool)
					{
						Pool *next = pool->next;
						std::destroy_at(pool);

						constexpr auto pool_size = sizeof(Pool);
						constexpr auto pool_align = alignof(Pool);
						const std::size_t aligned_pool_size = (pool_size + pool_align - 1) & ~(pool_align - 1);
						const std::size_t total_size = PAGE_SIZE + aligned_pool_size;

						void *original_mem = reinterpret_cast<void *>(
							reinterpret_cast<std::uintptr_t>(pool->memory) - aligned_pool_size
						);

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))
						munmap(original_mem, total_size);
#elif defined(_WIN32) || defined(_WIN64)
						VirtualFree(original_mem, 0, MEM_RELEASE);
#endif
						pool = next;
					}
				}
			}
		};

		struct SharedState
		{
			SizeClass size_classes[SIZE_CLASSES];
			std::atomic<BlockHeader *> free_lists[SIZE_CLASSES];
			std::atomic<Pool *> pools[SIZE_CLASSES];
			std::atomic<bool> pool_allocation_in_progress[SIZE_CLASSES];

			SharedState()
			{
				for (std::size_t i = 0; i < SIZE_CLASSES; ++i)
				{
					const std::size_t size = 1ULL << (i + 3);
					const std::size_t alignment = (size > ALIGNMENT) ? size : ALIGNMENT;
					constexpr auto header_size = sizeof(BlockHeader);
					const std::size_t padding = (alignment - (header_size % alignment)) % alignment;
					const std::size_t slot_size = (size + header_size + padding + alignment - 1) & ~(alignment - 1);

					size_classes[i].size = static_cast<std::uint16_t>(size);
					size_classes[i].slot = static_cast<std::uint16_t>(slot_size);
					size_classes[i].blocks = PAGE_SIZE / slot_size;
				}

				for (std::size_t i = 0; i < SIZE_CLASSES; ++i)
				{
					free_lists[i].store(nullptr, std::memory_order_relaxed);
					pools[i].store(nullptr, std::memory_order_relaxed);
					pool_allocation_in_progress[i].store(false, std::memory_order_relaxed);
				}
			}

			~SharedState()
			{
				for (std::size_t i = 0; i < SIZE_CLASSES; ++i)
				{
					Pool *pool = pools[i].load(std::memory_order_acquire);
					while (pool)
					{
						Pool *next_pool = pool->next;
						std::destroy_at(pool);

						constexpr auto pool_size = sizeof(Pool);
						constexpr auto pool_align = alignof(Pool);
						const std::size_t aligned_pool_size = (pool_size + pool_align - 1) & ~(pool_align - 1);
						const std::size_t total_size = PAGE_SIZE + aligned_pool_size;

						void *original_mem = reinterpret_cast<void *>(
							reinterpret_cast<std::uintptr_t>(pool->memory) - aligned_pool_size
						);

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))
						munmap(original_mem, total_size);
#elif defined(_WIN32) || defined(_WIN64)
						VirtualFree(original_mem, 0, MEM_RELEASE);
#endif
						pool = next_pool;
					}
				}
			}
		};

		static ThreadLocalState &tl_state()
		{
			thread_local ThreadLocalState state;
			return state;
		}

		static SharedState &shared_state()
		{
			static SharedState state;
			return state;
		}

		static std::uint8_t get_size_class(size_t size)
		{
			if (size <= 64)
				return static_cast<std::uint8_t>((size - 1) >> 3);

			std::size_t n = size - 1;
			n |= n >> 1;
			n |= n >> 2;
			n |= n >> 4;
			n |= n >> 8;
			n |= n >> 16;
			n |= n >> 32;

			const auto class_index = static_cast<std::uint8_t>((63 - __builtin_clzll(n + 1)) - 3);
			if (class_index >= SIZE_CLASSES)
				return SIZE_CLASSES - 1;

			return class_index;
		}

		static void allocate_pool_thread_local(std::uint8_t size_class)
		{
			auto &state = tl_state();
			constexpr auto pool_size = sizeof(Pool);
			constexpr auto pool_align = alignof(Pool);
			const std::size_t aligned_pool_size = (pool_size + pool_align - 1) & ~(pool_align - 1);
			const std::size_t total_size = PAGE_SIZE + aligned_pool_size;

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))
			void *mem = mmap(nullptr, total_size, PROT_READ | PROT_WRITE,
			                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (mem == MAP_FAILED)
				throw std::bad_alloc();
#elif defined(_WIN32) || defined(_WIN64)
			void *mem = VirtualAlloc(nullptr, total_size,
									MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
			if (!mem)
				throw std::bad_alloc();
#endif

			auto aligned_ptr = reinterpret_cast<std::uintptr_t>(mem);
			aligned_ptr = (aligned_ptr + pool_align - 1) & ~(pool_align - 1);

			Pool *new_pool = std::construct_at(reinterpret_cast<Pool *>(aligned_ptr),
			                                   static_cast<std::uint8_t *>(mem) + aligned_pool_size,
			                                   PAGE_SIZE);
			new_pool->next = state.pools[size_class];
			state.pools[size_class] = new_pool;

			const SizeClass &sc = state.size_classes[size_class];
			const std::size_t block_size = sc.slot;
			const std::size_t num_blocks = sc.blocks;

			for (std::size_t i = 0; i < num_blocks; ++i)
			{
				auto *header = reinterpret_cast<BlockHeader *>(new_pool->memory + i * block_size);
				header->init(sc.size, size_class, true);
				header->next = state.free_lists[size_class];
				state.free_lists[size_class] = header;
			}
		}

		static void allocate_pool_shared(std::uint8_t size_class)
		{
			auto &state = shared_state();
			if (auto expected = false;
				!state.pool_allocation_in_progress[size_class].compare_exchange_strong(
				expected, true, std::memory_order_acquire))
			{
				/* another thread is allocating, spin until done */
				while (state.pool_allocation_in_progress[size_class].load(std::memory_order_acquire))
				{
					std::this_thread::yield();
				}
				return;
			}

			/* double-check that we still need a pool */
			if (state.free_lists[size_class].load(std::memory_order_acquire) != nullptr)
			{
				state.pool_allocation_in_progress[size_class].store(false, std::memory_order_release);
				return;
			}

			/* allocate a new pool */
			constexpr auto pool_size = sizeof(Pool);
			constexpr auto pool_align = alignof(Pool);
			const std::size_t aligned_pool_size = (pool_size + pool_align - 1) & ~(pool_align - 1);
			const std::size_t total_size = PAGE_SIZE + aligned_pool_size;

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))
			void *mem = mmap(nullptr, total_size, PROT_READ | PROT_WRITE,
			                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (mem == MAP_FAILED)
			{
				state.pool_allocation_in_progress[size_class].store(false, std::memory_order_release);
				throw std::bad_alloc();
			}
#elif defined(_WIN32) || defined(_WIN64)
			void *mem = VirtualAlloc(nullptr, total_size,
									MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
			if (!mem)
			{
				state.pool_allocation_in_progress[size_class].store(false, std::memory_order_release);
				throw std::bad_alloc();
			}
#endif

			auto aligned_ptr = reinterpret_cast<std::uintptr_t>(mem);
			aligned_ptr = (aligned_ptr + pool_align - 1) & ~(pool_align - 1);

			Pool *new_pool = std::construct_at(reinterpret_cast<Pool *>(aligned_ptr),
			                                   static_cast<std::uint8_t *>(mem) + aligned_pool_size,
			                                   PAGE_SIZE);

			/* lock-free prepend to pool list */
			Pool *expected_pool = state.pools[size_class].load(std::memory_order_acquire);
			do
			{
				new_pool->next = expected_pool;
			}
			while (!state.pools[size_class].compare_exchange_weak(
				expected_pool, new_pool, std::memory_order_release, std::memory_order_acquire));

			const SizeClass &sc = state.size_classes[size_class];
			const std::size_t block_size = sc.slot;
			const std::size_t num_blocks = sc.blocks;

			/* build free list and atomically add to global list */
			BlockHeader *first_header = nullptr;
			BlockHeader *prev_header = nullptr;
			for (std::size_t i = 0; i < num_blocks; ++i)
			{
				auto *header = reinterpret_cast<BlockHeader *>(new_pool->memory + i * block_size);
				header->init(sc.size, size_class, true);

				if (prev_header)
					prev_header->next = header;
				else
					first_header = header;

				prev_header = header;
			}

			if (prev_header && first_header)
			{
				BlockHeader* expected_free =
						state.free_lists[size_class].load(std::memory_order_acquire);
				do
				{
					prev_header->next = expected_free;
				}
				while (!state.free_lists[size_class].compare_exchange_weak(
					expected_free, first_header, std::memory_order_release, std::memory_order_acquire));
			}

			state.pool_allocation_in_progress[size_class].store(false, std::memory_order_release);
		}

		static void *allocate_from_size_class(uint8_t size_class)
		{
			if constexpr (Policy == AllocationPolicy::THREAD_LOCAL)
			{
				auto &state = tl_state();
				if (!state.free_lists[size_class])
					allocate_pool_thread_local(size_class);

				if (state.free_lists[size_class])
				{
					BlockHeader *header = state.free_lists[size_class];
					state.free_lists[size_class] = header->next;
					header->set_free(false);
					return reinterpret_cast<char *>(header) + sizeof(BlockHeader);
				}
			}
			else
			{
				auto &state = shared_state();
				BlockHeader* header =
						state.free_lists[size_class].load(std::memory_order_acquire);
				while (header)
				{
					BlockHeader *next = header->next;
					if (state.free_lists[size_class].compare_exchange_weak(
						header, next, std::memory_order_release, std::memory_order_acquire))
					{
						header->set_free(false);
						return reinterpret_cast<char *>(header) + sizeof(BlockHeader);
					}
					/* RETRY: CAS fails */
				}

				/* no free blocks, allocate new pool and retry */
				allocate_pool_shared(size_class);

				header = state.free_lists[size_class].load(std::memory_order_acquire);
				while (header)
				{
					BlockHeader *next = header->next;
					if (state.free_lists[size_class].compare_exchange_weak(
						header, next, std::memory_order_release, std::memory_order_acquire))
					{
						header->set_free(false);
						return reinterpret_cast<char *>(header) + sizeof(BlockHeader);
					}
				}
			}

			return nullptr;
		}

		static void *allocate_large(size_t size)
		{
			constexpr auto header_size = sizeof(BlockHeader);
			const std::size_t total_size = size + header_size;
			const std::size_t aligned_size = (total_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))
			void *mem = mmap(nullptr, aligned_size,
			                 PROT_READ | PROT_WRITE,
			                 MAP_PRIVATE | MAP_ANONYMOUS,
			                 -1, 0);
			if (mem == MAP_FAILED)
				return nullptr;
#elif defined(_WIN32) || defined(_WIN64)
            void *mem = VirtualAlloc(nullptr, aligned_size,
                                    MEM_COMMIT | MEM_RESERVE,
                                    PAGE_READWRITE);
            if (!mem)
                return nullptr;
#endif

			auto *header = static_cast<BlockHeader *>(mem);
			header->init(size, 255, false);
			header->set_mmap(true);
			return static_cast<char *>(mem) + header_size;
		}
	};

	/* convenience aliases for common use cases */
	template<typename T>
	using shared_allocator = allocator<T, AllocationPolicy::SHARED>;

	template<typename T>
	using local_allocator = allocator<T>; /* already defaults to AllocationPolicy::THREAD_LOCAL */

	template<typename T1, typename T2, AllocationPolicy P1, AllocationPolicy P2>
	bool operator==(const allocator<T1, P1> &, const allocator<T2, P2> &) noexcept
	{
		return std::is_same_v<T1, T2> && P1 == P2;
	}

	template<typename T1, typename T2, AllocationPolicy P1, AllocationPolicy P2>
	bool operator!=(const allocator<T1, P1> &lhs, const allocator<T2, P2> &rhs) noexcept
	{
		return !(lhs == rhs);
	}
}
