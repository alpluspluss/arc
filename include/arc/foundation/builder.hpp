/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <functional>
#include <string_view>
#include <vector>
#include <arc/foundation/module.hpp>
#include <arc/foundation/node.hpp>
#include <arc/foundation/region.hpp>
#include <arc/foundation/typed-data.hpp>
#include <arc/support/inference.hpp>

namespace arc
{
	class StoreHelper;
	class Region;
	class StructBuilder;
	template<DataType T>
	class FunctionBuilder;
	template<DataType T>
	class BlockBuilder;
	template<DataType T>
		requires(T == DataType::FUNCTION || T == DataType::STRUCT)
	class Opaque;

	/**
	 * @brief Function traits for FunctionBuilder
	 */
	template<typename F>
	struct FunctionTraits;

	template<typename R, typename... Args>
	struct FunctionTraits<R(*)(Args...)>
	{
		static constexpr auto arity = sizeof...(Args);
		using return_type = R;

		template<std::size_t N>
		using arg = std::tuple_element_t<N, std::tuple<Args...> >;
	};

	template<typename R, typename C, typename... Args>
	struct FunctionTraits<R(C::*)(Args...) const>
	{
		static constexpr std::size_t arity = sizeof...(Args);
		using return_type = R;

		template<std::size_t N>
		using arg = std::tuple_element_t<N, std::tuple<Args...> >;
	};

	template<typename F>
	struct FunctionTraits : FunctionTraits<decltype(&F::operator())> {};

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
		 * @brief Create a struct allocation node
		 * @param type_def Type definition
		 * @return Node representing the allocation
		 */
		Node* alloc(const TypedData& type_def);

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
		Node *store(Node *value, Node *location);

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
		Node *ptr_store(Node *value, Node *pointer);

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
		 * @brief Create modulus node
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing lhs % rhs
		 */
		Node *mod(Node *lhs, Node *rhs);

		/**
		 * @brief Create bitwise AND node
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing lhs & rhs
		 */
		Node *band(Node *lhs, Node *rhs);

		/**
		 * @brief Create bitwise OR node
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing lhs | rhs
		 */
		Node *bor(Node *lhs, Node *rhs);

		/**
		 * @brief Create bitwise XOR node
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing lhs ^ rhs
		 */
		Node *bxor(Node *lhs, Node *rhs);

		/**
		 * @brief Create bitwise NOT node
		 * @param value Operand to invert
		 * @return Node representing ~value
		 */
		Node *bnot(Node *value);

		/**
		 * @brief Create bitwise shift left node
		 * @param lhs Left operand
		 * @param rhs Right operand (shift amount)
		 * @return Node representing lhs << rhs
		 */
		Node *bshl(Node *lhs, Node *rhs);

		/**
		 * @brief Create bitwise shift right node
		 * @param lhs Left operand
		 * @param rhs Right operand (shift amount)
		 * @return Node representing lhs >> rhs
		 */
		Node *bshr(Node *lhs, Node *rhs);

		/**
		 * @brief Create equality comparison node
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing lhs == rhs
		 */
		Node *eq(Node *lhs, Node *rhs);

		/**
		 * @brief Create inequality comparison node
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing lhs != rhs
		 */
		Node *neq(Node *lhs, Node *rhs);

		/**
		 * @brief Create less than comparison node
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing lhs < rhs
		 */
		Node *lt(Node *lhs, Node *rhs);

		/**
		 * @brief Create less than or equal comparison node
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing lhs <= rhs
		 */
		Node *lte(Node *lhs, Node *rhs);

		/**
		 * @brief Create greater than comparison node
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing lhs > rhs
		 */
		Node *gt(Node *lhs, Node *rhs);

		/**
		 * @brief Create greater than or equal comparison node
		 * @param lhs Left operand
		 * @param rhs Right operand
		 * @return Node representing lhs >= rhs
		 */
		Node *gte(Node *lhs, Node *rhs);

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
		Node *vector_build(const std::vector<Node *> &elements);

		/**
		 * @brief Create a vector splat node from a scalar
		 * @param scalar Scalar value to replicate
		 * @param lane_count Number of lanes in the vector
		 * @return Node representing the vector
		 */
		Node *vector_splat(Node *scalar, std::uint32_t lane_count);

		/**
		 * @brief Extract scalar from vector
		 * @param vector Vector to extract from
		 * @param index Index of element to extract
		 * @return Node representing the extracted scalar
		 */
		Node *vector_extract(Node *vector, std::uint32_t index);

		/**
		 * @brief Create type cast node
		 * @tparam TargetType Target type for the cast
		 * @param value Value to cast
		 * @return Node representing the cast operation
		 */
		template<DataType TargetType>
		Node *cast(Node *value);

		/**
		 * @brief Access a specific offset of the struct
		 */
		Node* struct_field(Node* struct_obj, const std::string& field_name);

