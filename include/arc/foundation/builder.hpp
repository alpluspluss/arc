/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <functional>
#include <initializer_list>
#include <string_view>
#include <type_traits>
#include <arc/foundation/module.hpp>
#include <arc/foundation/node.hpp>
#include <arc/foundation/region.hpp>
#include <arc/foundation/typed-data.hpp>

namespace arc
{
	class Builder;
	class FunctionBuilder;
	class BlockBuilder;

	/**
	 * @brief RAII scope guard for automatic insertion point management
	 */
	class ScopeGuard
	{
	public:
		/**
		 * @brief Construct a scope guard that manages insertion point
		 * @param builder The builder to manage
		 * @param new_region The region to set as current
		 */
		ScopeGuard(Builder& builder, Region* new_region);

		/**
		 * @brief Destructor that restores the previous insertion point
		 */
		~ScopeGuard();

		ScopeGuard(const ScopeGuard&) = delete;
		ScopeGuard& operator=(const ScopeGuard&) = delete;
		ScopeGuard(ScopeGuard&&) = default;
		ScopeGuard& operator=(ScopeGuard&&) = delete;

	private:
		Builder& bldr; /** @brief The builder to manage */
		Region* old_rgn; /** @brief Previous insertion region */
	};

	/**
	 * @brief Type-safe store operation builder with fluent interface
	 */
	class StoreOperation
	{
	public:
		/**
		 * @brief Construct a store operation
		 * @param builder The builder instance
		 * @param value The value to store
		 */
		StoreOperation(Builder& builder, Node* value);

		/**
		 * @brief Complete the store operation to an address
		 * @param address The target address
		 * @return The created store node
		 */
		Node* to(Node* address);

		/**
		 * @brief Complete the store operation with pointer arithmetic
		 * @param base The base address
		 * @param offset The offset node
		 * @return The created store node
		 */
		Node* to(Node* base, Node* offset);

		/**
		 * @brief Complete the store operation with constant offset
		 * @param base The base address
		 * @param offset The constant offset
		 * @return The created store node
		 */
		Node* to(Node* base, std::int32_t offset);

	private:
		Builder& bldr; /** @brief The builder instance */
		Node* val; /** @brief The value to store */
	};

	/**
	 * @brief Type-safe function builder with fluent interface
	 */
	class FunctionBuilder
	{
	public:
		/**
		 * @brief Construct a function builder
		 * @param builder The parent builder
		 * @param name The function name
		 */
		FunctionBuilder(Builder& builder, std::string_view name);

		/**
		 * @brief Add a parameter to the function
		 * @tparam T The parameter type
		 * @param name The parameter name
		 * @return The parameter node
		 */
		template<DataType T>
		Node* param(std::string_view name)
		{
			Node* param_node = create_parameter<T>(name);
			params.push_back(param_node);
			return param_node;
		}

		/**
		 * @brief Add a pointer parameter to the function
		 * @tparam T The pointee type
		 * @param name The parameter name
		 * @return The parameter node
		 */
		template<DataType T>
		Node* param_ptr(std::string_view name)
		{
			Node* param_node = create_parameter_ptr<T>(name);
			params.push_back(param_node);
			return param_node;
		}

		/**
		 * @brief Set the return type of the function
		 * @tparam T The return type
		 * @return Reference to this builder for chaining
		 */
		template<DataType T>
		FunctionBuilder& returns()
		{
			ret_type = T;
			return *this;
		}

		/**
		 * @brief Define the function body with scoped builder
		 * @tparam F The function type
		 * @param body_func The function body lambda
		 * @return The function node
		 */
		template<typename F>
		Node* body(F&& body_func);

	private:
		Builder& bldr; /** @brief The parent builder */
		std::string_view fn_name; /** @brief Function name */
		DataType ret_type = DataType::VOID; /** @brief Return type */
		Node* fn_node = nullptr; /** @brief Function node */
		u8slice<Node*> params; /** @brief Parameter nodes */

		Region* create_function_region();

		template<DataType T>
		Node* create_parameter(std::string_view name);

		template<DataType T>
		Node* create_parameter_ptr(std::string_view name);
	};

	/**
	 * @brief Scoped block builder with automatic insertion point management
	 */
	class BlockBuilder
	{
	public:
		/**
		 * @brief Construct a block builder
		 * @param builder The parent builder
		 * @param region The region for this block
		 */
		BlockBuilder(Builder& builder, Region* region);

