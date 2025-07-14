/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <arc/foundation/typed-data.hpp>
#include <arc/foundation/node.hpp>

namespace arc
{
	/**
	 * @brief Infer and apply type promotion for binary operations in-place
	 *
	 * Modifies the input nodes to have compatible promoted types following
	 * C/C++ conversion rules adapted for Arc's type system.
	 * i. Integer types smaller than INT32 are promoted to INT32
	 * ii. Mixed signed/unsigned promotes to next larger signed type
	 * iii. Float operations prefer FLOAT64 unless both operands are FLOAT32
	 * iv. VECTOR operations promote element types using same rules
	 * v. POINTER types require explicit casts
	 *
	 * @param lhs Left operand node (modified in-place)
	 * @param rhs Right operand node (modified in-place)
	 * @return true if promotion succeeded, false for incompatible types
	 */
	bool infer_binary_t(Node *lhs, Node *rhs);

	/**
	 * @brief Internal helper for primitive type inference
	 * @param lhs Left operand type
	 * @param rhs Right operand type
	 * @return Promoted type, or DataType::VOID for incompatible types
	 */
	DataType infer_primitive_types(DataType lhs, DataType rhs) noexcept;

	/**
	 * @brief Check if a type is an integer type
	 * @param type Type to check
	 * @return true if type is any integer type (signed or unsigned)
	 */
	constexpr bool is_integer_t(DataType type) noexcept
	{
		switch (type)
		{
			case DataType::INT8:
			case DataType::INT16:
			case DataType::INT32:
			case DataType::INT64:
			case DataType::UINT8:
			case DataType::UINT16:
			case DataType::UINT32:
			case DataType::UINT64:
				return true;
			default:
				return false;
		}
	}

	/**
	 * @brief Check if a type is a floating point type
	 * @param type Type to check
	 * @return true if type is FLOAT32 or FLOAT64
	 */
	constexpr bool is_float_t(DataType type) noexcept
	{
		return type == DataType::FLOAT32 || type == DataType::FLOAT64;
	}

	/**
	 * @brief Check if a type is a signed integer type
	 * @param type Type to check
	 * @return true if type is INT8, INT16, INT32, or INT64
	 */
	constexpr bool is_signed_integer_t(DataType type) noexcept
	{
		switch (type)
		{
			case DataType::INT8:
			case DataType::INT16:
			case DataType::INT32:
			case DataType::INT64:
				return true;
			default:
				return false;
		}
	}

	/**
	 * @brief Check if a type is an unsigned integer type
	 * @param type Type to check
	 * @return true if type is UINT8, UINT16, UINT32, or UINT64
	 */
	constexpr bool is_unsigned_integer_t(DataType type) noexcept
	{
		switch (type)
		{
			case DataType::UINT8:
			case DataType::UINT16:
			case DataType::UINT32:
			case DataType::UINT64:
				return true;
			default:
				return false;
		}
	}

	/**
	 * @brief Get the size rank of an integer type for promotion rules
	 * @param type Integer type
	 * @return Size rank (higher = larger type), or -1 for non-integer types
	 */
	constexpr int get_integer_rank(DataType type) noexcept
	{
		switch (type)
		{
			case DataType::INT8:
			case DataType::UINT8:
				return 0;
			case DataType::INT16:
			case DataType::UINT16:
				return 1;
			case DataType::INT32:
			case DataType::UINT32:
				return 2;
			case DataType::INT64:
			case DataType::UINT64:
				return 3;
			default:
				return -1;
		}
	}

	/**
	* @brief Create a default/empty value for a given DataType
	* @tparam T The DataType to create a value for
	* @return Default-constructed value of the corresponding type
	*/
	template<DataType T>
	auto make_t()
	{
		if constexpr (T == DataType::VOID)
		{
			return DataTraits<DataType::VOID>::value {};
		}
		else if constexpr (T == DataType::BOOL)
		{
			return false;
		}
		else if constexpr (T == DataType::INT8)
		{
			return std::int8_t { 0 };
		}
		else if constexpr (T == DataType::INT16)
		{
			return std::int16_t { 0 };
		}
		else if constexpr (T == DataType::INT32)
		{
			return std::int32_t { 0 };
		}
		else if constexpr (T == DataType::INT64)
		{
			return std::int64_t { 0 };
		}
		else if constexpr (T == DataType::UINT8)
		{
			return std::uint8_t { 0 };
		}
		else if constexpr (T == DataType::UINT16)
		{
			return std::uint16_t { 0 };
		}
		else if constexpr (T == DataType::UINT32)
		{
			return std::uint32_t { 0 };
		}
		else if constexpr (T == DataType::UINT64)
		{
			return std::uint64_t { 0 };
		}
		else if constexpr (T == DataType::FLOAT32)
		{
			return 0.0f;
		}
		else if constexpr (T == DataType::FLOAT64)
		{
			return 0.0;
		}
		else if constexpr (T == DataType::POINTER)
		{
			return DataTraits<DataType::POINTER>::value { nullptr, 0 };
		}
		else if constexpr (T == DataType::ARRAY)
		{
			DataTraits<DataType::ARRAY>::value arr = {};
			arr.elements = {};
			arr.elem_type = DataType::VOID;
			return arr;
		}
		else if constexpr (T == DataType::STRUCT)
		{
			DataTraits<DataType::STRUCT>::value s;
			s.fields = {};
			s.alignment = 1;
			s.name = StringTable::StringId {};
			return s;
		}
		else if constexpr (T == DataType::FUNCTION)
		{
			DataTraits<DataType::FUNCTION>::value fn;
			ach::allocator<TypedData> a;
			auto* s = a.allocate(1);
			std::construct_at(s);
			fn.return_type = s;
			return fn;
		}
		else if constexpr (T == DataType::VECTOR)
		{
			DataTraits<DataType::VECTOR>::value vec = {};
			vec.elem_type = DataType::VOID;
			vec.lane_count = 0;
			return vec;
		}
		else
		{
			static_assert(sizeof(T) == 0, "unsupported DataType in make_t");
		}
	}

