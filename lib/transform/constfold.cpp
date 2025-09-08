/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <cmath>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/foundation/region.hpp>
#include <arc/support/algorithm.hpp>
#include <arc/support/allocator.hpp>
#include <arc/transform/constfold.hpp>

namespace arc
{
	namespace
	{
		/**
		 * @brief Create a literal node with specified value and type
		 * @tparam T C++ type of the literal value
		 * @param value The literal value to store
		 * @param region Region where the literal should be placed
		 * @return Newly created literal node
		 */
		template<typename T, DataType DT>
		Node *create_literal(T value, Region *region)
		{
			ach::shared_allocator<Node> alloc;
			Node *lit = alloc.allocate(1);
			std::construct_at(lit);
			lit->ir_type = NodeType::LIT;
			lit->type_kind = DT;
			lit->parent = region;
			lit->value.set<T, DT>(value);
			return lit;
		}

		/**
		 * @brief Create a jump node to specified target
		 * @param target Target entry node to jump to
		 * @param region Region where the jump should be placed
		 * @return Newly created jump node
		 */
		Node *create_jump(Node *target, Region *region)
		{
			ach::shared_allocator<Node> alloc;
			Node *jump = alloc.allocate(1);
			std::construct_at(jump);

			jump->ir_type = NodeType::JUMP;
			jump->type_kind = DataType::VOID;
			jump->parent = region;
			jump->inputs.push_back(target);
			target->users.push_back(jump);

			return jump;
		}

		/**
		 * @brief Extract numeric value from literal node with type conversion
		 * @tparam T Target C++ type for extraction
		 * @param node Literal node to extract from
		 * @return Converted value
		 */
		template<typename T>
		T extract_v(const Node *node)
		{
			if (!node || node->ir_type != NodeType::LIT)
				throw std::invalid_argument("extract_v requires literal node");

			switch (node->type_kind)
			{
				case DataType::BOOL:
					return static_cast<T>(node->value.get<DataType::BOOL>());
				case DataType::INT8:
					return static_cast<T>(node->value.get<DataType::INT8>());
				case DataType::INT16:
					return static_cast<T>(node->value.get<DataType::INT16>());
				case DataType::INT32:
					return static_cast<T>(node->value.get<DataType::INT32>());
				case DataType::INT64:
					return static_cast<T>(node->value.get<DataType::INT64>());
				case DataType::UINT8:
					return static_cast<T>(node->value.get<DataType::UINT8>());
				case DataType::UINT16:
					return static_cast<T>(node->value.get<DataType::UINT16>());
				case DataType::UINT32:
					return static_cast<T>(node->value.get<DataType::UINT32>());
				case DataType::UINT64:
					return static_cast<T>(node->value.get<DataType::UINT64>());
				case DataType::FLOAT32:
					return static_cast<T>(node->value.get<DataType::FLOAT32>());
				case DataType::FLOAT64:
					return static_cast<T>(node->value.get<DataType::FLOAT64>());
				default:
					throw std::runtime_error("unsupported literal type for extraction");
			}
		}

		/**
		 * @brief Perform type-specific arithmetic folding
		 * @tparam T C++ type for arithmetic
		 * @param op Operation type
		 * @param lval Left operand value
		 * @param rval Right operand value
		 * @param region Region for new node
		 * @return Folded literal or nullptr
		 */
		template<typename T, DataType DT>
		Node *fold_arith_typed(NodeType op, T lval, T rval, Region *region)
		{
			switch (op)
			{
				case NodeType::ADD:
					return create_literal<T, DT>(lval + rval, region);
				case NodeType::SUB:
					return create_literal<T, DT>(lval - rval, region);
				case NodeType::MUL:
					return create_literal<T, DT>(lval * rval, region);
				case NodeType::DIV:
					if (rval == T { 0 })
						return nullptr; /* division by zero; skip folding */
					return create_literal<T, DT>(lval / rval, region);
				case NodeType::MOD:
					if (rval == T { 0 })
						return nullptr; /* modulus by zero; skip folding */
					if constexpr (std::is_floating_point_v<T>)
						return create_literal<T, DT>(std::fmod(lval, rval), region);
					else
						return create_literal<T, DT>(lval % rval, region);
				default:
					return nullptr;
			}
		}

