/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <cstdint>
#include <arc/support/slice.hpp>
#include <arc/support/string-table.hpp>

namespace arc
{
	/**
	 * @brief Represents the type of operation an IR node performs
	 * @note This enum is used to identify the type of operation of an IR node
	 */
	enum class NodeType
	{
		/** @brief Entry point of a basic block or function */
		ENTRY,
		/** @brief Exit point of a basic block or function */
		EXIT,
		/** @brief Function parameter */
		PARAM,
		/** @brief Literal/constant value; includes string literal */
		LIT,
		/** @brief Arithmetic addition operation */
		ADD,
		/** @brief Arithmetic subtraction operation */
		SUB,
		/** @brief Arithmetic multiplication operation */
		MUL,
		/** @brief Arithmetic division operation */
		DIV,
		/** @brief Arithmetic modulus operation */
		MOD,
		/** @brief Comparison greater than operation */
		GT,
		/** @brief Comparison greater than or equal operation */
		GTE,
		/** @brief Comparison less than operation */
		LT,
		/** @brief Comparison less than or equal operation */
		LTE,
		/** @brief Comparison equal to operation */
		EQ,
		/** @brief Comparison is not equal to operation */
		NEQ,
		/** @brief Bitwise AND operation */
		BAND,
		/** @brief Bitwise OR operation */
		BOR,
		/** @brief Bitwise XOR operation */
		BXOR,
		/** @brief Bitwise NOT operation */
		BNOT,
		/** @brief Bitwise SHL operation */
		BSHL,
		/** @brief Bitwise SHR operation */
		BSHR,
		/** @brief Return statement */
		RET,
		/** @brief Function definition */
		FUNCTION,
		/** @brief Function call */
		CALL,
		/** @brief Stack memory allocation */
		ALLOC,
		/** @brief Load from a named memory location */
		LOAD,
		/** @brief Store to a named memory location */
		STORE,
		/** @brief Get address of a variable; &var */
		ADDR_OF,
		/** @brief Load via pointer dereference; *ptr */
		PTR_LOAD,
		/** @brief Store via pointer dereference; *ptr = var */
		PTR_STORE,
		/** @brief Pointer arithmetic; ptr + offset */
		PTR_ADD,
		/** @brief Type reinterpretion cast */
		REINTERPRET_CAST,
		/** @brief Thread-safe memory load */
		ATOMIC_LOAD,
		/** @brief Thread-safe memory store */
		ATOMIC_STORE,
		/** @brief Thread-safe memory exchange */
		ATOMIC_CAS,
		/** @brief A jump */
		JUMP,
		/** @brief A conditional jump */
		BRANCH,
		/** @brief A function call with exception handling or unwind */
		INVOKE,
		/** @brief Build a vector from scalar values of different operand */
		VECTOR_BUILD,
		/** @brief Extract a scalar from a vector */
		VECTOR_EXTRACT,
		/** @brief Build a vector from scalar values of same operand */
		VECTOR_SPLAT,
	};

	enum class AtomicOrdering : std::uint8_t
	{
		RELAXED = 0,
		ACQUIRE = 1 << 0,
		RELEASE = 1 << 1,

		/* note: ARM lowering will prefer LDXR/STXR;
		 * other architecture-specific ordering is to
		 * be below `EXCLUSIVE` */
		EXCLUSIVE = 1 << 4,

		/* common combinations that is in a language's standard library
		 * such as std::memoryorder::* */
		ACQ_REL = ACQUIRE | RELEASE,
		SEQ_CST = ACQUIRE | RELEASE | (1 << 3),
	};

	/**
	 * @brief Bit flags representing node traits
	 */
	enum class NodeTraits : std::uint16_t
	{
		/** @brief No special traits */
		NONE = 0,
		/** @brief Represents an entity with static traits; Internal linkage */
		STATIC = 1 << 0,
		/** @brief Represents an expression that can be evaluated at compile time */
		CONSTEXPR = 1 << 1,
		/** @brief Represents an external entity; External linkage */
		EXTERN = 1 << 2,
		/** @brief Represents a driver function; entry point of the program */
		DRIVER = 1 << 3,
		/** @brief Represents a symbol to resolve across modules */
		EXPORT = 1 << 4,
		/** @brief Represents a node that should not be optimized e.g. C/C++'s `volatile` */
		VOLATILE = 1 << 5,
		/** @brief Represents a read-only type; This goes to an executable's section .rodata */
		READONLY = 1 << 6
	};

	inline NodeTraits operator|(NodeTraits lhs, NodeTraits rhs)
	{
		return static_cast<NodeTraits>(
			static_cast<std::underlying_type_t<NodeTraits>>(lhs) |
			static_cast<std::underlying_type_t<NodeTraits>>(rhs)
		);
	}

	inline NodeTraits &operator|=(NodeTraits &lhs, const NodeTraits rhs)
	{
		lhs = lhs | rhs;
		return lhs;
	}

	inline NodeTraits operator&(NodeTraits lhs, NodeTraits rhs)
	{
		return static_cast<NodeTraits>(
			static_cast<std::underlying_type_t<NodeTraits>>(lhs) &
			static_cast<std::underlying_type_t<NodeTraits>>(rhs)
		);
	}

	inline NodeTraits &operator&=(NodeTraits &lhs, const NodeTraits rhs)
	{
		lhs = lhs & rhs;
		return lhs;
	}

	inline NodeTraits operator^(NodeTraits lhs, NodeTraits rhs)
	{
		return static_cast<NodeTraits>(
			static_cast<std::underlying_type_t<NodeTraits>>(lhs) ^
			static_cast<std::underlying_type_t<NodeTraits>>(rhs)
		);
	}

	inline NodeTraits &operator^=(NodeTraits &lhs, const NodeTraits rhs)
	{
		lhs = lhs ^ rhs;
		return lhs;
	}

	inline NodeTraits operator~(NodeTraits prop)
	{
		return static_cast<NodeTraits>(
			~static_cast<std::underlying_type_t<NodeTraits>>(prop)
		);
	}

	/**
	 *	@brief Represent an IR node.
	 */
	struct Node
	{
		/** @brief IR nodes that are depended on this node */
		u8slice<Node*> inputs = {};
		/** @brief IR Nodes that has dependency on this node */
		u8slice<Node*> users = {};
		/** @brief String interning reference */
		StringTable::StringId str_id = {};
		/** @brief Type of the node */
		NodeType ir_type = NodeType::ENTRY;
		/** @brief Traits of the node */
		NodeTraits traits = NodeTraits::NONE;
	};
}
