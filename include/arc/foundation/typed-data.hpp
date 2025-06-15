/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <cstdint>
#include <arc/support/slice.hpp>
#include <arc/support/string-table.hpp>

namespace arc
{
	/** @brief Forward declaration of the `Node` structure */
	struct Node;

	/**
	 *	@brief Arc's data type enumeration
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
		VECTOR,
		STRING
	};

	template<DataType /* T */>
	struct DataTraits
	{
		struct value {};
	};

	/**
	 *	@brief Specialization for `DataType::VOID`
	 */
	template<>
	struct DataTraits<DataType::VOID>
	{
		struct value
		{
			bool operator==(const value&) const
			{
				return true; /* void type is always equal to itself */
			}
		};
	};

	/**
	 *	@brief Specialization for `DataType::BOOL`
	 */
	template<>
	struct DataTraits<DataType::BOOL>
	{
		using value = bool;
	};

	/**
	 *	@brief Specialization for `DataType::INT8`
	 */
	template<>
	struct DataTraits<DataType::INT8>
	{
		using value = std::int8_t;
	};

	/**
	 *	@brief Specialization for `DataType::INT16`
	 */
	template<>
	struct DataTraits<DataType::INT16>
	{
		using value = std::int16_t;
	};

	/**
	 *	@brief Specialization for `DataType::INT32`
	 */
	template<>
	struct DataTraits<DataType::INT32>
	{
		using value = std::int32_t;
	};

	/**
	 *	@brief Specialization for `DataType::INT64`
	 */
	template<>
	struct DataTraits<DataType::INT64>
	{
		using value = std::int64_t;
	};

	/**
	 *	@brief Specialization for `DataType::UINT8`
	 */
	template<>
	struct DataTraits<DataType::UINT8>
	{
		using value = std::uint8_t;
	};

	/**
	 *	@brief Specialization for `DataType::UINT16`
	 */
	template<>
	struct DataTraits<DataType::UINT16>
	{
		using value = std::uint16_t;
	};

	/**
	 *	@brief Specialization for `DataType::UINT32`
	 */
	template<>
	struct DataTraits<DataType::UINT32>
	{
		using value = std::uint32_t;
	};

	/**
	 *	@brief Specialization for `DataType::UINT64`
	 */
	template<>
	struct DataTraits<DataType::UINT64>
	{
		using value = std::uint64_t;
	};

	/**
	 *	@brief Specialization for `DataType::FLOAT32`
	 */
	template<>
	struct DataTraits<DataType::FLOAT32>
	{
		using value = float;
	};

	/**
	 *	@brief Specialization for `DataType::FLOAT64`
	 */
	template<>
	struct DataTraits<DataType::FLOAT64>
	{
		using value = double;
	};

	/**
	 *	@brief Specialization for `DataType::STRING`
	 */
	template<>
	struct DataTraits<DataType::STRING>
	{
		using value = StringTable::StringId;
	};

	/**
	 *	@brief Specialization for `DataType::VECTOR`
	 */
	template<>
	struct DataTraits<DataType::VECTOR>
	{
		struct value
		{
			/** @brief The elements of the vector type */
			DataType elem_type;

			bool operator==(const value& other) const
			{
				return elem_type == other.elem_type;
			}
		};
	};

	/**
	 *	@brief Specialization for `DataType::POINTER`
	 */
	template<>
	struct DataTraits<DataType::POINTER>
	{
		struct value
		{
			/**	@brief Pointer to a `Node` structure */
			Node *pointee;
			/** @brief The address space of the pointer */
			std::uint32_t addr_space;
		};
	};

	/**
	 *	@brief Specialization for `DataType::ARRAY`
	 */
	template<>
	struct DataTraits<DataType::ARRAY>
	{
		struct value
		{
			/** @brief The elements of the array */
			u16slice<Node*> elements;
			/** @brief The element type */
			DataType elem_type;
		};
	};

	/**
	 *	@brief Specialization for `DataType::STRUCT`
	 */
	template<>
	struct DataTraits<DataType::STRUCT>
	{
		struct value
		{
			/** @brief The fields of the struct */
			u8slice<std::pair<StringTable::StringId, DataType>> fields;
			/** @brief The alignment of the struct */
			std::uint32_t alignment;
		};
	};

	/**
	 *	@brief Specialization for `DataType::FUNCTION`
	 */
	template<>
	struct DataTraits<DataType::FUNCTION>
	{
		struct value {};
		/* intentionally left empty as it is possible to iterate
		 * through the use-def chains of the function to determine each parameter
		 * type by simply checking if any of node in the chains is `NodeType::PARAM`
		 *
		 * the return type of the function can be trivially determined by checking
		 * the `::type_kind` field. variadic argument parameter is not supported
		 * due to complexity at the ABI level */
	};
}
