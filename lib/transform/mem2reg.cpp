/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <algorithm>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <arc/analysis/tbaa.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/foundation/region.hpp>
#include <arc/support/allocator.hpp>
#include <arc/transform/mem2reg.hpp>

namespace arc
{
	/**
	 * @brief Information about a promotable stack allocation
	 */
	struct AllocInfo
	{
		Node *alloc_node = nullptr;
		std::vector<Node *> stores;
		std::vector<Node *> loads;
		std::unordered_map<Region *, Node *> phi_nodes;
		std::unordered_map<Region *, Node *> definitions;
		bool promotable = true;
	};

	static bool is_promotable(Node *alloc, const TypeBasedAliasResult &tbaa)
	{
		if (!alloc || alloc->ir_type != NodeType::ALLOC)
			return false;

		/* if allocation has escaped or if it's a recognized allocation site */
		if (tbaa.has_escaped(alloc) || !tbaa.is_allocation_site(alloc))
			return false;

		/* don't promote volatile allocations */
		if ((alloc->traits & NodeTraits::VOLATILE) != NodeTraits::NONE)
			return false;
		return true;
	}

	static bool is_load_op(const Node *node)
	{
		if (!node)
			return false;

		return node->ir_type == NodeType::LOAD ||
		       node->ir_type == NodeType::PTR_LOAD ||
		       node->ir_type == NodeType::ATOMIC_LOAD;
	}

	static bool is_store_op(const Node *node)
	{
		if (!node)
			return false;

		return node->ir_type == NodeType::STORE ||
		       node->ir_type == NodeType::PTR_STORE ||
		       node->ir_type == NodeType::ATOMIC_STORE;
	}

	static void collect_accesses(Node *alloc, const TypeBasedAliasResult &tbaa, AllocInfo &info)
	{
		/* you can't really promote ADDR_OF nodes; imagine doing `&5` in C/C++ */
		for (Node *user: alloc->users)
		{
			/* if any user is taking the address, this allocation can't be promoted */
			if (user->ir_type == NodeType::ADDR_OF)
			{
				info.promotable = false;
				return;
			}
		}

		for (Node *access: tbaa.memory_accesses())
		{
			if (const MemoryLocation *loc = tbaa.memory_location(access);
				!loc || loc->allocation_site != alloc)
			{
				continue;
			}

			/* categorize access type */
			if (is_load_op(access))
				info.loads.push_back(access);
			else if (is_store_op(access))
				info.stores.push_back(access);
			else /* unknown */
			{
				info.promotable = false;
				return;
			}
		}
	}

	static bool verify_type_consistency(const AllocInfo &info, const TypeBasedAliasResult &tbaa)
	{
		if (!info.alloc_node)
			return false;

		/* all LOADs and STOREs need to be type-compatible */
		const DataType expected_type = info.alloc_node->type_kind;
		for (Node *load: info.loads)
		{
			if (const MemoryLocation *loc = tbaa.memory_location(load);
				!loc || loc->access_type != expected_type)
			{
				return false;
			}
		}

		for (Node *store: info.stores)
		{
			if (const MemoryLocation *loc = tbaa.memory_location(store);
				!loc || loc->access_type != expected_type)
			{
				return false;
			}
		}

		return true;
	}

	static Node *create_phi_node(Region *region, DataType type)
	{
		if (!region)
			return nullptr;

		/* create FROM*/
		ach::shared_allocator<Node> alloc;
		Node *phi = alloc.allocate(1);
		std::construct_at(phi);

		phi->ir_type = NodeType::FROM;
		phi->type_kind = type;
		phi->parent = region;

		/* insert after ENTRY node */
		if (Node *entry = region->entry())
			region->insert_after(entry, phi);
		else
			region->append(phi);

		return phi;
	}

	static std::vector<AllocInfo> analyze_promotable_allocs(Region *region, const TypeBasedAliasResult &tbaa)
	{
		std::vector<AllocInfo> result;
		std::queue<Region *> worklist;
		worklist.push(region);

		/* find all ALLOC nodes in the function */
		while (!worklist.empty())
		{
			Region *current = worklist.front();
			worklist.pop();

			for (Node *node: current->nodes())
			{
				if (node->ir_type == NodeType::ALLOC)
				{
					if (is_promotable(node, tbaa))
					{
						AllocInfo info;
						info.alloc_node = node;
						collect_accesses(node, tbaa, info);

						if (info.promotable && verify_type_consistency(info, tbaa))
							result.push_back(std::move(info));
					}
				}
			}

			/* add child regions to worklist */
			for (Region *child: current->children())
				worklist.push(child);
		}

		return result;
	}

	static void insert_phi_nodes(AllocInfo &alloc_info)
	{
		/* this used to be LCA-based but now turned into load-centric phi insertion;
		 *
		 * the algorithm collect all store regions that could reach this load and
		 * if store and load are in different regions, place phi at load's region
		 * if multiple stores can reach this load.
		 */
		std::unordered_set<Region *> phi_regions;
		for (Node *load: alloc_info.loads)
		{
			Region *load_region = load->parent;
			if (!load_region)
				continue;

			std::vector<Region *> reaching_stores;
			for (Node *store: alloc_info.stores)
			{
				Region *store_region = store->parent;
				if (!store_region)
					continue;

				if (store_region != load_region)
					reaching_stores.push_back(store_region);
			}

			if (reaching_stores.size() > 1)
				phi_regions.insert(load_region);
		}

		/* create phi nodes at identified regions */
		for (Region *phi_region: phi_regions)
		{
			Node *phi = create_phi_node(phi_region, alloc_info.alloc_node->type_kind);
			alloc_info.phi_nodes[phi_region] = phi;
		}
	}