		/**
		 * @brief Perform type-specific comparison folding
		 * @tparam T C++ type for comparison
		 * @param op Comparison operation
		 * @param lval Left operand value
		 * @param rval Right operand value
		 * @param region Region for new node
		 * @return Boolean literal result
		 */
		template<typename T>
		Node *fold_cmp_typed(NodeType op, T lval, T rval, Region *region)
		{
			switch (op)
			{
				case NodeType::EQ:
					return create_literal<bool, DataType::BOOL>(lval == rval, region);
				case NodeType::NEQ:
					return create_literal<bool, DataType::BOOL>(lval != rval, region);
				case NodeType::LT:
					return create_literal<bool, DataType::BOOL>(lval < rval, region);
				case NodeType::LTE:
					return create_literal<bool, DataType::BOOL>(lval <= rval, region);
				case NodeType::GT:
					return create_literal<bool, DataType::BOOL>(lval > rval, region);
				case NodeType::GTE:
					return create_literal<bool, DataType::BOOL>(lval >= rval, region);
				default:
					return nullptr;
			}
		}

		/**
		 * @brief Perform type-specific bitwise folding
		 * @tparam T Integer type for bitwise operations
		 * @param op Bitwise operation type
		 * @param lval Left operand value
		 * @param rval Right operand value
		 * @param type DataType for result
		 * @param region Region for new node
		 * @return Folded literal or nullptr
		 */
		template<typename T, DataType DT>
		Node *fold_bitwise_typed(NodeType op, T lval, T rval, Region *region)
		{
			static_assert(std::is_integral_v<T>, "bitwise operations require integral types");

			switch (op)
			{
				case NodeType::BAND:
					return create_literal<T, DT>(lval & rval, region);
				case NodeType::BOR:
					return create_literal<T, DT>(lval | rval, region);
				case NodeType::BXOR:
					return create_literal<T, DT>(lval ^ rval, region);
				case NodeType::BSHL:
					return create_literal<T, DT>(lval << rval, region);
				case NodeType::BSHR:
					return create_literal<T, DT>(lval >> rval, region);
				default:
					return nullptr;
			}
		}
	}

	std::vector<Region *> ConstantFoldingPass::run(Module &module, PassManager & /* pm */)
	{
		/* clear state from previous runs */
		while (!worklist.empty())
			worklist.pop();
		in_worklist.clear();
		modified_regions.clear();

		process_module(module);

		/* return vector of modified regions for pass manager */
		std::vector<Region *> result;
		result.reserve(modified_regions.size());
		for (Region *region: modified_regions)
			result.push_back(region);

		return result;
	}

	std::size_t ConstantFoldingPass::process_module(Module &module)
	{
		/* collect all potentially foldable nodes from module */
		collect_nodes(module.root());
		for (const Node *func: module.functions())
		{
			if (func->ir_type != NodeType::FUNCTION)
				continue;

			const std::string_view func_name = module.strtable().get(func->str_id);
			for (Region *child: module.root()->children())
			{
				if (child->name() == func_name)
				{
					collect_nodes(child);
					break;
				}
			}
		}

		std::size_t total_folded = 0;
		while (!worklist.empty())
		{
			Node *node = worklist.front();
			worklist.pop();
			in_worklist.erase(node);

			if (process_node(node))
				total_folded++;
		}

		return total_folded;
	}

	void ConstantFoldingPass::collect_nodes(Region *region)
	{
		if (!region)
			return;

		for (Node *node: region->nodes())
		{
			if (is_foldable(node))
				add_to_worklist(node);
		}

		for (Region *child: region->children())
			collect_nodes(child);
	}

	void ConstantFoldingPass::add_to_worklist(Node *node)
	{
		if (!node || in_worklist.contains(node))
			return;

		worklist.push(node);
		in_worklist.insert(node);
	}

