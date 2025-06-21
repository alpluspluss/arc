/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <functional>
#include <string_view>
#include <vector>
#include <arc/foundation/module.hpp>
#include <arc/foundation/node.hpp>
#include <arc/foundation/region.hpp>
#include <arc/foundation/typed-data.hpp>

namespace arc
{
	class StoreHelper;
	class Region;
	template<DataType T>
	class FunctionBuilder;
	template<DataType T>
	class BlockBuilder;

	/**
	 * @brief Main IR builder class for constructing Arc IR nodes and regions
	 */
	class Builder
	{
	public:
		/**
		 * @brief Construct a new Builder
		 * @param module Module to build IR in
		 */
		explicit Builder(Module &module);

		/**
		 * @brief Set the current insertion point for new nodes
		 * @param region Region to insert nodes into
		 */
		void set_insertion_point(Region *region);

		/**
		 * @brief Get the current insertion region
		 * @return Current region for node insertion
		 */
		[[nodiscard]] Region *get_insertion_point() const;

		/**
		 * @brief Create a function with specified return type
		 * @tparam ReturnType Return type of the function
		 * @param name Function name
		 * @return Function builder for this function
		 */
		template<DataType ReturnType>
		FunctionBuilder<ReturnType> function(std::string_view name);

		/**
		 * @brief Create a basic block with specified return type
		 * @tparam ReturnType Return type of the block
		 * @param name Block name
		 * @return Block builder for this block
		 */
		template<DataType ReturnType>
		BlockBuilder<ReturnType> block(std::string_view name);

		/**
		 * @brief Create a literal node
		 * @tparam T Type of the literal value
		 * @param value Literal value
		 * @return Node representing the literal
		 */
		template<typename T>
		Node *lit(T value);

		/**
		 * @brief Create an allocation node
		 * @tparam ElementType Type of elements to allocate
		 * @param count Number of elements to allocate
		 * @return Node representing the allocation
		 */
		template<DataType ElementType>
		Node *alloc(Node *count);

		/**
		 * @brief Create a load node from named memory location
		 * @param location Memory location to load from
		 * @return Node representing the loaded value
		 */
		Node *load(Node *location);

		/**
		 * @brief Create a store node to named memory location
		 * @param value Value to store
		 * @param location Memory location to store to
		 * @return Node representing the store operation
		 */
		Node* store(Node* value, Node* location);

		/**
		 * @brief Create a load node from named memory location
		 * @param location Memory location to load from
		 * @return Node representing the loaded value
		 */
		StoreHelper store(Node *location);

		/**
		 * @brief Create a load node from pointer
		 * @param pointer Pointer to dereference
		 * @return Node representing the loaded value
		 */
		Node *ptr_load(Node *pointer);

		/**
		 * @brief Create a pointer store node
		 * @param value Value to store
		 * @param pointer Pointer to store through
		 * @return Node representing the store operation
		 */
		Node* ptr_store(Node* value, Node* pointer);

		/**
		 * @brief Create an address-of node
		 * @param variable Variable to take address of
		 * @return Node representing the address
		 */
		Node *addr_of(Node *variable);

		/**
		 * @brief Create pointer arithmetic node
		 * @param base_pointer Base pointer
		 * @param offset Byte offset
		 * @return Node representing the new pointer
		 */
		Node *ptr_add(Node *base_pointer, Node *offset);

		/**
		 * @brief Create binary arithmetic operation
		 * @param op Operation type
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing the operation result
		 */
		Node *binary_op(NodeType op, Node *lhs, Node *rhs);

		/**
		 * @brief Create addition node
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing lhs + rhs
		 */
		Node *add(Node *lhs, Node *rhs);

		/**
		 * @brief Create subtraction node
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing lhs - rhs
		 */
		Node *sub(Node *lhs, Node *rhs);

		/**
		 * @brief Create multiplication node
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing lhs * rhs
		 */
		Node *mul(Node *lhs, Node *rhs);

		/**
		 * @brief Create division node
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing lhs / rhs
		 */
		Node *div(Node *lhs, Node *rhs);

		/**
		 * @brief Create function call node
		 * @param function Function to call
		 * @param args Function arguments
		 * @return Node representing the call result
		 */
		Node *call(Node *function, const std::vector<Node *> &args = {});

