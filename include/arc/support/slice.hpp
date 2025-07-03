/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <arc/support/allocator.hpp>

namespace arc
{
	/**
	 *	@brief Lightweight dynamic array with configurable size type.
	 *
	 *	slice is a compact alternative to `std::vector` that uses a smaller
	 *	fixed-width size type and assumes stateless allocators for maximum
	 *	memory efficiency.
	 *
	 *	@tparam T Type of elements stored in the slice.
	 *	@tparam SizeType Size/capacity type. Defaults to `std::uint16_t`.
	 *	@tparam Allocator Allocator type. Must be stateless. Defaults to `ach::allocator<T>`.
	 */
	template<typename T,
			typename SizeType = std::uint16_t,
			typename Allocator = ach::allocator<T>>
		requires(std::is_unsigned_v<SizeType>)
	class slice
	{
	public:
		using value_type = T;
		using size_type = SizeType;
		using difference_type = std::ptrdiff_t;
		using reference = T&;
		using const_reference = const T&;
		using pointer = T*;
		using const_pointer = const T*;
		using iterator = T*;
		using const_iterator = const T*;
		using reverse_iterator = std::reverse_iterator<iterator>;
		using const_reverse_iterator = std::reverse_iterator<const_iterator>;
		using allocator_type = Allocator;

		/**
		 *	@brief Default constructor.
		 */
		constexpr slice() noexcept : dt(nullptr), sz(0), cap(0) {}

		/**
		 * @brief Constructor from raw pointer and size (non-owning view)
		 */
		slice(pointer data, size_type count) noexcept
			: dt(data), sz(count), cap(count) {}

		/**
		 *	@brief Constructor with initial size.
		 */
		explicit slice(size_type n, const allocator_type& = allocator_type())
			: dt(nullptr), sz(0), cap(0)
		{
			resize(n);
		}

		/**
		 *	@brief Constructor with size and fill value.
		 */
		slice(size_type n, const value_type& value, const allocator_type& = allocator_type())
			: dt(nullptr), sz(0), cap(0)
		{
			assign(n, value);
		}

		/**
		 *	@brief Constructor from iterator range
		 */
		template<typename InputIt>
		slice(InputIt first, InputIt last, const allocator_type& = allocator_type())
			: dt(nullptr), sz(0), cap(0)
		{
			assign(first, last);
		}

		/**
		 *	@brief Constructor from initializer list
		 */
		slice(std::initializer_list<T> init, const allocator_type& = allocator_type())
			: dt(nullptr), sz(0), cap(0)
		{
			assign(init);
		}

		/**
		 *	@brief Copy constructor
		 */
		slice(const slice& other)
			: dt(nullptr), sz(0), cap(0)
		{
			assign(other.begin(), other.end());
		}

		/**
		 *	@brief Move constructor
		 */
		slice(slice&& other) noexcept
			: dt(other.dt), sz(other.sz), cap(other.cap)
		{
			other.dt = nullptr;
			other.sz = 0;
			other.cap = 0;
		}

		/**
		 *	@brief Destructor
		 */
		~slice()
		{
			clear();
			if (dt)
			{
				allocator_type alloc;
				alloc.deallocate(dt, cap);
			}
		}

		/**
		 * @brief Copy assignment
		 */
		slice& operator=(const slice& other)
		{
			if (this != &other)
				assign(other.begin(), other.end());
			return *this;
		}

		/**
		 * @brief Move assignment
		 */
		slice& operator=(slice&& other) noexcept
		{
			if (this != &other)
			{
				clear();
				if (dt) {
					allocator_type alloc;
					alloc.deallocate(dt, cap);
				}

				dt = other.dt;
				sz = other.sz;
				cap = other.cap;

				other.dt = nullptr;
				other.sz = 0;
				other.cap = 0;
			}
			return *this;
		}

		/**
		 * @brief Assignment from initializer list
		 */
		slice& operator=(std::initializer_list<T> init)
		{
			assign(init);
			return *this;
		}

		reference at(size_type pos)
		{
			if (pos >= sz)
				throw std::out_of_range("slice::at");
			return dt[pos];
		}

		const_reference at(size_type pos) const
		{
			if (pos >= sz)
				throw std::out_of_range("slice::at");
			return dt[pos];
		}

		reference operator[](size_type pos) noexcept
		{
			return dt[pos];
		}

		const_reference operator[](size_type pos) const noexcept
		{
			return dt[pos];
		}

		reference front() noexcept
		{
			return dt[0];
		}

		const_reference front() const noexcept
		{
			return dt[0];
		}

		reference back() noexcept
		{
			return dt[sz - 1];
		}

		const_reference back() const noexcept
		{
			return dt[sz - 1];
		}