	void ConstantFoldingPass::add_users(Node *node)
	{
		if (!node)
			return;

		for (Node *user: node->users)
		{
			if (is_foldable(user))
				add_to_worklist(user);
		}
	}

	bool ConstantFoldingPass::process_node(Node *node)
	{
		if (!node || !is_foldable(node))
			return false;

		Node *folded_node = create_folded(node);
		if (!folded_node)
			return false;

		replace_node(node, folded_node);
		return true;
	}

	void ConstantFoldingPass::replace_node(Node *original, Node *folded)
	{
		if (!original || !folded || !original->parent)
			return;

		modified_regions.insert(original->parent);

		/* add users to worklist before updating connections and then add folded node to region and update
		 * all users to point to the folded node accordingly */
		add_users(original);
		original->parent->insert_before(original, folded);
		for (Node *user: original->users)
		{
			for (std::uint8_t i = 0; i < user->inputs.size(); ++i)
			{
				if (user->inputs[i] == original)
				{
					user->inputs[i] = folded;
					if (std::ranges::find(folded->users, user) == folded->users.end())
						folded->users.push_back(user);
				}
			}
		}

		/* removal */
		for (Node *input: original->inputs)
			erase(input->users, original);
		original->parent->remove(original);
	}

	bool ConstantFoldingPass::is_foldable(const Node *node) const
	{
		if (!node || node->ir_type == NodeType::LIT)
			return false;

		if ((node->traits & NodeTraits::VOLATILE) != NodeTraits::NONE)
			return false;

		switch (node->ir_type)
		{
			case NodeType::ADD:
			case NodeType::SUB:
			case NodeType::MUL:
			case NodeType::DIV:
			case NodeType::MOD:
			case NodeType::EQ:
			case NodeType::NEQ:
			case NodeType::LT:
			case NodeType::LTE:
			case NodeType::GT:
			case NodeType::GTE:
			case NodeType::BAND:
			case NodeType::BOR:
			case NodeType::BXOR:
			case NodeType::BSHL:
			case NodeType::BSHR:
			case NodeType::BNOT:
			case NodeType::BRANCH:
			case NodeType::SELECT:
			case NodeType::FROM:
			case NodeType::CAST:
				return true;
			default:
				return false;
		}
	}

	bool ConstantFoldingPass::all_const(const Node *node) const
	{
		if (!node)
			return false;

		for (const Node *input: node->inputs)
		{
			if (!input || input->ir_type != NodeType::LIT)
				return false;
		}
		return true;
	}

	Node *ConstantFoldingPass::create_folded(const Node *node) const
	{
		if (!node)
			return nullptr;

		switch (node->ir_type)
		{
			case NodeType::ADD:
			case NodeType::SUB:
			case NodeType::MUL:
			case NodeType::DIV:
			case NodeType::MOD:
				return fold_arith(node);

			case NodeType::EQ:
			case NodeType::NEQ:
			case NodeType::LT:
			case NodeType::LTE:
			case NodeType::GT:
			case NodeType::GTE:
				return fold_cmp(node);

			case NodeType::BAND:
			case NodeType::BOR:
			case NodeType::BXOR:
			case NodeType::BSHL:
			case NodeType::BSHR:
				return fold_bitwise(node);

			case NodeType::BNOT:
				return fold_unary(node);

			case NodeType::FROM:
				return fold_from(node);

			case NodeType::BRANCH:
				return fold_branch(node);

			case NodeType::CAST:
				return fold_cast(node);

			case NodeType::SELECT:
			{
				if (node->ir_type != NodeType::SELECT || node->inputs.size() != 3)
					return nullptr;

				const Node *condition = node->inputs[0];
				const Node *true_value = node->inputs[1];
				const Node *false_value = node->inputs[2];

				/* fold if condition is constant */
				if (condition && condition->ir_type == NodeType::LIT && condition->type_kind == DataType::BOOL)
				{
					const bool cond_val = condition->value.get<DataType::BOOL>();
					return const_cast<Node *>(cond_val ? true_value : false_value);
				}

				/* fold if both values are identical literals */
				if (true_value && false_value &&
					true_value->ir_type == NodeType::LIT &&
					false_value->ir_type == NodeType::LIT &&
					literals_equal(true_value, false_value))
				{
					return const_cast<Node *>(true_value);
				}

				return nullptr;
			}

			default:
				return nullptr;
		}
	}