		/**
		 * @brief Create a struct type builder
		 * @param name Struct name
		 * @return Struct builder for fluent API
		 */
		StructBuilder struct_type(std::string_view name);

		template<DataType ElementType, std::uint32_t Count>
		Node* array_alloc(std::uint32_t n = 1)
		{
			Node* count_lit = lit(n);
			Node* alloc_node = create_node(NodeType::ALLOC, DataType::ARRAY);

			DataTraits<DataType::ARRAY>::value arr_data;
			arr_data.elem_type = ElementType;
			arr_data.count = Count;
			arr_data.elements = {};
			alloc_node->value.set<decltype(arr_data), DataType::ARRAY>(arr_data);
			connect_inputs(alloc_node, { count_lit });
			return alloc_node;
		}


		template<DataType T>
			requires(T == DataType::FUNCTION /* || T == DataType::STRUCT */)
		Opaque<T> opaque_t(std::string_view name);

		/**
		 * @brief Index into an array
		 * @param array Array to index
		 * @param index Index value
		 * @return Node representing the indexed element
		 */
		Node* array_index(Node* array, Node* index);

		/**
		 * @brief Create a new node with specified type
		 * @param type Node type
		 * @param result_type Result data type
		 * @return Newly created node
		 */
		Node *create_node(NodeType type, DataType result_type = DataType::VOID);

		/**
		 * @brief Create a FROM node that merges values from different control flow paths
		 * @param sources Vector of source nodes from different paths
		 * @return Node representing the merged value
		 */
		Node *from(const std::vector<Node *> &sources);