		pointer data() noexcept
		{
			return dt;
		}

		const_pointer data() const noexcept
		{
			return dt;
		}

		iterator begin() noexcept
		{
			return dt;
		}

		const_iterator begin() const noexcept
		{
			return dt;
		}

		const_iterator cbegin() const noexcept
		{
			return dt;
		}

		iterator end() noexcept
		{
			return dt + sz;
		}

		const_iterator end() const noexcept
		{
			return dt + sz;
		}

		const_iterator cend() const noexcept
		{
			return dt + sz;
		}

		reverse_iterator rbegin() noexcept
		{
			return reverse_iterator(end());
		}

		const_reverse_iterator rbegin() const noexcept
		{
			return const_reverse_iterator(end());
		}

		const_reverse_iterator crbegin() const noexcept
		{
			return const_reverse_iterator(end());
		}

		reverse_iterator rend() noexcept
		{
			return reverse_iterator(begin());
		}

		const_reverse_iterator rend() const noexcept
		{
			return const_reverse_iterator(begin());
		}

		const_reverse_iterator crend() const noexcept
		{
			return const_reverse_iterator(begin());
		}

		[[nodiscard]] bool empty() const noexcept
		{
			return sz == 0;
		}

		size_type size() const noexcept
		{
			return sz;
		}

		size_type max_size() const noexcept
		{
			return std::numeric_limits<size_type>::max();
		}

		size_type capacity() const noexcept
		{
			return cap;
		}

		void reserve(size_type new_cap)
		{
			if (new_cap > cap)
				reallocate(new_cap);
		}

		void shrink_to_fit()
		{
			if (cap > sz)
				reallocate(sz);
		}

		void clear() noexcept
		{
			allocator_type alloc;
			for (size_type i = 0; i < sz; ++i)
				alloc.destroy(&dt[i]);
			sz = 0;
		}

		iterator insert(const_iterator pos, const value_type& value)
		{
			return insert(pos, 1, value);
		}

		iterator insert(const_iterator pos, T&& value)
		{
			const auto index = static_cast<size_type>(pos - cbegin());
			emplace(pos, std::move(value));
			return begin() + index;
		}

		iterator insert(const_iterator pos, size_type count, const value_type& value)
		{
			const auto index = static_cast<size_type>(pos - cbegin());
			insert_impl(index, count, value);
			return begin() + index;
		}

		template<typename InputIt>
			requires(std::input_iterator<InputIt>)
		iterator insert(const_iterator pos, InputIt first, InputIt last)
		{
			const auto index = static_cast<size_type>(pos - cbegin());
			insert_range_impl(index, first, last);
			return begin() + index;
		}

		template<typename... Args>
		iterator emplace(const_iterator pos, Args&&... args)
		{
			const auto index = static_cast<size_type>(pos - cbegin());
			emplace_impl(index, std::forward<Args>(args)...);
			return begin() + index;
		}

		iterator erase(const_iterator pos)
		{
			return erase(pos, pos + 1);
		}

		iterator erase(const_iterator first, const_iterator last)
		{
			const auto start_idx = static_cast<size_type>(first - cbegin());
			const auto end_idx = static_cast<size_type>(last - cbegin());
			erase_impl(start_idx, end_idx);
			return begin() + start_idx;
		}

		void push_back(const T& value)
		{
			emplace_back(value);
		}

		void push_back(T&& value)
		{
			emplace_back(std::move(value));
		}

		template<typename... Args>
		reference emplace_back(Args&&... args)
		{
			if (sz == cap)
			{
				const size_type new_cap = cap == 0 ? 1 : cap * 2;
				reserve(new_cap);
			}

			allocator_type alloc;
			alloc.construct(&dt[sz], std::forward<Args>(args)...);
			++sz;
			return back();
		}

		void pop_back() noexcept
		{
			--sz;
			allocator_type alloc;
			alloc.destroy(&dt[sz]);
		}

		void resize(size_type count)
		{
			resize(count, T{});
		}

		void resize(size_type n, const T& value)
		{
			if (n > sz)
			{
				if (n > cap)
					reserve(n);
				allocator_type alloc;
				for (size_type i = sz; i < n; ++i)
					alloc.construct(&dt[i], value);
			}
			else
			{
				allocator_type alloc;
				for (size_type i = n; i < sz; ++i)
					alloc.destroy(&dt[i]);
			}
			sz = n;
		}

		void assign(size_type n, const T& value)
		{
			clear();
			resize(n, value);
		}

		template<typename InputIt>
		void assign(InputIt first, InputIt last)
		{
			clear();
			insert(cbegin(), first, last);
		}