	Node *ConstantFoldingPass::fold_arith(const Node *node) const
	{
		if (!node || node->inputs.size() != 2 || !all_const(node))
			return nullptr;

		const Node *lhs = node->inputs[0];
		const Node *rhs = node->inputs[1];
		const DataType promoted_type = infer_primitive_types(lhs->type_kind, rhs->type_kind);
		if (promoted_type == DataType::VOID)
			return nullptr;

		/* check for division by zero before folding to avoid UB */
		if ((node->ir_type == NodeType::DIV || node->ir_type == NodeType::MOD) &&
		    is_div_zero(node))
		{
			return nullptr;
		}

		Region *region = node->parent;
		switch (promoted_type)
		{
			case DataType::INT32:
			{
				const auto lval = extract_v<std::int32_t>(lhs);
				const auto rval = extract_v<std::int32_t>(rhs);
				return fold_arith_typed<std::int32_t, DataType::INT32>(node->ir_type, lval, rval, region);
			}
			case DataType::INT64:
			{
				const auto lval = extract_v<std::int64_t>(lhs);
				const auto rval = extract_v<std::int64_t>(rhs);
				return fold_arith_typed<std::int64_t, DataType::INT64>(node->ir_type, lval, rval, region);
			}
			case DataType::UINT32:
			{
				const auto lval = extract_v<std::uint32_t>(lhs);
				const auto rval = extract_v<std::uint32_t>(rhs);
				return fold_arith_typed<std::uint32_t, DataType::UINT32>(node->ir_type, lval, rval, region);
			}
			case DataType::UINT64:
			{
				const auto lval = extract_v<std::uint64_t>(lhs);
				const auto rval = extract_v<std::uint64_t>(rhs);
				return fold_arith_typed<std::uint64_t, DataType::UINT64>(node->ir_type, lval, rval, region);
			}
			case DataType::FLOAT32:
			{
				const auto lval = extract_v<float>(lhs);
				const auto rval = extract_v<float>(rhs);
				return fold_arith_typed<float, DataType::FLOAT32>(node->ir_type, lval, rval, region);
			}
			case DataType::FLOAT64:
			{
				const auto lval = extract_v<double>(lhs);
				const auto rval = extract_v<double>(rhs);
				return fold_arith_typed<double, DataType::FLOAT64>(node->ir_type, lval, rval, region);
			}
			default:
				return nullptr;
		}
	}

