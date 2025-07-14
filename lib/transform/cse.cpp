/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <algorithm>
#include <cstring>
#include <queue>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/foundation/region.hpp>
#include <arc/transform/cse.hpp>

namespace arc
{
	std::string CommonSubexpressionEliminationPass::name() const
	{
		return "common-subexpression-elimination";
	}

	std::vector<std::string> CommonSubexpressionEliminationPass::require() const
	{
		return { "type-based-alias-analysis" };
	}

	std::vector<std::string> CommonSubexpressionEliminationPass::invalidates() const
	{
		return {};
	}

	std::vector<Region *> CommonSubexpressionEliminationPass::run(Module &module, PassManager &pm)
	{
		const auto &tbaa_result = pm.get<TypeBasedAliasResult>();

		value_numbers.clear();
		expression_to_node.clear();
		next_value_number = 1;

		std::vector<Region *> modified_regions;
		if (const std::size_t eliminated = process_module(module, tbaa_result);
			eliminated > 0)
		{
			/* collect all regions that might have been modified */
			std::queue<Region *> region_worklist;
			region_worklist.push(module.root());

			while (!region_worklist.empty())
			{
				Region *current = region_worklist.front();
				region_worklist.pop();

				/* check if any nodes in this region were eliminated */
				auto region_modified = false;
				for (const auto &[node, _]: value_numbers)
				{
					if (node->parent == current)
					{
						region_modified = true;
						break;
					}
				}

				if (region_modified)
					modified_regions.push_back(current);

				/* add child regions to worklist */
				for (Region *child: current->children())
					region_worklist.push(child);
			}
		}

		return modified_regions;
	}

	std::size_t CommonSubexpressionEliminationPass::process_module(Module &module,
	                                                               const TypeBasedAliasResult &tbaa_result)
	{
		std::size_t total_eliminated = 0;

		/* process global region first and then functions */
		total_eliminated += process_region(module.root(), tbaa_result);
		for (const Node *func_node: module.functions())
		{
			if (func_node->ir_type != NodeType::FUNCTION)
				continue;

			/* find the function's region */
			const std::string_view func_name = module.strtable().get(func_node->str_id);
			for (Region *child: module.root()->children())
			{
				if (child->name() == func_name)
				{
					total_eliminated += process_region(child, tbaa_result);
					break;
				}
			}
		}

		return total_eliminated;
	}

	std::size_t CommonSubexpressionEliminationPass::process_region(Region *region,
	                                                               const TypeBasedAliasResult &tbaa_result)
	{
		if (!region)
			return 0;

		std::size_t eliminated = 0;
		std::queue<Region *> worklist;
		worklist.push(region);

		while (!worklist.empty())
		{
			const Region *current_region = worklist.front();
			worklist.pop();
			for (Node *node: current_region->nodes())
			{
				if (!is_eligible_for_cse(node))
					continue;

				ValueNumber vn = compute_value_number(node, tbaa_result);
				if (vn == 0)
					continue;

				/* check if we've seen this expression before */
				if (auto it = expression_to_node.find(vn);
					it != expression_to_node.end())
				{
					Node *existing = it->second;
					/*  check aliasing for load operations */
					if (is_load_operation(node) && is_load_operation(existing))
					{
						if (loads_may_alias(existing, node, tbaa_result))
							continue;
					}
					/* replace all uses with existing equivalent expression */
					if (replace_all_uses(node, existing))
					{
						eliminated++;
						continue;
					}
				}
				/* record this expression for future matches */
				expression_to_node[vn] = node;
				value_numbers[node] = vn;
			}
			/* add child regions to worklist */
			for (Region *child: current_region->children())
				worklist.push(child);
		}

		return eliminated;
	}

	ValueNumber CommonSubexpressionEliminationPass::compute_value_number(
		Node *node, const TypeBasedAliasResult &tbaa_result)
	{
		if (!node)
			return 0;

		/* return cached value number if available */
		if (auto it = value_numbers.find(node);
			it != value_numbers.end())
			return it->second;

		ValueNumber vn = 0;
		switch (node->ir_type)
		{
			case NodeType::LIT:
				vn = compute_literal_value_number(node);
				break;
			case NodeType::LOAD:
			case NodeType::PTR_LOAD:
			case NodeType::ATOMIC_LOAD:
				vn = compute_load_value_number(node, tbaa_result);
				break;
			default:
				if (!node->inputs.empty())
					vn = compute_expression_value_number(node);
				else
					vn = next_value_number++;
				break;
		}

		if (vn != 0)
			value_numbers[node] = vn;

		return vn;
	}