		void assign(std::initializer_list<T> init)
		{
			assign(init.begin(), init.end());
		}

		bool operator==(const slice& other) const noexcept
		{
			return sz == other.sz &&
				   std::equal(begin(), end(), other.begin());
		}

		bool operator!=(const slice& other) const noexcept
		{
			return !(*this == other);
		}

		allocator_type get_allocator() const noexcept
		{
			return allocator_type{};
		}

	private:
		pointer dt;
		size_type sz;
		size_type cap;

		void reallocate(size_type new_capacity)
		{
			allocator_type alloc;

			if (new_capacity == 0)
			{
				clear();
				alloc.deallocate(dt, cap);
				dt = nullptr;
				cap = 0;
				return;
			}

			pointer new_data = alloc.allocate(new_capacity);
			if (dt)
			{
				for (size_type i = 0; i < sz; ++i)
				{
					if constexpr (std::is_nothrow_move_constructible_v<T>)
						alloc.construct(&new_data[i], std::move(dt[i]));
					else
						alloc.construct(&new_data[i], dt[i]);
					alloc.destroy(&dt[i]);
				}
				alloc.deallocate(dt, cap);
			}

			dt = new_data;
			cap = new_capacity;
		}

		void insert_impl(size_type index, size_type n, const value_type& value)
		{
			if (n == 0)
				return;

			const size_type new_size = sz + n;
			if (new_size > cap)
				reserve(new_size);

			allocator_type alloc;
			for (size_type i = sz; i > index; --i) {
				if constexpr (std::is_nothrow_move_constructible_v<T>)
					alloc.construct(&dt[i + n - 1], std::move(dt[i - 1]));
				else
					alloc.construct(&dt[i + n - 1], dt[i - 1]);
				alloc.destroy(&dt[i - 1]);
			}

			for (size_type i = 0; i < n; ++i)
				alloc.construct(&dt[index + i], value);

			sz = new_size;
		}

		template<typename InputIt>
		void insert_range_impl(size_type index, InputIt first, InputIt last)
		{
			if (first == last)
				return;

			const auto count = static_cast<size_type>(std::distance(first, last));
			const size_type new_size = sz + count;
			if (new_size > cap)
				reserve(new_size);

			allocator_type alloc;
			/* move elements by -1 to make space for new elements to apppend */
			for (size_type i = sz; i > index; --i)
			{
				alloc.construct(&dt[i + count - 1], std::move(dt[i - 1]));
				alloc.destroy(&dt[i - 1]);
			}

			/* inserts the new elements */
			size_type i = index;
			for (auto it = first; it != last; ++it, ++i)
				alloc.construct(&dt[i], *it);

			sz = new_size;
		}

		template<typename... Args>
		void emplace_impl(size_type index, Args&&... args)
		{
			if (sz == cap)
			{
				const size_type new_cap = cap == 0 ? 1 : cap * 2;
				reserve(new_cap);
			}

			allocator_type alloc;
			for (size_type i = sz; i > index; --i)
			{
				alloc.construct(&dt[i], std::move(dt[i - 1]));
				alloc.destroy(&dt[i - 1]);
			}

			alloc.construct(&dt[index], std::forward<Args>(args)...);
			++sz;
		}

		void erase_impl(size_type start_idx, size_type end_idx)
		{
			const size_type count = end_idx - start_idx;
			allocator_type alloc;

			for (size_type i = start_idx; i < end_idx; ++i)
				alloc.destroy(&dt[i]);

			/* shift remaining elements to the left */
			for (size_type i = end_idx; i < sz; ++i)
			{
				alloc.construct(&dt[i - count], std::move(dt[i]));
				alloc.destroy(&dt[i]);
			}

			sz -= count;
		}
		/* no padding for maximum memory efficiency; we don't need extra padding
		 * as the slice will be used specifically as a component in other structs */
	} __attribute__((packed));

	/* deduction guides for slice */
	template<typename InputIt>
	slice(InputIt, InputIt) -> slice<typename std::iterator_traits<InputIt>::value_type>;

	template<typename T>
	slice(std::initializer_list<T>) -> slice<T>;

	/* aliases for common slice types with different size types
	 * note: the size and storage is under the assumption that the
	 * allocator is stateless */
	template<typename T>
	using u8slice = slice<T, std::uint8_t>; /* up to 255 elements; ~10 bytes storage */

	template<typename T>
	using u16slice = slice<T>; /* up to 65535 elements; ~12 bytes storage */

	template<typename T>
	using u32slice = slice<T, std::uint32_t>; /* up to 4294967295 elements; ~16 bytes storage */

	/* u64slice is not defined as it would require 24 bytes of storage,
	 * which defeats the purpose of using a smaller size type */
}