	Node *ConstantFoldingPass::fold_cmp(const Node *node) const
	{
		if (!node || node->inputs.size() != 2 || !all_const(node))
			return nullptr;

		const Node *lhs = node->inputs[0];
		const Node *rhs = node->inputs[1];
		Region *region = node->parent;

		/* special case for bool comparisons; note: only EQ and NEQ are valid */
		if (lhs->type_kind == DataType::BOOL && rhs->type_kind == DataType::BOOL)
		{
			if (node->ir_type != NodeType::EQ && node->ir_type != NodeType::NEQ)
				return nullptr;

			const bool lval = lhs->value.get<DataType::BOOL>();
			const bool rval = rhs->value.get<DataType::BOOL>();
			return fold_cmp_typed(node->ir_type, lval, rval, region);
		}

		/* determine promoted type for comparison */
		const DataType promoted_type = infer_primitive_types(lhs->type_kind, rhs->type_kind);
		if (promoted_type == DataType::VOID)
			return nullptr;

		/* dispatch to typed comparison based on promoted type */
		switch (promoted_type)
		{
			case DataType::INT32:
			{
				const auto lval = extract_v<std::int32_t>(lhs);
				const auto rval = extract_v<std::int32_t>(rhs);
				return fold_cmp_typed(node->ir_type, lval, rval, region);
			}
			case DataType::INT64:
			{
				const auto lval = extract_v<std::int64_t>(lhs);
				const auto rval = extract_v<std::int64_t>(rhs);
				return fold_cmp_typed(node->ir_type, lval, rval, region);
			}
			case DataType::UINT32:
			{
				const auto lval = extract_v<std::uint32_t>(lhs);
				const auto rval = extract_v<std::uint32_t>(rhs);
				return fold_cmp_typed(node->ir_type, lval, rval, region);
			}
			case DataType::UINT64:
			{
				const auto lval = extract_v<std::uint64_t>(lhs);
				const auto rval = extract_v<std::uint64_t>(rhs);
				return fold_cmp_typed(node->ir_type, lval, rval, region);
			}
			case DataType::FLOAT32:
			{
				const auto lval = extract_v<float>(lhs);
				const auto rval = extract_v<float>(rhs);
				return fold_cmp_typed(node->ir_type, lval, rval, region);
			}
			case DataType::FLOAT64:
			{
				const auto lval = extract_v<double>(lhs);
				const auto rval = extract_v<double>(rhs);
				return fold_cmp_typed(node->ir_type, lval, rval, region);
			}
			default:
				return nullptr;
		}
	}

	Node *ConstantFoldingPass::fold_bitwise(const Node *node) const
	{
		if (!node || node->inputs.size() != 2 || !all_const(node))
			return nullptr;

		const Node *lhs = node->inputs[0];
		const Node *rhs = node->inputs[1];

		/* bitwise operations require exact type match; no promotion */
		if (lhs->type_kind != rhs->type_kind || !is_integer_t(lhs->type_kind))
			return nullptr;

		/* dispatch based on exact integer type */
		Region *region = node->parent;
		switch (lhs->type_kind)
		{
			case DataType::INT8:
			{
				const auto lval = lhs->value.get<DataType::INT8>();
				const auto rval = rhs->value.get<DataType::INT8>();
				return fold_bitwise_typed<std::int8_t, DataType::INT8>(node->ir_type, lval, rval, region);
			}
			case DataType::INT16:
			{
				const auto lval = lhs->value.get<DataType::INT16>();
				const auto rval = rhs->value.get<DataType::INT16>();
				return fold_bitwise_typed<std::int16_t, DataType::INT16>(node->ir_type, lval, rval, region);
			}
			case DataType::INT32:
			{
				const auto lval = lhs->value.get<DataType::INT32>();
				const auto rval = rhs->value.get<DataType::INT32>();
				return fold_bitwise_typed<std::int32_t, DataType::INT32>(node->ir_type, lval, rval, region);
			}
			case DataType::INT64:
			{
				const auto lval = lhs->value.get<DataType::INT64>();
				const auto rval = rhs->value.get<DataType::INT64>();
				return fold_bitwise_typed<std::int64_t, DataType::INT64>(node->ir_type, lval, rval, region);
			}
			case DataType::UINT8:
			{
				const auto lval = lhs->value.get<DataType::UINT8>();
				const auto rval = rhs->value.get<DataType::UINT8>();
				return fold_bitwise_typed<std::uint8_t, DataType::UINT8>(node->ir_type, lval, rval, region);
			}
			case DataType::UINT16:
			{
				const auto lval = lhs->value.get<DataType::UINT16>();
				const auto rval = rhs->value.get<DataType::UINT16>();
				return fold_bitwise_typed<std::uint16_t, DataType::UINT16>(node->ir_type, lval, rval, region);
			}
			case DataType::UINT32:
			{
				const auto lval = lhs->value.get<DataType::UINT32>();
				const auto rval = rhs->value.get<DataType::UINT32>();
				return fold_bitwise_typed<std::uint32_t, DataType::UINT32>(node->ir_type, lval, rval, region);
			}
			case DataType::UINT64:
			{
				const auto lval = lhs->value.get<DataType::UINT64>();
				const auto rval = rhs->value.get<DataType::UINT64>();
				return fold_bitwise_typed<std::uint64_t, DataType::UINT64>(node->ir_type, lval, rval, region);
			}
			default:
				return nullptr;
		}
	}

