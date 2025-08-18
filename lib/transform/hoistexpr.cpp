/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <algorithm>
#include <queue>
#include <unordered_set>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/foundation/region.hpp>
#include <arc/transform/hoistexpr.hpp>

namespace arc
{
	static bool is_loop_region(Region *region)
	{
		if (!region)
			return false;

		/* check if this region has back-edges targeting its entry */
		Node *entry = region->entry();
		if (!entry)
			return false;

		/* look for control flow instructions that target this region's entry
		 * from regions that this region dominates which are back-edges */
		for (Node *user : entry->users)
		{
			if (user->ir_type == NodeType::JUMP)
			{
				/* unconditional jump back to loop header */
				if (user->parent && region->dominates(user->parent))
					return true;
			}
			else if (user->ir_type == NodeType::BRANCH)
			{
				/* conditional branch that might loop back */
				if (user->parent && region->dominates(user->parent))
				{
					/* check if either branch target is this region's entry */
					if (user->inputs.size() >= 3 &&
						(user->inputs[1] == entry || user->inputs[2] == entry))
					{
						return true;
					}
				}
			}
			else if (user->ir_type == NodeType::INVOKE)
			{
				/* function call with exception handling that might loop back */
				if (user->parent && region->dominates(user->parent))
				{
					if (user->inputs.size() >= 2 &&
						(user->inputs[user->inputs.size() - 2] == entry ||
						 user->inputs[user->inputs.size() - 1] == entry))
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	static bool is_invariant(Node *expr, Region *loop_region)
	{
		if (!expr || !loop_region)
			return false;

		/* analyze all operands for loop dependence using region dominance.
		 * an expression is invariant if all its operands are defined outside
		 * the loop or are compile-time constants */
		for (Node *input: expr->inputs)
		{
			if (!input)
				continue;

			/* literals and parameters are always invariant */
			if (input->ir_type == NodeType::LIT || input->ir_type == NodeType::PARAM)
				continue;

			/* if operand is defined in a region dominated by the loop,
			 * then it depends on loop execution */
			if (input->parent && loop_region->dominates(input->parent))
				return false;
		}

		return true;
	}

	static bool is_safe_to_speculate(Node *expr)
	{
		/* we only reject operations that definitely
		 * have side effects or modify program state; optimizers
		 * don't have to care if the user program contains undefined behavior. */
		switch (expr->ir_type)
		{
			case NodeType::CALL:
			case NodeType::INVOKE:
			case NodeType::STORE:
			case NodeType::PTR_STORE:
			case NodeType::ATOMIC_STORE:
			case NodeType::ATOMIC_CAS:
				/* operations with side effects cannot be speculated */
				return false;

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
			case NodeType::CAST:
			case NodeType::VECTOR_BUILD:
			case NodeType::VECTOR_EXTRACT:
			case NodeType::VECTOR_SPLAT:
			case NodeType::ADDR_OF:
			case NodeType::PTR_ADD:
			case NodeType::LOAD:
			case NodeType::PTR_LOAD:
			case NodeType::ATOMIC_LOAD:
				/* aggressively hoist all computational and memory operations */
				break;

			default:
				/* conservatively reject unknown operations */
				return false;
		}

		/* volatile operations must not be optimized */
		return (expr->traits & NodeTraits::VOLATILE) == NodeTraits::NONE;
	}

	static bool is_load_operation(Node *node)
	{
		return node->ir_type == NodeType::LOAD ||
		       node->ir_type == NodeType::PTR_LOAD ||
		       node->ir_type == NodeType::ATOMIC_LOAD;
	}

	static bool is_store_operation(Node *node)
	{
		if (!node)
			return false;

		return node->ir_type == NodeType::STORE ||
		       node->ir_type == NodeType::PTR_STORE ||
		       node->ir_type == NodeType::ATOMIC_STORE ||
		       node->ir_type == NodeType::ATOMIC_CAS;
	}

	static std::vector<Region *> find_dominating_path(Region *from, Region *to)
	{
		std::vector<Region *> path;

		/* walk up the dominator tree from 'from' until we reach 'to'.
		 * this gives us all regions along the domination path where
		 * interfering stores might be located */
		Region *current = from;
		while (current && current != to)
		{
			path.push_back(current);
			current = current->parent();
		}

		/* include the target region if we found a valid path */
		if (current == to)
			path.push_back(to);

		return path;
	}

	static std::vector<Node *> find_stores_in_region(Region *region)
	{
		std::vector<Node *> stores;

		if (!region)
			return stores;

		/* collect all store operations in the region that might
		 * interfere with load operations being hoisted */
		for (Node *node: region->nodes())
		{
			if (is_store_operation(node))
				stores.push_back(node);
		}

		return stores;
	}

	static bool safe_to_hoist_load(Node *load, Region *from, Region *to, const TypeBasedAliasResult &tbaa_result)
	{
		if (!is_load_operation(load))
			return false;

		/* find all regions along the domination path from source to target.
		 * we need to check for stores that might alias with this load
		 * along the entire path to ensure hoisting preserves semantics */
		std::vector<Region *> path_regions = find_dominating_path(from, to);

		/* check for potentially interfering stores along the path */
		for (Region *region: path_regions)
		{
			std::vector<Node *> stores = find_stores_in_region(region);
			for (Node *store: stores)
			{
				if (tbaa_result.may_alias(load, store))
				{
					/* found a store that might alias with this load,
					 * making hoisting potentially unsafe */
					return false;
				}
			}
		}

		return true;
	}

	static bool safe_to_hoist(Node *expr, Region *from, Region *to, const TypeBasedAliasResult &tbaa_result)
	{
		if (!expr || !from || !to)
			return false;

		/* basic speculation safety check for all expressions */
		if (!is_safe_to_speculate(expr))
			return false;

		/* memory operations require additional alias analysis to ensure
		 * hoisting doesn't violate memory ordering constraints */
		if (is_load_operation(expr))
			return safe_to_hoist_load(expr, from, to, tbaa_result);

		/* pure computational operations are generally safe to hoist */
		return true;
	}

	static Region *find_hoist_target(Region *loop_region)
	{
		if (!loop_region)
			return nullptr;

		/* the parent region serves as the natural preheader for the loop.
		 * in Arc's region hierarchy, this provides the domination relationship
		 * needed for safe hoisting without requiring explicit preheader creation */
		Region *parent = loop_region->parent();
		if (!parent)
			return nullptr;

		/* verify parent actually dominates the loop region */
		if (!parent->dominates(loop_region))
			return nullptr;

		return parent;
	}

	static std::uint32_t compute_benefit(const HoistCandidate &candidate)
	{
		if (!candidate.expr || !candidate.from || !candidate.to)
			return 0;

		std::uint32_t base_benefit = 1;

		/* assign higher benefit scores to more expensive operations.
		 * multiply and divide operations are typically much more expensive
		 * than simple arithmetic, making them better hoisting candidates */
		switch (candidate.expr->ir_type)
		{
			case NodeType::MUL:
				base_benefit = 3;
				break;
			case NodeType::DIV:
			case NodeType::MOD:
				base_benefit = 10;
				break;
			case NodeType::CALL:
			case NodeType::INVOKE:
				base_benefit = 20;
				break;
			default:
				base_benefit = 1;
				break;
		}

		/* estimate loop nesting depth by counting parent regions that are loops.
		 * deeper nesting means more iterations, making hoisting more valuable */
		std::uint32_t nesting_depth = 0;
		Region *current = candidate.from;
		while (current && current != candidate.to)
		{
			if (is_loop_region(current))
				nesting_depth++;
			current = current->parent();
		}

		/* exponential benefit scaling for nested loops since innermost loops
		 * execute most frequently */
		return base_benefit * (1U << nesting_depth);
	}

	static bool is_hoistable_expression(Node *expr)
	{
		if (!expr)
			return false;

		/* determine which expression types are eligible for hoisting.
		 * exclude structural nodes, control flow, and operations with
		 * side effects or special semantics */
		switch (expr->ir_type)
		{
			case NodeType::ENTRY:
			case NodeType::EXIT:
			case NodeType::FUNCTION:
			case NodeType::PARAM:
			case NodeType::RET:
			case NodeType::BRANCH:
			case NodeType::JUMP:
			case NodeType::INVOKE:
			case NodeType::FROM:
				return false;

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
			case NodeType::CAST:
			case NodeType::VECTOR_BUILD:
			case NodeType::VECTOR_EXTRACT:
			case NodeType::VECTOR_SPLAT:
			case NodeType::ADDR_OF:
			case NodeType::PTR_ADD:
			case NodeType::ACCESS:
				return true;

			case NodeType::LOAD:
			case NodeType::PTR_LOAD:
			case NodeType::ATOMIC_LOAD:
				/* loads are hoistable but require careful alias analysis */
				return true;

			case NodeType::LIT:
				/* literals are typically not worth hoisting since they're
				 * already constants and don't benefit from motion */
				return false;

			default:
				/* be conservative with unknown node types */
				return false;
		}
	}

	static bool hoist_expression(const HoistCandidate &candidate)
	{
		if (!candidate.expr || !candidate.from || !candidate.to)
			return false;

		/* remove expression from its current location */
		candidate.from->remove(candidate.expr);

		/* insert expression at the end of target region, before any terminator.
		 * this preserves control flow structure while placing the expression
		 * where it will be executed before entering the loop */
		if (candidate.to->is_terminated())
		{
			/* find the terminator and insert before it */
			const auto &nodes = candidate.to->nodes();
			if (!nodes.empty() && nodes.back())
			{
				candidate.to->insert_before(nodes.back(), candidate.expr);
			}
			else
			{
				candidate.to->append(candidate.expr);
			}
		}
		else
		{
			candidate.to->append(candidate.expr);
		}
		return true;
	}

	std::string HoistExpr::name() const
	{
		return "hoist-expr";
	}

	std::vector<std::string> HoistExpr::require() const
	{
		return { "type-based-alias-analysis" };
	}

	std::vector<std::string> HoistExpr::invalidates() const
	{
		return {}; /* hoisting preserves most analysis results */
	}

	std::vector<Region *> HoistExpr::run(Module &module, PassManager &pm)
	{
		const auto &tbaa_result = pm.get<TypeBasedAliasResult>();
		std::vector<HoistCandidate> candidates = find_candidates(module, tbaa_result);

		if (candidates.empty())
			return {};

		/* sort candidates by benefit score to prioritize high-value hoisting.
		 * this ensures we focus on the most profitable opportunities first */
		std::ranges::sort(candidates, [](const HoistCandidate &a, const HoistCandidate &b)
		{
			return a.benefit > b.benefit;
		});

		return hoist_candidates(candidates);
	}

	std::vector<HoistCandidate> HoistExpr::find_candidates(Module &module, const TypeBasedAliasResult &tbaa_result)
	{
		return process_module(module, tbaa_result);
	}

	std::vector<HoistCandidate> HoistExpr::process_module(Module &module, const TypeBasedAliasResult &tbaa_result)
	{
		std::vector<HoistCandidate> candidates;

		/* process global region first, then all function regions.
		 * this ensures we catch hoisting opportunities in both global
		 * initialization code and function bodies */
		auto global_candidates = process_region(module.root(), tbaa_result);
		candidates.insert(candidates.end(), global_candidates.begin(), global_candidates.end());

		for (Node *func: module.functions())
		{
			if (func->ir_type != NodeType::FUNCTION)
				continue;

			/* find the function's region using name matching. each function
			 * has a corresponding region in the module's region hierarchy */
			const std::string_view func_name = module.strtable().get(func->str_id);
			for (Region *child: module.root()->children())
			{
				if (child->name() == func_name)
				{
					auto func_candidates = process_region(child, tbaa_result);
					candidates.insert(candidates.end(), func_candidates.begin(), func_candidates.end());
					break;
				}
			}
		}

		return candidates;
	}

	std::vector<HoistCandidate> HoistExpr::process_region(Region *region, const TypeBasedAliasResult &tbaa_result)
	{
	   if (!region)
	       return {};

	   std::vector<HoistCandidate> candidates;
	   std::queue<Region *> worklist;
	   worklist.push(region);

	   /* use worklist algorithm to process all regions in the hierarchy.
	    * this allows us to find nested loops and handle complex control
	    * flow structures systematically */
	   while (!worklist.empty())
	   {
	       Region *current_region = worklist.front();
	       worklist.pop();

	       /* check if this region represents a loop structure */
	       if (is_loop_region(current_region))
	       {
	           Region *hoist_target = find_hoist_target(current_region);
	           if (!hoist_target)
	           {
	               /* no suitable target found; add child regions for nested analysis */
	               for (Region *child: current_region->children())
	                   worklist.push(child);
	               continue;
	           }

	           /* analyze expressions in loop region for hoisting opportunities */
	           for (Node *node: current_region->nodes())
	           {
	               if (!is_hoistable_expression(node))
	                   continue;

	               if (is_invariant(node, current_region) &&
	                   safe_to_hoist(node, current_region, hoist_target, tbaa_result))
	               {
	                   auto candidate = HoistCandidate();
	                   candidate.expr = node;
	                   candidate.from = current_region;
	                   candidate.to = hoist_target;
	                   candidate.benefit = compute_benefit(candidate);
	                   candidates.push_back(candidate);
	               }
	           }

	           /* second pass: find expressions that become invariant after initial hoisting.
	            * simulate hoisting the first batch without moving nodes, then check what
	            * becomes newly invariant in that hypothetical state */
	           std::unordered_set<Node*> would_be_hoisted;
	           for (const auto &candidate : candidates)
	           {
	               if (candidate.from == current_region)
	                   would_be_hoisted.insert(candidate.expr);
	           }

	           auto is_invariant_assuming_hoisted = [&](Node *expr) -> bool {
	               if (!expr || !current_region)
	                   return false;

	               for (Node *input : expr->inputs)
	               {
	                   if (!input)
	                       continue;

	                   if (input->ir_type == NodeType::LIT || input->ir_type == NodeType::PARAM)
	                       continue;

	                   if (input->parent && !current_region->dominates(input->parent))
	                       continue;

	                   /* if input would be hoisted, treat it as invariant */
	                   if (would_be_hoisted.contains(input))
	                       continue;

	                   return false;
	               }
	               return true;
	           };

	           for (Node *node : current_region->nodes())
	           {
	               if (!is_hoistable_expression(node))
	                   continue;

	               if (would_be_hoisted.contains(node))
	                   continue;

	               if (is_invariant_assuming_hoisted(node) &&
	                   safe_to_hoist(node, current_region, hoist_target, tbaa_result))
	               {
	                   auto candidate = HoistCandidate();
	                   candidate.expr = node;
	                   candidate.from = current_region;
	                   candidate.to = hoist_target;
	                   candidate.benefit = compute_benefit(candidate);
	                   candidates.push_back(candidate);
	                   would_be_hoisted.insert(node);
	               }
	           }
	       }

	       /* recursively process child regions to handle nested loops */
	       for (Region *child: current_region->children())
	           worklist.push(child);
	   }

	   return candidates;
	}

	std::vector<Region *> HoistExpr::hoist_candidates(const std::vector<HoistCandidate> &candidates)
	{
		std::unordered_set<Node*> hoisted_nodes;
		std::unordered_set<Region *> modified_regions;

		/* apply hoisting transformations to all validated candidates.
		 * track which regions are modified for analysis invalidation */
		for (const HoistCandidate &candidate: candidates)
		{
			/* if already hoisted/region visited, skip to avoid duplicates */
			if (hoisted_nodes.contains(candidate.expr))
				continue;

			if (hoist_expression(candidate))
			{
				hoisted_nodes.insert(candidate.expr);
				modified_regions.insert(candidate.from);
				modified_regions.insert(candidate.to);
			}
		}

		return { modified_regions.begin(), modified_regions.end() };
	}
}
