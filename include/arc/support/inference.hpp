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
	 * C/C++ conversion rules adapted for Arc's type system:
	 * - Integer types smaller than INT32 are promoted to INT32
	 * - Mixed signed/unsigned promotes to next larger signed type
	 * - Float operations prefer FLOAT64 unless both operands are FLOAT32
	 * - VECTOR operations promote element types using same rules
	 * - POINTER types require explicit casts
	 *
	 * @param lhs Left operand node (modified in-place)
	 * @param rhs Right operand node (modified in-place)
	 * @return true if promotion succeeded, false for incompatible types
	 */
	bool infer_binary_t(Node* lhs, Node* rhs);

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
	constexpr bool is_integer_t(DataType type) noexcept;

	/**
	 * @brief Check if a type is a floating point type
	 * @param type Type to check
	 * @return true if type is FLOAT32 or FLOAT64
	 */
	constexpr bool is_float_t(DataType type) noexcept;

	/**
	 * @brief Check if a type is a signed integer type
	 * @param type Type to check
	 * @return true if type is INT8, INT16, INT32, or INT64
	 */
	constexpr bool is_signed_integer_t(DataType type) noexcept;

	/**
	 * @brief Check if a type is an unsigned integer type
	 * @param type Type to check
	 * @return true if type is UINT8, UINT16, UINT32, or UINT64
	 */
	constexpr bool is_unsigned_integer_t(DataType type) noexcept;

	/**
	 * @brief Get the size rank of an integer type for promotion rules
	 * @param type Integer type
	 * @return Size rank (higher = larger type), or -1 for non-integer types
	 */
	constexpr int get_integer_rank(DataType type) noexcept;
}