		/* literal creation with automatic type deduction */
		Node* lit(std::int8_t value) { return create_literal<DataType::INT8>(value); }
		Node* lit(std::int16_t value) { return create_literal<DataType::INT16>(value); }
		Node* lit(std::int32_t value) { return create_literal<DataType::INT32>(value); }
		Node* lit(std::int64_t value) { return create_literal<DataType::INT64>(value); }
		Node* lit(std::uint8_t value) { return create_literal<DataType::UINT8>(value); }
		Node* lit(std::uint16_t value) { return create_literal<DataType::UINT16>(value); }
		Node* lit(std::uint32_t value) { return create_literal<DataType::UINT32>(value); }
		Node* lit(std::uint64_t value) { return create_literal<DataType::UINT64>(value); }
		Node* lit(float value) { return create_literal<DataType::FLOAT32>(value); }
		Node* lit(double value) { return create_literal<DataType::FLOAT64>(value); }
		Node* lit(bool value) { return create_literal<DataType::BOOL>(value); }
		Node* lit(std::string_view value) { return create_literal<DataType::STRING>(value); }
		Node* lit(const char* value) { return lit(std::string_view(value)); }

		/**
		 * @brief Type-safe allocation
		 * @tparam T The element type
		 * @param count The number of elements (optional)
		 * @return The allocation node
		 */
		template<DataType T>
		Node* alloc(Node* count = nullptr)
		{
			if (!count)
			{
				count = lit(1);
			}
			return create_alloc_node<T>(count);
		}

		/**
		 * @brief Load from memory location or pointer
		 * @param address The address to load from
		 * @return The loaded value
		 */
		Node* load(Node* address);

		/**
		 * @brief Create a store operation
		 * @param value The value to store
		 * @return Store operation for fluent interface
		 */
		StoreOperation store(Node* value);

		/* arithmetic operations */
		Node* add(Node* lhs, Node* rhs);
		Node* sub(Node* lhs, Node* rhs);
		Node* mul(Node* lhs, Node* rhs);
		Node* div(Node* lhs, Node* rhs);
		Node* mod(Node* lhs, Node* rhs);

		/* bitwise operations */
		Node* band(Node* lhs, Node* rhs);
		Node* bor(Node* lhs, Node* rhs);
		Node* bxor(Node* lhs, Node* rhs);
		Node* bnot(Node* value);
		Node* bshl(Node* lhs, Node* rhs);
		Node* bshr(Node* lhs, Node* rhs);

		/* comparison operations */
		Node* eq(Node* lhs, Node* rhs);
		Node* neq(Node* lhs, Node* rhs);
		Node* lt(Node* lhs, Node* rhs);
		Node* lte(Node* lhs, Node* rhs);
		Node* gt(Node* lhs, Node* rhs);
		Node* gte(Node* lhs, Node* rhs);

		/* pointer operations */
		Node* ptr_add(Node* base, Node* offset);
		Node* addr_of(Node* value);

		/* control flow */
		Node* ret(Node* value = nullptr);
		Node* call(Node* function, std::initializer_list<Node*> args = {});

		/**
		 * @brief Create a scoped block
		 * @tparam F The function type
		 * @param name The block name
		 * @param block_func The block body lambda
		 */
		template<typename F>
		auto block(std::string_view name, F&& block_func)
		{
			Region* block_region = create_child_region(name);
			ScopeGuard scope(bldr, block_region);
			BlockBuilder block_builder(bldr, block_region);

			if constexpr (std::is_void_v<std::invoke_result_t<F, BlockBuilder&>>)
			{
				block_func(block_builder);
			}
			else
			{
				return block_func(block_builder);
			}
		}

		/**
		 * @brief Create conditional blocks
		 * @tparam TrueF The true branch function type
		 * @tparam FalseF The false branch function type
		 * @param condition The condition to evaluate
		 * @param true_func The true branch lambda
		 * @param false_func The false branch lambda
		 */
		template<typename TrueF, typename FalseF>
		void if_else(Node* condition, TrueF&& true_func, FalseF&& false_func)
		{
			auto true_block = create_child_region("if_true");
			auto false_block = create_child_region("if_false");
			auto merge_block = create_child_region("if_merge");

			create_branch(condition, true_block, false_block);

			{
				ScopeGuard scope(bldr, true_block);
				BlockBuilder true_builder(bldr, true_block);
				true_func(true_builder);
				true_builder.jump(merge_block);
			}

			{
				ScopeGuard scope(bldr, false_block);
				BlockBuilder false_builder(bldr, false_block);
				false_func(false_builder);
				false_builder.jump(merge_block);
			}

			set_insertion_point(merge_block);
		}

	private:
		Builder& bldr; /** @brief The parent builder */
		Region* rgn; /** @brief The region for this block */

		template<DataType T, typename ValueT>
		Node* create_literal(ValueT value);

		template<DataType T>
		Node* create_alloc_node(Node* count);