	private:
		Module &module;
		Region *current_region;

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
		friend class StructBuilder;
		template<DataType T>
			requires(T == DataType::FUNCTION || T == DataType::STRUCT)
		friend class Opaque;
	};

	template<DataType T>
		requires(T == DataType::FUNCTION || T == DataType::STRUCT)
	class Opaque
	{
	public:
		Opaque(Builder& b, Node* node) : opaque_node(node), builder(b) {}

		[[nodiscard]] Node* node() const
		{
			return opaque_node;
		}

	protected:
		Node* opaque_node;
		Builder& builder;
	};

	template<>
	class Opaque<DataType::FUNCTION>
	{
	public:
		Opaque(Builder& b, Node* node) : opaque_node(node), builder(b) {}

		[[nodiscard]] Node* node() const
		{
			return opaque_node;
		}

		template<DataType ReturnType>
		FunctionBuilder<ReturnType> function()
		{
			opaque_node->ir_type = NodeType::FUNCTION;
			opaque_node->type_kind = DataType::FUNCTION;

			DataTraits<DataType::FUNCTION>::value fn_data;
			ach::shared_allocator<TypedData> alloc;
			TypedData* ret_type = alloc.allocate(1);
			std::construct_at(ret_type);
			set_t<ReturnType>(*ret_type);
			fn_data.return_type = ret_type;
			opaque_node->value.set<decltype(fn_data), DataType::FUNCTION>(fn_data);

			const std::string_view func_name = builder.module.strtable().get(opaque_node->str_id);
			Region* func_region = builder.module.create_region(func_name, builder.get_insertion_point());
			return FunctionBuilder<ReturnType>(builder, opaque_node, func_region);
		}

	private:
		Node* opaque_node;
		Builder& builder;
	};

	class StructBuilder
	{
	public:
		/**
		 * @brief Construct struct builder
		 * @param builder Parent builder reference
		 * @param name Struct name
		 */
		StructBuilder(Builder& builder, std::string_view name);

		/**
		 * @brief Add a field with primitive type
		 * @param name Field name
		 * @param type Field type
		 * @param type_data Additional type information for complex types; Defaults to an empty TypedData
		 * @return Reference to this builder for chaining
		 */
		StructBuilder& field(std::string_view name, DataType type, const TypedData& type_data = {});

		/**
		 * @brief Add a pointer field; a convenience method for pointer types
		 * @param name Field name
		 * @param pointee_type Type the pointer points to. Can be nullptr for forward refs
		 * @param addr_space Address space (defaults to 0)
		 * @return Reference to this builder for chaining
		 */
		StructBuilder& field_ptr(std::string_view name, Node* pointee_type = nullptr, std::uint32_t addr_space = 0);

		/**
		 * @brief Add a self-referential pointer field
		 * @param name Field name
		 * @param addr_space Address space
		 * @return Reference to this builder for chaining
		 *
		 * @note: Address space is optional; defaults to 0
		 */
		StructBuilder& self_ptr(std::string_view name, std::uint32_t addr_space = 0);

		/**
		 * @brief Build the struct type
		 * @param alignment Struct alignment in bytes; optional
		 * @return TypedData representing the struct type
		 */
		TypedData build(std::uint32_t alignment = 8);

		/**
		 * @brief Mark the struct as packed (no padding)
		 * @return Reference to this builder for chaining
		 */
		StructBuilder& packed();

	private:
		Builder& builder;
		u8slice<std::tuple<StringTable::StringId, DataType, TypedData>> fields{};
		StringTable::StringId struct_name_id;
		bool is_packed = false;
	};

	/**
	 * @brief Builder for function definitions
	 */
	template<DataType /* ReturnType */>
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
		FunctionBuilder &param(std::string_view name);

		/**
		 * @brief Add a pointer parameter to the function
		 * @tparam PointeeType Type that the pointer points to
		 * @param name Parameter name
		 * @param pointee_node The node the parameter points to
		 * @return Node representing the pointer parameter
		 */
		template<DataType PointeeType>
		FunctionBuilder &param_ptr(std::string_view name, Node *pointee_node);

		/**
		 * @brief Define the function body
		 * @param body_func Lambda defining the function body
		 * @return The function node
		 */
		template<typename F>
		Node *body(F &&body_func);

	private:
		Builder &builder;
		Node *function;
		Region *region;
		std::vector<Node *> parameters;

		template<typename F, std::size_t... Is>
		auto call_with_params(F &&func, std::index_sequence<Is...>)
		{
			return func(builder, parameters[Is]...);
		}
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
		[[nodiscard]] Node *entry() const
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
	FunctionBuilder<ReturnType> &FunctionBuilder<ReturnType>::param(std::string_view name)
	{
		Node *param_node = builder.create_node(NodeType::PARAM, ParamType);
		param_node->str_id = builder.module.intern_str(name);
		function->inputs.push_back(param_node);
		param_node->users.push_back(function);
		region->append(param_node);
		parameters.push_back(param_node);
		return *this;
	}

	template<DataType ReturnType>
	template<DataType PointeeType>
	FunctionBuilder<ReturnType> &FunctionBuilder<ReturnType>::param_ptr(const std::string_view name, Node *pointee_node)
	{
		if (!pointee_node)
			throw std::invalid_argument("pointee node cannot be null");

		if (pointee_node->type_kind != PointeeType)
			throw std::invalid_argument("pointee node type must match template parameter");

		Node *param_node = builder.create_node(NodeType::PARAM, DataType::POINTER);
		param_node->str_id = builder.module.intern_str(name);

		function->inputs.push_back(param_node);
		param_node->users.push_back(function);
		region->append(param_node);

		DataTraits<DataType::POINTER>::value ptr_data = {};
		ptr_data.pointee = pointee_node;
		ptr_data.addr_space = 0;
		param_node->value.set<decltype(ptr_data), DataType::POINTER>(ptr_data);

		parameters.push_back(param_node);
		return *this;
	}

	template<DataType ReturnType>
	template<typename F>
	Node *FunctionBuilder<ReturnType>::body(F &&body_func)
	{
		using traits = FunctionTraits<std::decay_t<F>>;
		constexpr std::size_t expected_params = traits::arity - 1;
		if (parameters.size() != expected_params)
			throw std::invalid_argument("lambda parameter count doesn't match declared parameters");

		DataTraits<DataType::FUNCTION>::value fn_data = {};
		ach::shared_allocator<TypedData> alloc;
		TypedData* ret_type = alloc.allocate(1);
		std::construct_at(ret_type);
		arc::set_t<ReturnType>(*ret_type);
		fn_data.return_type = ret_type;
		function->value.set<decltype(fn_data), DataType::FUNCTION>(fn_data);

		Region *old_region = builder.get_insertion_point();
		builder.set_insertion_point(region);
		call_with_params(std::forward<F>(body_func),
						 std::make_index_sequence<expected_params>{});

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
		func_node->ir_type = NodeType::FUNCTION;
		func_node->type_kind = DataType::FUNCTION;
		module.add_fn(func_node);
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
	Node *Builder::cast(Node *value)
	{
		if (!value)
			throw std::invalid_argument("cast value cannot be null");

		Node *node = create_node(NodeType::CAST, TargetType);
		connect_inputs(node, { value });
		return node;
	}

	template<DataType T>
		requires(T == DataType::FUNCTION /* || T == DataType::STRUCT */)
	Opaque<T> Builder::opaque_t(std::string_view name)
	{
		if constexpr (T == DataType::FUNCTION)
		{
			Node* func_node = create_node(NodeType::FUNCTION, DataType::FUNCTION);
			func_node->str_id = module.intern_str(name);
			DataTraits<DataType::FUNCTION>::value fn_data;
			fn_data.return_type = nullptr; /* will be set later by ::function<ReturnType>() */
			func_node->value.set<decltype(fn_data), DataType::FUNCTION>(fn_data);
			module.add_fn(func_node);
			return Opaque<DataType::FUNCTION>(*this, func_node);
		}
		std::unreachable();
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