	/**
	* @brief Convenience function to set a TypedData with a type value
	* @tparam T The DataType to set
	* @param data The TypedData to modify
	*/
	template<DataType T>
	void set_t(TypedData &data)
	{
		auto value = make_t<T>();
		data.set<decltype(value), T>(value);
	}

	/**
	 * @brief Set TypedData to default value of specified type
	 * @param data TypedData to initialize
	 * @param type DataType to set; runtime parameter
	 */
	void set_t(TypedData &data, DataType type);

	/**
	 * @brief Get the size in bytes of a data type
	 * @param type The data type to get size for
	 * @return Size in bytes
	 */
	constexpr std::uint32_t elem_sz(DataType type) noexcept
	{
		switch (type)
		{
			case DataType::BOOL:
			case DataType::INT8:
			case DataType::UINT8:
				return 1;
			case DataType::INT16:
			case DataType::UINT16:
				return 2;
			case DataType::INT32:
			case DataType::UINT32:
			case DataType::FLOAT32:
				return 4;
			case DataType::INT64:
			case DataType::UINT64:
			case DataType::FLOAT64:
			case DataType::POINTER:
				return 8;
			case DataType::VOID:
			default:
				return 0;
		}
	}

	/**
	 * @brief Get the alignment requirement for a data type
	 * @param type The data type to get alignment for
	 * @return Alignment in bytes
	 */
	constexpr std::uint32_t align_t(const DataType type) noexcept
	{
	    switch (type)
	    {
	        case DataType::BOOL:
	        case DataType::INT8:
	        case DataType::UINT8:
	            return 1;
	        case DataType::INT16:
	        case DataType::UINT16:
	            return 2;
	        case DataType::INT32:
	        case DataType::UINT32:
	        case DataType::FLOAT32:
	            return 4;
	        case DataType::INT64:
	        case DataType::UINT64:
	        case DataType::FLOAT64:
	        case DataType::POINTER:
	            return 8;
	        case DataType::VOID:
	        default:
	            return 1;
	    }
	}

	/**
	 * @brief Get the most appropriate padding type for a given padding size
	 * @param padding_size Size of padding needed in bytes
	 * @return DataType that best fits the padding size
	 */
	constexpr DataType padding_t(const std::size_t padding_size) noexcept
	{
	    if (padding_size >= 8)
	    	return DataType::UINT64;
	    if (padding_size >= 4)
	    	return DataType::UINT32;
	    if (padding_size >= 2)
	    	return DataType::UINT16;
	    return DataType::UINT8;
	}

	/**
	 * @brief Calculate the total size of a struct with padding
	 * @param struct_data The struct data to calculate size for
	 * @return Total size in bytes including padding
	 */
	constexpr std::size_t compute_struct_size(const DataTraits<DataType::STRUCT>::value& struct_data) noexcept
	{
		/* packed structs have no padding, so we can just sum the field sizes */
	    if (struct_data.alignment == 1)
	    {
	        std::size_t size = 0;
	        for (const auto& [name_id, field_type, field_data] : struct_data.fields)
	        {
	            size += elem_sz(field_type);
	        }
	        return size;
	    }

	    std::size_t size = 0;
	    for (const auto& [name_id, field_type, field_data] : struct_data.fields)
	        size += elem_sz(field_type);
	    return size;
	}

	/**
	 * @brief Check if a pointer has a specific qualifier
	 * @param pointer Pointer node to check
	 * @param qual Qualifier to check for
	 * @return true if pointer has the specified qualifier
	 */
	bool has_pointer_qualifier(Node* pointer, DataTraits<DataType::POINTER>::PtrQualifier qual);

	/**
	 * @brief Check if a pointer is const-qualified
	 * @param pointer Pointer node to check
	 * @return true if pointer points to immutable data
	 */
	bool is_const_pointer(Node* pointer);

	/**
	 * @brief Check if a pointer is restrict-qualified
	 * @param pointer Pointer node to check
	 * @return true if pointer has no aliasing guarantees
	 */
	bool is_restrict_pointer(Node* pointer);

	/**
	 * @brief Check if a pointer is writeonly-qualified
	 * @param pointer Pointer node to check
	 * @return true if function only writes, never reads through this pointer
	 */
	bool is_writeonly_pointer(Node* pointer);

	/**
	 * @brief Check if a pointer itself is non-mutable
	 * @param pointer Pointer node to check
	 * @return true if pointer itself can't be modified
	 */
	bool is_nomutable_pointer(Node* pointer);

	inline bool has_qualifier(const DataTraits<DataType::POINTER>::value& ptr_data,
						 DataTraits<DataType::POINTER>::PtrQualifier qual)
	{
		return (ptr_data.qualifier & qual) != DataTraits<DataType::POINTER>::PtrQualifier::NONE;
	}

	inline bool is_const_pointer(const DataTraits<DataType::POINTER>::value& ptr_data)
	{
		return has_qualifier(ptr_data, DataTraits<DataType::POINTER>::PtrQualifier::CONST);
	}
}