	ValueNumber CommonSubexpressionEliminationPass::compute_literal_value_number(Node *node) // NOLINT(*-no-recursion)
	{
		if (!node || node->ir_type != NodeType::LIT)
			throw std::invalid_argument("compute_literal_value_number requires LIT node");

		auto hash = static_cast<ValueNumber>(node->type_kind);
		switch (node->type_kind)
		{
			case DataType::BOOL:
				hash = hash_combine(hash, node->value.get<DataType::BOOL>());
				break;
			case DataType::INT8:
				hash = hash_combine(hash, static_cast<std::int64_t>(node->value.get<DataType::INT8>()));
				break;
			case DataType::INT16:
				hash = hash_combine(hash, static_cast<std::int64_t>(node->value.get<DataType::INT16>()));
				break;
			case DataType::INT32:
				hash = hash_combine(hash, static_cast<std::int64_t>(node->value.get<DataType::INT32>()));
				break;
			case DataType::INT64:
				hash = hash_combine(hash, node->value.get<DataType::INT64>());
				break;
			case DataType::UINT8:
				hash = hash_combine(hash, static_cast<std::uint64_t>(node->value.get<DataType::UINT8>()));
				break;
			case DataType::UINT16:
				hash = hash_combine(hash, static_cast<std::uint64_t>(node->value.get<DataType::UINT16>()));
				break;
			case DataType::UINT32:
				hash = hash_combine(hash, static_cast<std::uint64_t>(node->value.get<DataType::UINT32>()));
				break;
			case DataType::UINT64:
				hash = hash_combine(hash, node->value.get<DataType::UINT64>());
				break;
			case DataType::FLOAT32:
			{
				const float value = node->value.get<DataType::FLOAT32>();
				std::uint32_t bits;
				std::memcpy(&bits, &value, sizeof(bits));
				hash = hash_combine(hash, static_cast<std::uint64_t>(bits));
				break;
			}
			case DataType::FLOAT64:
			{
				const double value = node->value.get<DataType::FLOAT64>();
				std::uint64_t bits;
				std::memcpy(&bits, &value, sizeof(bits));
				hash = hash_combine(hash, bits);
				break;
			}
			case DataType::VECTOR:
				hash = compute_vector_literal_value_number(node);
				break;
			default:
				throw std::runtime_error(
					"unsupported literal type for CSE: " + std::to_string(static_cast<int>(node->type_kind)));
		}

		return hash == 0 ? 1 : hash;
	}

	ValueNumber CommonSubexpressionEliminationPass::compute_vector_literal_value_number(Node *node) // NOLINT(*-no-recursion)
	{
		if (node->type_kind != DataType::VECTOR)
			throw std::invalid_argument("compute_vector_literal_value_number requires VECTOR node");

		const auto &vec_data = node->value.get<DataType::VECTOR>();
		auto hash = static_cast<ValueNumber>(DataType::VECTOR);
		hash = hash_combine(hash, static_cast<std::uint64_t>(vec_data.elem_type));
		hash = hash_combine(hash, static_cast<std::uint64_t>(vec_data.lane_count));

		/* for vector literals created by VECTOR_BUILD, hash the individual elements */
		if (node->ir_type == NodeType::VECTOR_BUILD)
		{
			for (Node *element: node->inputs)
			{
				if (element && element->ir_type == NodeType::LIT)
				{
					ValueNumber elem_vn = compute_literal_value_number(element);
					hash = hash_combine(hash, elem_vn);
				}
				else
				{
					return 0; /* can't compute deterministic hash without literal elements */
				}
			}
		}

		return hash == 0 ? 1 : hash;
	}

	ValueNumber CommonSubexpressionEliminationPass::compute_expression_value_number(Node *node)
	{
		if (node->inputs.empty())
			return 0;

		auto hash = static_cast<ValueNumber>(node->ir_type);
		hash = hash_combine(hash, static_cast<ValueNumber>(node->type_kind));

		/* collect input value numbers */
		std::vector<ValueNumber> input_vns;
		input_vns.reserve(node->inputs.size());
		for (Node *input: node->inputs)
		{
			if (!input)
				return 0;

			ValueNumber input_vn = 0;
			if (auto it = value_numbers.find(input);
				it != value_numbers.end())
			{
				input_vn = it->second;
			}
			else
			{
				/* can't compute expression value number without input value numbers */
				return 0;
			}
			input_vns.push_back(input_vn);
		}

		/* sort operands to normalize commutative operations */
		if (is_commutative(node->ir_type) && input_vns.size() == 2)
		{
			if (input_vns[0] > input_vns[1])
				std::swap(input_vns[0], input_vns[1]);
		}

		/* combine all input value numbers into final hash */
		for (ValueNumber vn: input_vns)
			hash = hash_combine(hash, vn);

		return hash == 0 ? 1 : hash;
	}