	Node *ConstantFoldingPass::fold_unary(const Node *node) const
	{
		if (!node || node->ir_type != NodeType::BNOT || node->inputs.size() != 1)
			return nullptr;

		const Node *input = node->inputs[0];
		if (!input || input->ir_type != NodeType::LIT || !is_integer_t(input->type_kind))
			return nullptr;

		/* dispatch based on input type */
		Region *region = node->parent;
		switch (input->type_kind)
		{
			case DataType::INT8:
				return create_literal<std::int8_t, DataType::INT8>(~input->value.get<DataType::INT8>(), region);
			case DataType::INT16:
				return create_literal<std::int16_t, DataType::INT16>(~input->value.get<DataType::INT16>(), region);
			case DataType::INT32:
				return create_literal<std::int32_t, DataType::INT32>(~input->value.get<DataType::INT32>(), region);
			case DataType::INT64:
				return create_literal<std::int64_t, DataType::INT64>(~input->value.get<DataType::INT64>(), region);
			case DataType::UINT8:
				return create_literal<std::uint8_t, DataType::UINT8>(~input->value.get<DataType::UINT8>(), region);
			case DataType::UINT16:
				return create_literal<std::uint16_t, DataType::UINT16>(~input->value.get<DataType::UINT16>(), region);
			case DataType::UINT32:
				return create_literal<std::uint32_t, DataType::UINT32>(~input->value.get<DataType::UINT32>(), region);
			case DataType::UINT64:
				return create_literal<std::uint64_t, DataType::UINT64>(~input->value.get<DataType::UINT64>(), region);
			default:
				return nullptr;
		}
	}

	Node *ConstantFoldingPass::fold_from(const Node *node) const
	{
		if (!node || node->ir_type != NodeType::FROM || node->inputs.empty())
			return nullptr;

		/* check if all inputs are literals */
		if (!all_const(node))
			return nullptr;

		/* check if all inputs are identical literals */
		const Node *first = node->inputs[0];
		for (const Node *input: node->inputs)
		{
			if (!literals_equal(first, input))
				return nullptr;
		}

		/* return the first one if all inputs are the same literal */
		return const_cast<Node *>(first);
	}

	Node *ConstantFoldingPass::fold_branch(const Node *node) const
	{
		if (!node || node->ir_type != NodeType::BRANCH || node->inputs.size() != 3)
			return nullptr;

		const Node *condition = node->inputs[0];
		if (!condition || condition->ir_type != NodeType::LIT || condition->type_kind != DataType::BOOL)
			return nullptr;

		const bool cond_value = condition->value.get<DataType::BOOL>();
		Node *target = cond_value ? node->inputs[1] : node->inputs[2];
		return create_jump(target, node->parent); /* create unconditional jump to selected target */
	}

	Node *ConstantFoldingPass::fold_cast(const Node *node) const
	{
		if (!node || node->ir_type != NodeType::CAST || node->inputs.size() != 1)
			return nullptr;

		const Node *input = node->inputs[0];
		if (!input || input->ir_type != NodeType::LIT)
			return nullptr;

		/* extract source value and convert to target type then dispatch based on it */
		const DataType target_type = node->type_kind;
		Region *region = node->parent;
		switch (target_type)
		{
			case DataType::BOOL:
				return create_literal<bool, DataType::BOOL>(extract_v<bool>(input), region);
			case DataType::INT8:
				return create_literal<std::int8_t, DataType::INT8>(extract_v<std::int8_t>(input), region);
			case DataType::INT16:
				return create_literal<std::int16_t, DataType::INT16>(extract_v<std::int16_t>(input), region);
			case DataType::INT32:
				return create_literal<std::int32_t, DataType::INT32>(extract_v<std::int32_t>(input), region);
			case DataType::INT64:
				return create_literal<std::int64_t, DataType::INT64>(extract_v<std::int64_t>(input), region);
			case DataType::UINT8:
				return create_literal<std::uint8_t, DataType::UINT8>(extract_v<std::uint8_t>(input), region);
			case DataType::UINT16:
				return create_literal<std::uint16_t, DataType::UINT16>(extract_v<std::uint16_t>(input), region);
			case DataType::UINT32:
				return create_literal<std::uint32_t, DataType::UINT32>(extract_v<std::uint32_t>(input), region);
			case DataType::UINT64:
				return create_literal<std::uint64_t, DataType::UINT64>(extract_v<std::uint64_t>(input), region);
			case DataType::FLOAT32:
				return create_literal<float, DataType::FLOAT32>(extract_v<float>(input), region);
			case DataType::FLOAT64:
				return create_literal<double, DataType::FLOAT64>(extract_v<double>(input), region);
			default:
				return nullptr; /* unsupported cast target */
		}
	}