		Node* create_load_node(Node* address);
		Node* create_binary_op(NodeType op, Node* lhs, Node* rhs);
		Node* create_unary_op(NodeType op, Node* operand);
		Node* create_comparison(NodeType op, Node* lhs, Node* rhs);
		Node* create_return(Node* value);
		Node* create_call(Node* function, const std::vector<Node*>& args);
		Node* create_branch(Node* condition, Region* true_target, Region* false_target);
		Node* jump(Region* target);

		Region* create_child_region(std::string_view name);
		void set_insertion_point(Region* new_region);
	};

	/**
	 * @brief Main builder class with fluent interface
	 */
	class Builder
	{
	public:
		/**
		 * @brief Construct a builder for the given module
		 * @param module The module to build into
		 */
		explicit Builder(Module& module);

		/**
		 * @brief Create a function with fluent interface
		 * @param name The function name
		 * @return Function builder for defining the function
		 */
		FunctionBuilder function(std::string_view name);

		/**
		 * @brief Create a standalone block for testing
		 * @tparam F The function type
		 * @param name The block name
		 * @param block_func The block body lambda
		 */
		template<typename F>
		auto block(std::string_view name, F&& block_func)
		{
			Region* block_region = create_region(name);
			ScopeGuard scope(*this, block_region);
			BlockBuilder block_builder(*this, block_region);

			if constexpr (std::is_void_v<std::invoke_result_t<F, BlockBuilder&>>)
			{
				block_func(block_builder);
			}
			else
			{
				return block_func(block_builder);
			}
		}

		/**
		 * @brief Get the module being built
		 * @return Reference to the module
		 */
		Module& module();

		/**
		 * @brief Get the current insertion region
		 * @return Pointer to current region
		 */
		Region* current_region();

		/**
		 * @brief Set the current insertion region
		 * @param region The region to set as current
		 */
		void set_current_region(Region* region);

		/**
		 * @brief Create a node with the given type
		 * @param type The node type
		 * @param result_type The result type
		 * @return The created node
		 */
		Node* create_node(NodeType type, DataType result_type = DataType::VOID);

		/**
		 * @brief Connect nodes with input relationships
		 * @param user The node that uses the inputs
		 * @param inputs The input nodes
		 */
		void connect_nodes(Node* user, std::initializer_list<Node*> inputs);

	private:
		Module& mod; /** @brief The module being built */
		Region* curr_rgn = nullptr; /** @brief Current insertion region */

		Region* create_region(std::string_view name, Region* parent = nullptr);

		friend class ScopeGuard;
		friend class StoreOperation;
		friend class FunctionBuilder;
		friend class BlockBuilder;
	};

	template<DataType T, typename ValueT>
	Node *BlockBuilder::create_literal(ValueT value)
	{
		Node* lit_node = bldr.create_node(NodeType::LIT, T);
		if constexpr (T == DataType::STRING)
		{
			StringTable::StringId str_id = bldr.module().intern_str(value);
			lit_node->value.set<StringTable::StringId, DataType::STRING>(str_id);
		}
		else
		{
			lit_node->value.set<typename DataTraits<T>::value, T>(value);
		}
		return lit_node;
	}

	template<DataType T>
	Node* BlockBuilder::create_alloc_node(Node* count)
	{
		Node* alloc_node = bldr.create_node(NodeType::ALLOC, T);
		bldr.connect_nodes(alloc_node, {count});
		return alloc_node;
	}

	template<typename F>
	Node* FunctionBuilder::body(F&& body_func)
	{
		Region* fn_region = create_function_region();
		ScopeGuard scope(bldr, fn_region);

		BlockBuilder fb(bldr, fn_region);

		if constexpr (std::is_void_v<std::invoke_result_t<F, BlockBuilder&>>)
		{
			body_func(fb);
			return fn_node;
		}
		else
		{
			Node* result = body_func(fb);
			fb.ret(result);
			return fn_node;
		}
	}

	template<DataType T>
	Node* FunctionBuilder::create_parameter(std::string_view name)
	{
		Node* param = bldr.create_node(NodeType::PARAM, T);
		param->str_id = bldr.module().intern_str(name);
		return param;
	}

	template<DataType T>
	Node* FunctionBuilder::create_parameter_ptr(std::string_view name)
	{
		Node* param = bldr.create_node(NodeType::PARAM, DataType::POINTER);
		param->str_id = bldr.module().intern_str(name);
		/* set up pointer type data */
		typename DataTraits<DataType::POINTER>::value ptr_data;
		ptr_data.pointee = nullptr; /* will be resolved later */
		ptr_data.addr_space = 0;
		param->value.set<typename DataTraits<DataType::POINTER>::value, DataType::POINTER>(ptr_data);
		return param;
	}
}