		/**
		 * @brief Create return node
		 * @param value Value to return (optional for void functions)
		 * @return Node representing the return
		 */
		Node *ret(Node *value = nullptr);

		/**
		 * @brief Create conditional branch node
		 * @param condition Condition to branch on
		 * @param true_target Target for true condition
		 * @param false_target Target for false condition
		 * @return Node representing the branch
		 */
		Node *branch(Node *condition, Node *true_target, Node *false_target);

		/**
		 * @brief Create unconditional jump node
		 * @param target Target to jump to
		 * @return Node representing the jump
		 */
		Node *jump(Node *target);

		/**
		 * @brief Create function call with exception handling
		 * @param function Function to call
		 * @param args Function arguments
		 * @param normal_target Target for normal execution
		 * @param except_target Target for exception handling
		 * @return Node representing the invoke operation
		 */
		Node *invoke(Node *function, const std::vector<Node *> &args, Node *normal_target, Node *except_target);

		/**
		 * @brief Create a vector build node from scalar elements
		 * @param elements Scalar elements to combine into vector
		 * @return Node representing the vector
		 */
		Node* vector_build(const std::vector<Node*>& elements);

		/**
		 * @brief Create a vector splat node from a scalar
		 * @param scalar Scalar value to replicate
		 * @param lane_count Number of lanes in the vector
		 * @return Node representing the vector
		 */
		Node* vector_splat(Node* scalar, std::uint32_t lane_count);

		/**
		 * @brief Extract scalar from vector
		 * @param vector Vector to extract from
		 * @param index Index of element to extract
		 * @return Node representing the extracted scalar
		 */
		Node* vector_extract(Node* vector, std::uint32_t index);

		/**
		 * @brief Create type cast node
		 * @tparam TargetType Target type for the cast
		 * @param value Value to cast
		 * @return Node representing the cast operation
		 */
		template<DataType TargetType>
		Node* cast(Node* value);

	private:
		Module &module;
		Region *current_region;

		/**
		 * @brief Create a new node with specified type
		 * @param type Node type
		 * @param result_type Result data type
		 * @return Newly created node
		 */
		Node *create_node(NodeType type, DataType result_type = DataType::VOID);

		/**
		 * @brief Connect inputs to a node
		 * @param node Node to connect inputs to
		 * @param inputs Input nodes
		 */
		static void connect_inputs(Node *node, const std::vector<Node *> &inputs);

		template<DataType T>
		friend class FunctionBuilder;
		template<DataType T>
		friend class BlockBuilder;
		friend class StoreHelper;
	};

	/**
	 * @brief Builder for function definitions
	 */
	template<DataType>
	class FunctionBuilder
	{
	public:
		/**
		 * @brief Construct function builder
		 * @param builder Parent builder
		 * @param function_node Function node being built
		 * @param function_region Function's region
		 */
		FunctionBuilder(Builder &builder, Node *function_node, Region *function_region);

		/**
		 * @brief Mark function as exported (external linkage)
		 * @return Reference to this builder for chaining
		 */
		FunctionBuilder &exported();

		/**
		 * @brief Mark function as driver (entry point)
		 * @return Reference to this builder for chaining
		 */
		FunctionBuilder &driver();

		/**
		 * @brief Mark function as extern (imported)
		 * @return Reference to this builder for chaining
		 */
		FunctionBuilder &imported();

		/**
		 * @brief Mark function as volatile (no optimization)
		 * @return Reference to this builder for chaining
		 */
		FunctionBuilder &keep();

		/**
		 * @brief Add a parameter to the function
		 * @tparam ParamType Type of the parameter
		 * @param name Parameter name
		 * @return Node representing the parameter
		 */
		template<DataType ParamType>
		Node *param(std::string_view name);

		/**
		 * @brief Add a pointer parameter to the function
		 * @tparam PointeeType Type that the pointer points to
		 * @param name Parameter name
		 * @return Node representing the pointer parameter
		 */
		template<DataType PointeeType>
		Node *param_ptr(std::string_view name);

		/**
		 * @brief Define the function body
		 * @param body_func Lambda defining the function body
		 * @return The function node
		 */
		Node *body(const std::function<Node*(Builder &)> &body_func);

	private:
		Builder &builder;
		Node *function;
		Region *region;
		std::vector<Node *> parameters;
	};

