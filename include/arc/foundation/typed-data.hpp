/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <cstdint>
#include <memory>
#include <tuple>
#include <arc/support/slice.hpp>
#include <arc/support/string-table.hpp>

namespace arc
{
	class TypedData;
	/** @brief Forward declaration of the `Node` structure */
	struct Node;

	/**
	 * @brief Arc's data type enumeration
	 */
	enum class DataType : std::uint8_t
	{
		VOID,
		BOOL,
		INT8, INT16, INT32, INT64,
		UINT8, UINT16, UINT32, UINT64,
		FLOAT32, FLOAT64,
		POINTER,
		ARRAY,
		STRUCT,
		FUNCTION,
		VECTOR
	};

	template<DataType /* T */>
	struct DataTraits
	{
		struct value {};
	};

	/**
	 * @brief Specialization for `DataType::VOID`
	 */
	template<>
	struct DataTraits<DataType::VOID>
	{
		struct value
		{
			bool operator==(const value &) const
			{
				return true; /* void type is always equal to itself */
			}
		};
	};

	/**
	 * @brief Specialization for `DataType::BOOL`
	 */
	template<>
	struct DataTraits<DataType::BOOL>
	{
		using value = bool;
	};

	/**
	 * @brief Specialization for `DataType::INT8`
	 */
	template<>
	struct DataTraits<DataType::INT8>
	{
		using value = std::int8_t;
	};

	/**
	 * @brief Specialization for `DataType::INT16`
	 */
	template<>
	struct DataTraits<DataType::INT16>
	{
		using value = std::int16_t;
	};

	/**
	 * @brief Specialization for `DataType::INT32`
	 */
	template<>
	struct DataTraits<DataType::INT32>
	{
		using value = std::int32_t;
	};

	/**
	 * @brief Specialization for `DataType::INT64`
	 */
	template<>
	struct DataTraits<DataType::INT64>
	{
		using value = std::int64_t;
	};

	/**
	 * @brief Specialization for `DataType::UINT8`
	 */
	template<>
	struct DataTraits<DataType::UINT8>
	{
		using value = std::uint8_t;
	};

	/**
	 * @brief Specialization for `DataType::UINT16`
	 */
	template<>
	struct DataTraits<DataType::UINT16>
	{
		using value = std::uint16_t;
	};

	/**
	 * @brief Specialization for `DataType::UINT32`
	 */
	template<>
	struct DataTraits<DataType::UINT32>
	{
		using value = std::uint32_t;
	};

	/**
	 * @brief Specialization for `DataType::UINT64`
	 */
	template<>
	struct DataTraits<DataType::UINT64>
	{
		using value = std::uint64_t;
	};

	/**
	 * @brief Specialization for `DataType::FLOAT32`
	 */
	template<>
	struct DataTraits<DataType::FLOAT32>
	{
		using value = float;
	};

	/**
	 * @brief Specialization for `DataType::FLOAT64`
	 */
	template<>
	struct DataTraits<DataType::FLOAT64>
	{
		using value = double;
	};

	/**
	 * @brief Specialization for `DataType::VECTOR`
	 */
	template<>
	struct DataTraits<DataType::VECTOR>
	{
		struct value
		{
			/** @brief The elements of the vector type */
			DataType elem_type;
			std::uint32_t lane_count;
			bool operator==(const value &other) const
			{
				return elem_type == other.elem_type;
			}
		} __attribute__((packed));
	};

	/**
	 * @brief Specialization for `DataType::POINTER`
	 */
	template<>
	struct DataTraits<DataType::POINTER>
	{
		enum class PtrQualifier : std::uint8_t
		{
			NONE = 0,
			CONST = 1 << 0,      /* data pointed to is immutable AND won't escape */
			RESTRICT = 1 << 1,   /* no aliasing with other pointers */
			WRITEONLY = 1 << 2,  /* function only writes, never reads */
			NOMUTABLE = 1 << 3   /* ptr itself can't be modified */
		};

		friend constexpr PtrQualifier operator|(PtrQualifier lhs, PtrQualifier rhs)
		{
			return static_cast<PtrQualifier>(
				static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs)
			);
		}

