/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <cstdint>
#include <arc/foundation/typed-data.hpp>
#include <arc/support/slice.hpp>
#include <arc/support/string-table.hpp>

namespace arc
{
	class Region;

	/**
	 * @brief Represents the type of operation an IR node performs
	 * @note This enum is used to identify the type of operation of an IR node
	 */
	enum class NodeType : std::uint16_t
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
		/** @brief Type cast */
		CAST,
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
		/** @brief Represents an external entity; Behaves similar to C/C++'s extern keyword */
		EXTERN = 1 << 0,
		/** @brief Represents a driver function; entry point of the program */
		DRIVER = 1 << 1,
		/** @brief Represents a symbol to resolve across modules; External linkage */
		EXPORT = 1 << 2,
		/** @brief Represents a node that should not be optimized e.g. C/C++'s `volatile` */
		VOLATILE = 1 << 3,
		/** @brief Represents a read-only type; This goes to an executable's section .rodata */
		READONLY = 1 << 4
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
	 *	@brief Represent an IR node
	 */
	struct alignas(8) Node
	{
		/** @brief IR nodes that are depended on this node */
		u8slice<Node*> inputs = {};
		/** @brief IR Nodes that has dependency on this node */
		u8slice<Node*> users = {};
		/** @brief Parent region */
		Region* parent = nullptr;
		/** @brief Type of the node */
		NodeType ir_type = NodeType::ENTRY;
		/** @brief Traits of the node */
		NodeTraits traits = NodeTraits::NONE;
		/** @brief Type-erased value storage */
		TypedData value;
		/** @brief Type of the value */
		DataType type_kind;
		/** @brief String interning reference */
		StringTable::StringId str_id = {};

		/* the packed attribute is used mainly to reduce the cache line
		 * footprint from 72 bytes to 64 bytes. 72 bytes would span two cache lines,
		 * requiring two memory fetches per node access.
		 *
		 * field ordering prioritizes hot data first: connectivity are accessed most
		 * frequently to traverse the IR graph during the optimizations followed by
		 * `NodeType` and `NodeTrait` as they are used to check if a node has special
		 * rules or semantics e.g. VOLATILE, EXPORT, FUNCTION etc. value storage
		 * and type of the value is last as they are only accessed in a fairly small
		 * amount of passes that needs to see value such as SROA, AA, or CSE */
	} __attribute__((packed));
}