	/**
	 * @brief Builder for basic blocks
	 * @tparam ReturnType Return type of the block
	 */
	template<DataType ReturnType>
	class BlockBuilder
	{
	public:
		/**
		 * @brief Construct block builder
		 * @param builder Parent builder
		 * @param block_region Block's region
		 */
		BlockBuilder(Builder &builder, Region *block_region);

		/**
		 * @brief Define the block body
		 * @param block_func Lambda defining the block body
		 * @return The return value of the block
		 */
		Node *operator()(const std::function<Node*(Builder &)> &block_func);

		/**
		 * @brief Get the entry node for this block
		 * @return Entry node for control flow targeting
		 */
		Node* entry() const
		{
			return region->entry();
		}

	private:
		Builder &builder;
		Region *region;
	};

	/**
	 * @brief Helper class for fluent store operations
	 */
	class StoreHelper
	{
	public:
		/**
		 * @brief Construct store helper
		 * @param builder Parent builder
		 * @param value Value to store
		 */
		StoreHelper(Builder &builder, Node *value);

		/**
		 * @brief Store to named memory location
		 * @param location Memory location
		 * @return Store node
		 */
		Node *to(Node *location);

		/**
		 * @brief Store to memory location with offset
		 * @param location Base memory location
		 * @param offset Byte offset
		 * @return Store node
		 */
		Node *to(Node *location, Node *offset);

		/**
		 * @brief Store through pointer
		 * @param pointer Pointer to store through
		 * @return Store node
		 */
		Node *through(Node *pointer);

		/**
		 * @brief Store to memory location atomically
		 * @param location Memory location
		 * @param ordering Memory ordering (optional, defaults to SEQ_CST)
		 * @return Atomic store node
		 */
		Node *to_atomic(Node *location, AtomicOrdering ordering = AtomicOrdering::SEQ_CST);

		/**
		 * @brief Store through pointer atomically
		 * @param pointer Pointer to store through
		 * @param ordering Memory ordering (optional, defaults to SEQ_CST)
		 * @return Atomic store node
		 */
		Node *through_atomic(Node *pointer, AtomicOrdering ordering = AtomicOrdering::SEQ_CST);

	private:
		Builder &builder;
		Node *value;
	};

	/* template implementations */

	template<DataType ReturnType>
	FunctionBuilder<
		ReturnType>::FunctionBuilder(Builder &builder, Node *function_node, Region *function_region) : builder(builder),
		function(function_node), region(function_region) {}

	template<DataType ReturnType>
	FunctionBuilder<ReturnType> &FunctionBuilder<ReturnType>::exported()
	{
		function->traits |= NodeTraits::EXPORT;
		return *this;
	}

	template<DataType ReturnType>
	FunctionBuilder<ReturnType> &FunctionBuilder<ReturnType>::driver()
	{
		function->traits |= NodeTraits::DRIVER;
		return *this;
	}

	template<DataType ReturnType>
	FunctionBuilder<ReturnType> &FunctionBuilder<ReturnType>::imported()
	{
		function->traits |= NodeTraits::EXTERN;
		return *this;
	}

	template<DataType ReturnType>
	FunctionBuilder<ReturnType> &FunctionBuilder<ReturnType>::keep()
	{
		function->traits |= NodeTraits::VOLATILE;
		return *this;
	}

	template<DataType ReturnType>
	template<DataType ParamType>
	Node *FunctionBuilder<ReturnType>::param(std::string_view name)
	{
		Node *param_node = builder.create_node(NodeType::PARAM, ParamType);
		param_node->str_id = builder.module.intern_str(name);
		parameters.push_back(param_node);
		return param_node;
	}

	template<DataType ReturnType>
	template<DataType>
	Node *FunctionBuilder<ReturnType>::param_ptr(const std::string_view name)
	{
		Node *param_node = builder.create_node(NodeType::PARAM, DataType::POINTER);
		param_node->str_id = builder.module.intern_str(name);

		/* set up pointer type data */
		DataTraits<DataType::POINTER>::value ptr_data = {};
		ptr_data.pointee = nullptr; /* will be resolved during type checking */
		ptr_data.addr_space = 0;
		param_node->value.set<decltype(ptr_data), DataType::POINTER>(ptr_data);

		parameters.push_back(param_node);
		return param_node;
	}

