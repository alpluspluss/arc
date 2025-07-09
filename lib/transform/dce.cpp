/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <queue>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/foundation/region.hpp>
#include <arc/transform/dce.hpp>

namespace arc
{
	std::string DeadCodeElimination::name() const
	{
		return "dead-code-elimination";
	}

	std::vector<std::string> DeadCodeElimination::invalidates() const
	{
		return {}; /* note: to be filled with analysis names */
	}

	std::vector<Region *> DeadCodeElimination::run(Module &module, PassManager &pm)
	{
		alive_nodes.clear();
		dead_nodes.clear();
		std::vector<Region *> modified_regions;

		/* find live nodes starting from root region and all function regions */
		find_live_nodes(module.root());
		for (const Node *fn: module.functions())
		{
			if (fn->ir_type != NodeType::FUNCTION)
				continue;

			/* find the corresponding function region */
			const std::string_view fn_name = module.strtable().get(fn->str_id);
			for (Region *child: module.root()->children())
			{
				if (child->name() == fn_name)
				{
					find_live_nodes(child);
					break;
				}
			}
		}

		/* mark and remove dead nodes */
		find_dead_nodes(module.root());
		remove_dead_nodes(modified_regions);
		return modified_regions;
	}

	void DeadCodeElimination::find_live_nodes(Region *region)
	{
		if (!region)
			return;

		/* worklist algorithm to avoid deep recursion */
		std::queue<Node *> worklist;
		std::vector<Region *> region_worklist;
		region_worklist.push_back(region);

		while (!region_worklist.empty())
		{
			const Region *current_region = region_worklist.back();
			region_worklist.pop_back();

			/* find all root nodes in this region */
			for (Node *node: current_region->nodes())
			{
				if (is_root_node(node))
				{
					if (alive_nodes.insert(node).second)
						worklist.push(node);
				}
			}

			/* add child regions to worklist */
			for (Region *child: current_region->children())
				region_worklist.push_back(child);
		}

		/* propagate liveness backwards through use-def chains */
		while (!worklist.empty())
		{
			Node *current = worklist.front();
			worklist.pop();

			for (Node *input: current->inputs)
			{
				if (input && alive_nodes.insert(input).second)
					worklist.push(input);
			}
		}
	}

	void DeadCodeElimination::find_dead_nodes(Region *region)
	{
		if (!region)
			return;

		/* worklist */
		std::vector<Region *> worklist;
		worklist.push_back(region);

		while (!worklist.empty())
		{
			Region *current_region = worklist.back();
			worklist.pop_back();

			/* any node not in alive set is dead */
			for (Node *node: current_region->nodes())
			{
				if (!alive_nodes.contains(node))
					dead_nodes.insert(node);
			}

			/* add child regions to worklist */
			for (Region *child: current_region->children())
				worklist.push_back(child);
		}
	}

	std::size_t DeadCodeElimination::remove_dead_nodes(std::vector<Region *> &modified_regions)
	{
		if (dead_nodes.empty())
			return 0;

		std::unordered_set<Region *> modified_set;
		std::size_t removed = 0;
		for (Node *node: dead_nodes)
		{
			/* remove from users list of all inputs */
			for (Node *input: node->inputs)
			{
				if (!input)
					continue;

				auto &users = input->users;
				for (auto it = users.begin(); it != users.end(); ++it)
				{
					if (*it == node)
					{
						users.erase(it);
						break;
					}
				}
			}

			/* remove from parent region */
			if (Region *parent = node->parent)
			{
				parent->remove(node);
				modified_set.insert(parent);
				removed++;
			}
		}

		/* convert set to vector for return */
		modified_regions.reserve(modified_set.size());
		for (Region *region: modified_set)
			modified_regions.push_back(region);

		return removed;
	}

	bool DeadCodeElimination::is_root_node(const Node *node)
	{
		if (!node)
			return false;

		if (is_global_scope(node->parent))
			return true;

		/* structural nodes should be preserved */
		if (node->ir_type == NodeType::ENTRY ||
		    node->ir_type == NodeType::FUNCTION)
		{
			return true;
		}

		/* return nodes, parameters and exit nodes are roots */
		if (node->ir_type == NodeType::RET ||
		    node->ir_type == NodeType::EXIT ||
		    node->ir_type == NodeType::PARAM)
		{
			return true;
		}

		/* control flow nodes must be preserved */
		if (node->ir_type == NodeType::BRANCH ||
		    node->ir_type == NodeType::JUMP ||
		    node->ir_type == NodeType::INVOKE)
		{
			return true;
		}

		/* side effects - stores and atomic operations */
		if (node->ir_type == NodeType::STORE ||
		    node->ir_type == NodeType::PTR_STORE ||
		    node->ir_type == NodeType::ATOMIC_STORE ||
		    node->ir_type == NodeType::ATOMIC_CAS)
		{
			return true;
		}

		/* conservatively assume all calls have side effects */
		if (node->ir_type == NodeType::CALL)
		{
			/* could be more precise with call graph analysis,
			 * but conservatively assume all calls are roots for now.
			 * ADCE would better suited for this case */
			return true;
		}
		/* no optimization for volatile nodes */
		return (node->traits & NodeTraits::VOLATILE) != NodeTraits::NONE;
	}

	bool DeadCodeElimination::is_global_scope(const Region *region)
	{
		if (!region)
			return false;
		return region->parent() == nullptr; /* root region has no parent and is the global scope */
	}
}