	static void rename_variables(Region *func_region, AllocInfo &alloc_info)
	{
		/* walk regions in domination order and rename variables */
		func_region->walk_dominated_regions([&](Region *region)
		{
			/* check if this region has a phi node for this allocation */
			Node *current_def = nullptr;
			if (const auto it = alloc_info.phi_nodes.find(region);
				it != alloc_info.phi_nodes.end())
			{
				current_def = it->second;
			}
			else
			{
				if (Region *parent = region->parent())
				{
					if (auto parent_it = alloc_info.definitions.find(parent);
						parent_it != alloc_info.definitions.end())
					{
						current_def = parent_it->second;
					}
				}
			}

			for (Node *node: region->nodes())
			{
				/* replace loads with current definition */
				if (is_load_op(node))
				{
					if (auto load_it = std::ranges::find(alloc_info.loads, node);
						load_it != alloc_info.loads.end() && current_def)
					{
						for (Node *user: node->users)
						{
							for (std::size_t i = 0; i < user->inputs.size(); ++i)
							{
								if (user->inputs[i] == node)
								{
									user->inputs[i] = current_def;
									if (std::ranges::find(current_def->users, user) == current_def->users.end())
										current_def->users.push_back(user);
								}
							}
						}
						node->users.clear();
					}
				}
				/* stores create new definitions */
				else if (is_store_op(node))
				{
					if (auto store_it = std::ranges::find(alloc_info.stores, node);
						store_it != alloc_info.stores.end())
					{
						/* the stored value becomes the new definition */
						if (!node->inputs.empty())
						{
							current_def = node->inputs[0];
							alloc_info.definitions[region] = current_def;
						}
					}
				}
			}
			/* record current definition for this region */
			if (current_def)
				alloc_info.definitions[region] = current_def;

			/* note: defer phi input wiring to after all regions are processed
			 * so we have the full information */
		});

		/* wire phi node inputs */
		for (auto &[phi_region, phi_node]: alloc_info.phi_nodes)
		{
			std::unordered_set<Node *> phi_inputs;

			/* collect definitions from regions that can reach this phi */
			for (auto &[def_region, definition]: alloc_info.definitions)
			{
				if (def_region != phi_region && definition)
				{
					/* check if control flow can go from def_region to phi_region */
					if (def_region->can_reach(phi_region))
						phi_inputs.insert(definition);
				}
			}

			/* wire phi inputs */
			phi_node->inputs.clear();
			phi_node->inputs.reserve(phi_inputs.size());
			for (Node *input: phi_inputs)
			{
				phi_node->inputs.push_back(input);
				if (std::ranges::find(input->users, phi_node) == input->users.end())
					input->users.push_back(phi_node);
			}
		}
	}

	static void cleanup_allocations(const std::vector<AllocInfo> &infos, std::vector<Region *> &modified_regions)
	{
		std::unordered_set<Region *> regions_to_modify;
		for (const AllocInfo &info: infos)
		{
			/* remove all load operations */
			for (Node *load: info.loads)
			{
				if (Region *parent = load->parent)
				{
					parent->remove(load);
					regions_to_modify.insert(parent);
				}
			}

			/* remove all store operations */
			for (Node *store: info.stores)
			{
				if (Region *parent = store->parent)
				{
					parent->remove(store);
					regions_to_modify.insert(parent);
				}
			}

			/* remove the original allocation */
			if (Node *alloc = info.alloc_node;
				alloc && alloc->parent)
			{
				alloc->parent->remove(alloc);
				regions_to_modify.insert(alloc->parent);
			}
		}

		/* add modified regions to result vector */
		for (Region *region: regions_to_modify)
			modified_regions.push_back(region);
	}

	std::string Mem2RegPass::name() const
	{
		return "mem2reg";
	}

	std::vector<std::string> Mem2RegPass::require() const
	{
		return { "type-based-alias-analysis" };
	}

	std::vector<std::string> Mem2RegPass::invalidates() const
	{
		return {};
		/* note: TBAA remains valid after mem2reg because other passes that run after
		 * mem2reg would not be able to reference the dead node */
	}

	std::vector<Region *> Mem2RegPass::run(Module &module, PassManager &pm)
	{
		const auto &tbaa_result = pm.get<TypeBasedAliasResult>();
		std::vector<Region *> all_modified_regions = {};

		for (const Node *func_node: module.functions())
		{
			if (func_node->ir_type != NodeType::FUNCTION)
				continue;

			const std::string_view func_name = module.strtable().get(func_node->str_id);
			Region *func_region = nullptr;
			for (Region *child: module.root()->children())
			{
				if (child->name() == func_name)
				{
					func_region = child;
					break;
				}
			}

			if (!func_region)
				continue;

			std::vector<Region *> modified_regions = process_function(func_region, tbaa_result);
			all_modified_regions.insert(all_modified_regions.end(), modified_regions.begin(), modified_regions.end());
		}

		return all_modified_regions;
	}

	std::vector<Region *> Mem2RegPass::process_function(Region *func_region, const TypeBasedAliasResult &tbaa)
	{
		std::vector<Region *> modified_regions;

		/* analyze and find promotable allocations then process each promotable allocation */
		std::vector<AllocInfo> promotable_allocs = analyze_promotable_allocs(func_region, tbaa);
		for (AllocInfo &alloc_info: promotable_allocs)
		{
			if (!alloc_info.promotable)
				continue;

			insert_phi_nodes(alloc_info);
			rename_variables(func_region, alloc_info);
		}

		/* after that, cleanup memory operations */
		cleanup_allocations(promotable_allocs, modified_regions);
		return modified_regions;
	}
}