	ValueNumber CommonSubexpressionEliminationPass::compute_load_value_number(
		Node *node, const TypeBasedAliasResult &tbaa_result)
	{
		if (!is_load_operation(node))
			throw std::invalid_argument("compute_load_value_number requires load operation");

		if (node->inputs.empty())
			return next_value_number++;

		Node *address = node->inputs[0];
		if (!address)
			return next_value_number++;

		/* get address value number */
		ValueNumber addr_vn = 0;
		if (const auto it = value_numbers.find(address);
			it != value_numbers.end())
			addr_vn = it->second;
		else
			return 0; /* can't compute without address value number */

		auto hash = static_cast<ValueNumber>(node->ir_type);
		hash = hash_combine(hash, static_cast<ValueNumber>(node->type_kind));
		hash = hash_combine(hash, addr_vn);
		if (const MemoryLocation *loc = tbaa_result.memory_location(node))
		{
			hash = hash_combine(hash, reinterpret_cast<std::uintptr_t>(loc->allocation_site));
			if (loc->offset != -1)
				hash = hash_combine(hash, static_cast<std::uint64_t>(loc->offset));
			hash = hash_combine(hash, loc->size);
			hash = hash_combine(hash, static_cast<std::uint64_t>(loc->access_type));
		}

		/* handle atomic ordering for atomic loads */
		if (node->ir_type == NodeType::ATOMIC_LOAD && node->inputs.size() > 1)
		{
			if (Node *ordering = node->inputs[1];
				ordering && ordering->ir_type == NodeType::LIT)
			{
				hash = hash_combine(hash, compute_literal_value_number(ordering));
			}
			else
				return 0; /* can't compute without ordering information */
		}

		return hash == 0 ? 1 : hash;
	}

	bool CommonSubexpressionEliminationPass::is_load_operation(const Node *node)
	{
		if (!node)
			return false;

		return node->ir_type == NodeType::LOAD ||
		       node->ir_type == NodeType::PTR_LOAD ||
		       node->ir_type == NodeType::ATOMIC_LOAD;
	}

	bool CommonSubexpressionEliminationPass::is_eligible_for_cse(const Node *node)
	{
		if (!node)
			return false;

		/* exclude nodes with side effects or special semantics */
		switch (node->ir_type)
		{
			case NodeType::ENTRY:
			case NodeType::EXIT:
			case NodeType::FUNCTION:
			case NodeType::RET:
			case NodeType::CALL:
			case NodeType::INVOKE:
			case NodeType::STORE:
			case NodeType::PTR_STORE:
			case NodeType::ATOMIC_STORE:
			case NodeType::ATOMIC_CAS:
			case NodeType::ALLOC:
			case NodeType::BRANCH:
			case NodeType::JUMP:
				return false;

			/* these operations are safe to eliminate if redundant */
			case NodeType::ADD:
			case NodeType::SUB:
			case NodeType::MUL:
			case NodeType::DIV:
			case NodeType::MOD:
			case NodeType::GT:
			case NodeType::GTE:
			case NodeType::LT:
			case NodeType::LTE:
			case NodeType::EQ:
			case NodeType::NEQ:
			case NodeType::BAND:
			case NodeType::BOR:
			case NodeType::BXOR:
			case NodeType::BNOT:
			case NodeType::BSHL:
			case NodeType::BSHR:
			case NodeType::LOAD:
			case NodeType::PTR_LOAD:
			case NodeType::ATOMIC_LOAD:
			case NodeType::ADDR_OF:
			case NodeType::PTR_ADD:
			case NodeType::CAST:
			case NodeType::LIT:
			case NodeType::PARAM:
			case NodeType::VECTOR_BUILD:
			case NodeType::VECTOR_EXTRACT:
			case NodeType::VECTOR_SPLAT:
			case NodeType::ACCESS:
				break;

			default:
				/* be conservative with unknown operations */
				return false;
		}

		if ((node->traits & NodeTraits::VOLATILE) != NodeTraits::NONE)
			return false;

		return true;
	}

	bool CommonSubexpressionEliminationPass::is_commutative(const NodeType type)
	{
		switch (type)
		{
			case NodeType::ADD:
			case NodeType::MUL:
			case NodeType::BAND:
			case NodeType::BOR:
			case NodeType::BXOR:
			case NodeType::EQ:
			case NodeType::NEQ:
				return true;
			default:
				return false;
		}
	}

	bool CommonSubexpressionEliminationPass::loads_may_alias(Node *a, Node *b, const TypeBasedAliasResult &tbaa_result)
	{
		if (!a || !b || !is_load_operation(a) || !is_load_operation(b))
			return true;

		/* use TBAA to determine aliasing relationship */
		const TBAAResult alias_result = tbaa_result.alias(a, b);
		return alias_result != TBAAResult::NO_ALIAS;
	}

	bool CommonSubexpressionEliminationPass::replace_all_uses(Node *node_to_replace, Node *replacement_node)
	{
		if (!node_to_replace || !replacement_node || node_to_replace == replacement_node)
			return false;

		/* don't replace structural nodes */
		if (node_to_replace->ir_type == NodeType::ENTRY)
			return false;

		for (const auto users_copy = node_to_replace->users;
		     Node *user: users_copy)
		{
			if (!user)
				continue;
			for (std::size_t i = 0; i < user->inputs.size(); ++i)
			{
				if (user->inputs[i] == node_to_replace)
				{
					user->inputs[i] = replacement_node;
					if (std::ranges::find(replacement_node->users, user) == replacement_node->users.end())
						replacement_node->users.push_back(user);
				}
			}
		}
		node_to_replace->users.clear();
		return true;
	}
}