	template<DataType ReturnType>
	Node *FunctionBuilder<ReturnType>::body(const std::function<Node*(Builder &)> &body_func)
	{
		Region *old_region = builder.get_insertion_point();
		builder.set_insertion_point(region);
		builder.create_node(NodeType::ENTRY);
		body_func(builder);
		builder.set_insertion_point(old_region);
		return function;
	}

	template<DataType ReturnType>
	BlockBuilder<ReturnType>::BlockBuilder(Builder &builder, Region *block_region) : builder(builder),
		region(block_region) {}

	template<DataType ReturnType>
	FunctionBuilder<ReturnType> Builder::function(std::string_view name)
	{
		Node *func_node = create_node(NodeType::FUNCTION, DataType::FUNCTION);
		func_node->str_id = module.intern_str(name);

		Region *func_region = module.create_region(name, current_region);
		return FunctionBuilder<ReturnType>(*this, func_node, func_region);
	}

	template<DataType ReturnType>
	BlockBuilder<ReturnType> Builder::block(std::string_view name)
	{
		Region *block_region = module.create_region(name, current_region);
		return BlockBuilder<ReturnType>(*this, block_region);
	}

	template<typename T>
	Node *Builder::lit(T value)
	{
		Node *node = nullptr;

		if constexpr (std::is_same_v<T, bool>)
		{
			node = create_node(NodeType::LIT, DataType::BOOL);
			node->value.set<T, DataType::BOOL>(value);
		}
		else if constexpr (std::is_same_v<T, std::int8_t>)
		{
			node = create_node(NodeType::LIT, DataType::INT8);
			node->value.set<T, DataType::INT8>(value);
		}
		else if constexpr (std::is_same_v<T, std::int16_t>)
		{
			node = create_node(NodeType::LIT, DataType::INT16);
			node->value.set<T, DataType::INT16>(value);
		}
		else if constexpr (std::is_same_v<T, std::int32_t>)
		{
			node = create_node(NodeType::LIT, DataType::INT32);
			node->value.set<T, DataType::INT32>(value);
		}
		else if constexpr (std::is_same_v<T, std::int64_t>)
		{
			node = create_node(NodeType::LIT, DataType::INT64);
			node->value.set<T, DataType::INT64>(value);
		}
		else if constexpr (std::is_same_v<T, std::uint8_t>)
		{
			node = create_node(NodeType::LIT, DataType::UINT8);
			node->value.set<T, DataType::UINT8>(value);
		}
		else if constexpr (std::is_same_v<T, std::uint16_t>)
		{
			node = create_node(NodeType::LIT, DataType::UINT16);
			node->value.set<T, DataType::UINT16>(value);
		}
		else if constexpr (std::is_same_v<T, std::uint32_t>)
		{
			node = create_node(NodeType::LIT, DataType::UINT32);
			node->value.set<T, DataType::UINT32>(value);
		}
		else if constexpr (std::is_same_v<T, std::uint64_t>)
		{
			node = create_node(NodeType::LIT, DataType::UINT64);
			node->value.set<T, DataType::UINT64>(value);
		}
		else if constexpr (std::is_same_v<T, float>)
		{
			node = create_node(NodeType::LIT, DataType::FLOAT32);
			node->value.set<T, DataType::FLOAT32>(value);
		}
		else if constexpr (std::is_same_v<T, double>)
		{
			node = create_node(NodeType::LIT, DataType::FLOAT64);
			node->value.set<T, DataType::FLOAT64>(value);
		}
		else
		{
			static_assert(sizeof(T) == 0, "unsupported literal type");
		}

		return node;
	}

	template<DataType ElementType>
	Node *Builder::alloc(Node *count)
	{
		Node *node = create_node(NodeType::ALLOC, ElementType);
		connect_inputs(node, { count });
		return node;
	}

	template<DataType TargetType>
	Node* Builder::cast(Node* value)
	{
		if (!value)
			throw std::invalid_argument("cast value cannot be null");

		Node* node = create_node(NodeType::CAST, TargetType);
		connect_inputs(node, {value});
		return node;
	}

	template<DataType ReturnType>
	Node *BlockBuilder<ReturnType>::operator()(const std::function<Node*(Builder &)> &block_func)
	{
		Region *old_region = builder.get_insertion_point();
		builder.set_insertion_point(region);
		builder.create_node(NodeType::ENTRY);
		Node *result = block_func(builder);
		builder.set_insertion_point(old_region);
		return result;
	}
}