	bool ConstantFoldingPass::literals_equal(const Node *a, const Node *b) const
	{
		if (!a || !b || a->ir_type != NodeType::LIT || b->ir_type != NodeType::LIT)
			return false;

		if (a->type_kind != b->type_kind)
			return false;

		/* dispatch based on type for value comparison */
		switch (a->type_kind)
		{
			case DataType::BOOL:
				return a->value.get<DataType::BOOL>() == b->value.get<DataType::BOOL>();
			case DataType::INT8:
				return a->value.get<DataType::INT8>() == b->value.get<DataType::INT8>();
			case DataType::INT16:
				return a->value.get<DataType::INT16>() == b->value.get<DataType::INT16>();
			case DataType::INT32:
				return a->value.get<DataType::INT32>() == b->value.get<DataType::INT32>();
			case DataType::INT64:
				return a->value.get<DataType::INT64>() == b->value.get<DataType::INT64>();
			case DataType::UINT8:
				return a->value.get<DataType::UINT8>() == b->value.get<DataType::UINT8>();
			case DataType::UINT16:
				return a->value.get<DataType::UINT16>() == b->value.get<DataType::UINT16>();
			case DataType::UINT32:
				return a->value.get<DataType::UINT32>() == b->value.get<DataType::UINT32>();
			case DataType::UINT64:
				return a->value.get<DataType::UINT64>() == b->value.get<DataType::UINT64>();
			case DataType::FLOAT32:
				return a->value.get<DataType::FLOAT32>() == b->value.get<DataType::FLOAT32>();
			case DataType::FLOAT64:
				return a->value.get<DataType::FLOAT64>() == b->value.get<DataType::FLOAT64>();
			default:
				return false;
		}
	}

	bool ConstantFoldingPass::is_div_zero(const Node *node) const
	{
		if (!node || (node->ir_type != NodeType::DIV && node->ir_type != NodeType::MOD))
			return false;

		if (node->inputs.size() != 2)
			return false;

		const Node *divisor = node->inputs[1];
		if (!divisor || divisor->ir_type != NodeType::LIT)
			return false;

		/* if the divisor is zero based on type */
		switch (divisor->type_kind)
		{
			case DataType::INT8:
				return divisor->value.get<DataType::INT8>() == 0;
			case DataType::INT16:
				return divisor->value.get<DataType::INT16>() == 0;
			case DataType::INT32:
				return divisor->value.get<DataType::INT32>() == 0;
			case DataType::INT64:
				return divisor->value.get<DataType::INT64>() == 0;
			case DataType::UINT8:
				return divisor->value.get<DataType::UINT8>() == 0;
			case DataType::UINT16:
				return divisor->value.get<DataType::UINT16>() == 0;
			case DataType::UINT32:
				return divisor->value.get<DataType::UINT32>() == 0;
			case DataType::UINT64:
				return divisor->value.get<DataType::UINT64>() == 0;
			case DataType::FLOAT32:
				return divisor->value.get<DataType::FLOAT32>() == 0.0f;
			case DataType::FLOAT64:
				return divisor->value.get<DataType::FLOAT64>() == 0.0;
			default:
				return false;
		}
	}
}
