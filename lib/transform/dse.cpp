/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <unordered_map>
#include <unordered_set>
#include <arc/analysis/tbaa.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/foundation/region.hpp>
#include <arc/transform/dse.hpp>

namespace arc
{
	namespace
	{
		bool is_store_operation(const Node* node)
		{
			if (!node)
				return false;

			return node->ir_type == NodeType::STORE ||
			       node->ir_type == NodeType::PTR_STORE;
		}

		bool is_load_operation(const Node* node)
		{
			if (!node)
				return false;

			return node->ir_type == NodeType::LOAD ||
			       node->ir_type == NodeType::PTR_LOAD;
		}

		bool is_call_operation(const Node* node)
		{
			if (!node)
				return false;

			return node->ir_type == NodeType::CALL ||
			       node->ir_type == NodeType::INVOKE;
		}
	}

	std::string DeadStoreEliminationPass::name() const
	{
		return "dead-store-elimination";
	}

	std::vector<std::string> DeadStoreEliminationPass::require() const
	{
		return {"type-based-alias-analysis"};
	}

	std::vector<std::string> DeadStoreEliminationPass::invalidates() const
	{
		return {};
	}

	std::vector<Region*> DeadStoreEliminationPass::run(Module& module, PassManager& pm)
	{
		const auto& tbaa_result = pm.get<TypeBasedAliasResult>();
		std::vector<Region*> all_modified_regions;

		for (const Node* func_node : module.functions())
		{
			if (func_node->ir_type != NodeType::FUNCTION)
				continue;

			const std::string_view func_name = module.strtable().get(func_node->str_id);
			Region* func_region = nullptr;
			for (Region* child : module.root()->children())
			{
				if (child->name() == func_name)
				{
					func_region = child;
					break;
				}
			}

			if (!func_region)
				continue;

			std::vector<Region*> modified_regions = process_function(func_region, tbaa_result);
			all_modified_regions.insert(all_modified_regions.end(),
			                           modified_regions.begin(),
			                           modified_regions.end());
		}

		return all_modified_regions;
	}

	std::vector<Region*> DeadStoreEliminationPass::process_function(Region* func_region,
	                                                               const TypeBasedAliasResult& tbaa_result)
	{
		std::unordered_set<Region*> modified_regions_set;

		func_region->walk_dominated_regions([&](Region* region)
		{
			if (std::size_t removed = process_region(region, tbaa_result); removed > 0)
				modified_regions_set.insert(region);
		});

		std::vector<Region*> modified_regions;
		modified_regions.reserve(modified_regions_set.size());
		for (Region* region : modified_regions_set)
			modified_regions.push_back(region);
		return modified_regions;
	}

	std::size_t DeadStoreEliminationPass::process_region(Region* region, const TypeBasedAliasResult& tbaa_result)
	{
		if (!region)
			return 0;

		/* dead store elimination uses forward analysis.
		 *
		 * 1. track the last store to each memory location
		 * 2. when a new store appears, mark previous stores to same location as potentially dead
		 * 3. when a load appears, mark stores to aliasing locations as definitely live
		 * 4. when a call appears, mark stores to escaped addresses as definitely live
		 * 5. eliminate stores that are potentially dead but not definitely live
		 *
		 * it is more reliable than backward analysis because it processes
		 * operations in execution order and maintains precise liveness information */

		std::unordered_map<Node*, Node*> last_store_to_location;
		std::unordered_set<Node*> potentially_dead_stores;
		std::unordered_set<Node*> definitely_live_stores;
		for (Node* node : region->nodes())
		{
			if (is_store_operation(node))
			{
				/* volatile stores have observable side effects */
				if ((node->traits & NodeTraits::VOLATILE) != NodeTraits::NONE)
				{
					definitely_live_stores.insert(node);
					continue;
				}

				Node* store_addr = get_store_address(node);
				if (!store_addr)
					continue;

				/* check if this store overwrites previous stores to aliasing locations */
				std::vector<Node*> aliasing_addresses_to_remove;
				for (const auto& [other_addr, other_store] : last_store_to_location)
				{
					TBAAResult alias = tbaa_result.alias(node, other_store);
					if (alias == TBAAResult::MUST_ALIAS)
					{
						/* this store definitely overwrites the previous store */
						potentially_dead_stores.insert(other_store);
						aliasing_addresses_to_remove.push_back(other_addr);
					}
				}

				/* if we already have a store to this exact location, mark it as potentially dead */
				if (auto it = last_store_to_location.find(store_addr); it != last_store_to_location.end())
					potentially_dead_stores.insert(it->second);

				/* remove overwritten addresses from tracking and record this as the new last store */
				for (Node* addr_to_remove : aliasing_addresses_to_remove)
					last_store_to_location.erase(addr_to_remove);

				last_store_to_location[store_addr] = node;
			}
			else if (is_load_operation(node))
			{
				Node* load_addr = get_memory_address(node);
				if (!load_addr)
					continue;

				/* this load makes any stores to aliasing locations live */
				for (const auto& [store_addr, store] : last_store_to_location)
				{
					TBAAResult alias = tbaa_result.alias(node, store);
					if (alias != TBAAResult::NO_ALIAS)
					{
						/* definitely live as store is read by this load */
						definitely_live_stores.insert(store);
						potentially_dead_stores.erase(store);
					}
				}
			}
			else if (is_call_operation(node))
			{
				/* function calls may read escaped memory locations */
				for (const auto& [store_addr, store] : last_store_to_location)
				{
					if (tbaa_result.has_escaped(store_addr))
					{
						/* definitely live store to escaped address may be read by call */
						definitely_live_stores.insert(store);
						potentially_dead_stores.erase(store);
					}
				}
			}
		}

		/* determine final set of stores to eliminate */
		std::vector<Node*> final_stores_to_remove;
		for (Node* store : potentially_dead_stores)
		{
			if (!definitely_live_stores.contains(store))
			{
				if (Node* store_addr = get_store_address(store);
				    store_addr && !tbaa_result.has_escaped(store_addr))
				{
					final_stores_to_remove.push_back(store);
				}
			}
		}

		/* remove dead stores from the region */
		for (Node* store : final_stores_to_remove)
			region->remove(store);
		return final_stores_to_remove.size();
	}

	Node* DeadStoreEliminationPass::get_store_address(Node* store)
	{
		if (!store)
			return nullptr;

		switch (store->ir_type)
		{
			case NodeType::STORE:
			case NodeType::PTR_STORE:
				/* store operations have value as input[0] and address as input[1] */
				return store->inputs.size() > 1 ? store->inputs[1] : nullptr;
			default:
				return nullptr;
		}
	}

	Node* DeadStoreEliminationPass::get_memory_address(Node* memory_op)
	{
		if (!memory_op)
			return nullptr;

		switch (memory_op->ir_type)
		{
			case NodeType::LOAD:
			case NodeType::PTR_LOAD:
				/* load operations have address as input[0] */
				return memory_op->inputs.empty() ? nullptr : memory_op->inputs[0];
			case NodeType::STORE:
			case NodeType::PTR_STORE:
				/* store operations have value as input[0] and address as input[1] */
				return memory_op->inputs.size() > 1 ? memory_op->inputs[1] : nullptr;
			default:
				return nullptr;
		}
	}
}