		friend constexpr PtrQualifier operator&(PtrQualifier lhs, PtrQualifier rhs)
		{
			return static_cast<PtrQualifier>(
				static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs)
			);
		}

		friend constexpr PtrQualifier operator^(PtrQualifier lhs, PtrQualifier rhs)
		{
			return static_cast<PtrQualifier>(
				static_cast<std::uint8_t>(lhs) ^ static_cast<std::uint8_t>(rhs)
			);
		}

		friend constexpr PtrQualifier operator~(PtrQualifier qual)
		{
			return static_cast<PtrQualifier>(~static_cast<std::uint8_t>(qual));
		}

		friend constexpr PtrQualifier& operator|=(PtrQualifier& lhs, PtrQualifier rhs)
		{
			lhs = lhs | rhs;
			return lhs;
		}

		friend constexpr PtrQualifier& operator&=(PtrQualifier& lhs, PtrQualifier rhs)
		{
			lhs = lhs & rhs;
			return lhs;
		}

		struct value
		{
			/**	@brief Pointer to a `Node` structure */
			Node *pointee;
			/** @brief The address space of the pointer */
			std::uint32_t addr_space;
			/** @brief Pointer qualifiers */
			PtrQualifier qualifier = PtrQualifier::NONE;
		} __attribute__((packed));
	};

	/**
	 * @brief Specialization for `DataType::ARRAY`
	 */
	template<>
	struct DataTraits<DataType::ARRAY>
	{
		struct value
		{
			/** @brief The elements of the array */
			u16slice<Node *> elements;
			/** @brief The element type */
			DataType elem_type;
			/** @brief Array size */
			std::uint32_t count;
		} __attribute__((packed));
	};

	/**
	 * @brief Specialization for `DataType::STRUCT`
	 */
	template<>
	struct DataTraits<DataType::STRUCT>
	{
		struct value
		{
			/** @brief The fields of the struct */
			u8slice<std::tuple<StringTable::StringId, DataType, TypedData> > fields;
			/** @brief The alignment of the struct */
			std::uint32_t alignment;
			/** @brief Interned string id to the struct name */
			StringTable::StringId name;
		} __attribute__((packed));
	};

	/**
	 * @brief Specialization for `DataType::FUNCTION`
	 */
	template<>
	struct DataTraits<DataType::FUNCTION>
	{
		struct value
		{
			/* always initialized to nullptr just so the TypedData::destroy() doesn't
		     * accidentally clear it */
			TypedData* return_type = nullptr;
		};

		/* parameter types can be determined by iterating through the function's
		 * use-def chains and checking for nodes with NodeType::PARAM.
		 *
		 * return type is stored directly in the return_type field to avoid
		 * complex inference logic that fails with recursive functions.
		 *
		 * variadic argument parameters are not supported due to complexity
		 * at the ABI level */
	};

	/**
	 * @brief Type-erased storage with fixed 20-byte buffer
	 */
	class TypedData
	{
		/* Node::type_kind determines what's stored here. DataTraits<T>::value
		 * defines the storage layout. this separation allows the same DataType
		 * to describe both type contracts and actual values, disambiguated by
		 * Node::ir_type (TYPE vs LIT vs operations) */
	public:
		/**
		 * @brief Default constructor. Creates DataType::VOID type
		 */
		TypedData();

		/**
		 * @brief Destructor
		 */
		~TypedData();

		TypedData(const TypedData &other);

		TypedData(TypedData &&other) noexcept;

		TypedData &operator=(const TypedData &other);

		TypedData &operator=(TypedData &&other) noexcept;

		/**
		 * @brief Get the current data type
		 */
		[[nodiscard]] DataType type() const;

		/**
		 * @brief Check if the type currently holding is the same as `T`
		 * @tparam T type to check against
		 * @return true if T comparison to the currently holding matches otherwise false
		 */
		template<typename T>
		[[nodiscard]] bool is_type() const
		{
			switch (current_type)
			{
#define ARC_TYPEDATA_CHECK(dt) \
case DataType::dt: return std::is_same_v<T, typename DataTraits<DataType::dt>::value>;

				ARC_TYPEDATA_CHECK(BOOL)
				ARC_TYPEDATA_CHECK(INT8)
				ARC_TYPEDATA_CHECK(INT16)
				ARC_TYPEDATA_CHECK(INT32)
				ARC_TYPEDATA_CHECK(INT64)
				ARC_TYPEDATA_CHECK(UINT8)
				ARC_TYPEDATA_CHECK(UINT16)
				ARC_TYPEDATA_CHECK(UINT32)
				ARC_TYPEDATA_CHECK(UINT64)
				ARC_TYPEDATA_CHECK(FLOAT32)
				ARC_TYPEDATA_CHECK(FLOAT64)
				ARC_TYPEDATA_CHECK(POINTER)
				ARC_TYPEDATA_CHECK(ARRAY)
				ARC_TYPEDATA_CHECK(STRUCT)
				ARC_TYPEDATA_CHECK(FUNCTION)
				ARC_TYPEDATA_CHECK(VECTOR)
#undef ARC_TYPEDATA_CHECK

				default:
					return false;
			}
		}

		/**
		 * @brief Get the value of the current type
		 * @tparam T DataType enum value to get
		 * @return Reference to the value of the current type
		 *
		 * note: the DataType should match the `Node::type_kind` field.
		 * example: if node->type_kind == DataType::ARRAY, use get<DataType::ARRAY>()
		 */
		template<DataType T>
		typename DataTraits<T>::value &get()
		{
			/* DataType must match Node::type_kind to enforce storage contract.
				* provides compile-time type safety with runtime type checking */
			if (current_type != T)
				throw std::bad_variant_access();
			return *std::launder(reinterpret_cast<typename DataTraits<T>::value *>(storage));
		}

		/**
		 * @brief get the value of the current type (const version)
		 * @tparam T DataType enum value to get
		 * @return const reference to the value of the current type
		 *
		 * note: the DataType should match the Node's type_kind field.
		 */
		template<DataType T>
		const typename DataTraits<T>::value &get() const
		{
			if (current_type != T)
				throw std::bad_variant_access();
			return *std::launder(reinterpret_cast<const typename DataTraits<T>::value *>(storage));
		}

		/**
		 * @brief Set the type to `U` and the value to `value`
		 * @tparam T C++ equivalent type of the value
		 * @tparam U `DataType` to set
		 * @param value value to set
		 */
		template<typename T, DataType U>
			requires(std::is_same_v<std::decay_t<T>, typename DataTraits<U>::value>)
		void set(T &&value)
		{
			static_assert(sizeof(typename DataTraits<U>::value) <= MAX_SIZE,
			              "type too large for TypedData buffer");
			static_assert(alignof(typename DataTraits<U>::value) <= MAX_ALIGN,
			              "type alignment too strict for TypedData buffer");

			destroy();
			current_type = U;
			std::construct_at(reinterpret_cast<typename DataTraits<U>::value *>(storage),
			                  std::forward<T>(value));
		}

		/**
		 * @brief Set the type to `U` and the value to `value` (const lvalue version)
		 * @tparam T C++ equivalent type of the value
		 * @tparam U `DataType` to set
		 * @param value value to set
		 */
		template<typename T, DataType U>
			requires(std::is_same_v<std::decay_t<T>, typename DataTraits<U>::value>)
		void set(const T &value)
		{
			static_assert(sizeof(typename DataTraits<U>::value) <= MAX_SIZE,
			              "type too large for TypedData buffer");
			static_assert(alignof(typename DataTraits<U>::value) <= MAX_ALIGN,
			              "type alignment too strict for TypedData buffer");

			destroy();
			current_type = U;
			std::construct_at(reinterpret_cast<typename DataTraits<U>::value *>(storage), value);
		}

	private:
		static constexpr std::size_t MAX_SIZE = 20;
		static constexpr std::size_t MAX_ALIGN = []() constexpr
		{
			std::size_t max_align = 1;

#define ARC_CHECK_ALIGN(dt) \
   	max_align = std::max(max_align, alignof(typename DataTraits<DataType::dt>::value));

			ARC_CHECK_ALIGN(BOOL)
			ARC_CHECK_ALIGN(INT8)
			ARC_CHECK_ALIGN(INT16)
			ARC_CHECK_ALIGN(INT32)
			ARC_CHECK_ALIGN(INT64)
			ARC_CHECK_ALIGN(UINT8)
			ARC_CHECK_ALIGN(UINT16)
			ARC_CHECK_ALIGN(UINT32)
			ARC_CHECK_ALIGN(UINT64)
			ARC_CHECK_ALIGN(FLOAT32)
			ARC_CHECK_ALIGN(FLOAT64)
			ARC_CHECK_ALIGN(VECTOR)
			ARC_CHECK_ALIGN(POINTER)
			ARC_CHECK_ALIGN(ARRAY)
			ARC_CHECK_ALIGN(STRUCT)
			ARC_CHECK_ALIGN(FUNCTION)
			ARC_CHECK_ALIGN(VOID)

#undef ARC_CHECK_ALIGN
			return max_align;
		}();

		alignas(MAX_ALIGN) unsigned char storage[MAX_SIZE] = {};
		DataType current_type = DataType::VOID;

		void destroy();

		void copy_from(const TypedData &other);

		void move_from(TypedData &&other) noexcept;
	};

	/**
	* @brief Specialized get implementations for VOID
	*/
	template<>
	inline const DataTraits<DataType::VOID>::value &TypedData::get<DataType::VOID>() const
	{
		if (current_type != DataType::VOID)
			throw std::bad_variant_access();

		static constexpr DataTraits<DataType::VOID>::value void_value = {};
		return void_value;
	}

	template<>
	inline DataTraits<DataType::VOID>::value &TypedData::get<DataType::VOID>()
	{
		if (current_type != DataType::VOID)
			throw std::bad_variant_access();

		static DataTraits<DataType::VOID>::value void_value = {};
		return void_value;
	}
}
