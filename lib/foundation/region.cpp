/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <algorithm>
#include <queue>
#include <unordered_set>
#include <arc/foundation/module.hpp>
#include <arc/foundation/region.hpp>

namespace arc
{
	Region::Region(const std::string_view name, Module &mod, Region *parent) : mod(mod), prnt(parent)
	{
		region_id = mod.intern_str(name);
		ach::allocator<Node> a;
		Node *n = a.allocate(1);
		std::construct_at(n);
		n->ir_type = NodeType::ENTRY;
		n->parent = this;
		ns.push_back(n);
	}

	Region::~Region() = default;

	std::string_view Region::name() const
	{
		return mod.strtable().get(region_id);
	}

	Region *Region::parent() const
	{
		return prnt;
	}

	Module &Region::module()
	{
		return mod;
	}

	const Module &Region::module() const
	{
		return mod;
	}

	void Region::add_child(Region *child)
	{
		if (!child || std::ranges::find(childs, child) != childs.end())
			return;
		childs.push_back(child);
	}

	const std::vector<Region *> &Region::children() const
	{
		return childs;
	}

	void Region::append(Node *node)
	{
		if (!node)
			return;

		if (node->parent && node->parent != this)
			node->parent->remove(node);

		if (std::ranges::find(ns, node) == ns.end())
		{
			if (node->ir_type == NodeType::ENTRY && !ns.empty() && ns[0]->ir_type == NodeType::ENTRY)
				return;

			if (node->ir_type == NodeType::ENTRY)
				ns.insert(ns.begin(), node);
			else
				ns.push_back(node);
			node->parent = this;
		}
	}

	void Region::remove(Node *node)
	{
		if (!node)
			return;

		auto it = std::ranges::find(ns, node);
		if (it != ns.end())
		{
			ns.erase(it);
			node->parent = nullptr;
		}
	}

	void Region::remove(const std::vector<Node *> &nodes)
	{
		for (Node *node: nodes)
			remove(node);
	}

	const std::vector<Node *> &Region::nodes() const
	{
		return ns;
	}

	void Region::insert_before(Node *before, Node *node)
	{
		if (!before || !node)
			return;

		if (const auto it = std::ranges::find(ns, before);
			it != ns.end())
		{
			if (node->parent && node->parent != this)
				node->parent->remove(node);

			if (it == ns.begin() && (*it)->ir_type == NodeType::ENTRY)
				ns.insert(it + 1, node); /* insert after ENTRY */
			else
				ns.insert(it, node);
			node->parent = this;
		}
	}

	void Region::insert_after(Node *after, Node *node)
	{
		if (!after || !node)
			return;

		if (const auto it = std::ranges::find(ns, after);
			it != ns.end())
		{
			if (node->parent && node->parent != this)
				node->parent->remove(node);

			ns.insert(it + 1, node);
			node->parent = this;
		}
	}

	void Region::insert(Node *node)
	{
		if (!node)
			return;

		if (node->parent && node->parent != this)
			node->parent->remove(node);

		if (!ns.empty() && ns[0]->ir_type == NodeType::ENTRY)
			ns.insert(ns.begin() + 1, node);
		else
			ns.insert(ns.begin(), node);
		node->parent = this;
	}

	bool Region::is_terminated() const
	{
		if (ns.empty())
			return false;

		const Node *last = ns.back();
		if (!last)
			return false;

		/* note: EXIT is not considered a terminator as it can be used
		 * to mark the restore point of registers */
		switch (last->ir_type)
		{
			case NodeType::RET:
			case NodeType::JUMP:
			case NodeType::BRANCH:
			case NodeType::INVOKE:
				return true;
			default:
				return false;
		}
	}

	bool Region::dominates(const Region *possibly_dominated) const
	{
		if (!possibly_dominated)
			return false;

		if (this == possibly_dominated)
			return true;

		/* check if this region has unstructured control flow that might bypass
		 * normal parent-child dominance */
		if (has_unstructured_jumps_to(possibly_dominated))
			return false;

		/* check if any ancestor region has unstructured control flow that might
		 * allow possible_dominated to be reached without going through this region */
		const Region *ancestor = this->parent();
		while (ancestor)
		{
			if (ancestor->has_unstructured_jumps_to(possibly_dominated))
				return false;
			ancestor = ancestor->parent();
		}

		/* standard tree-based dominance check */
		ancestor = possibly_dominated->parent();
		while (ancestor)
		{
			if (ancestor == this)
				return true;
			ancestor = ancestor->parent();
		}

		return false;
	}

	Node *Region::has_unstructured_jumps_to(const Region *target) const
	{
		if (!target)
			return nullptr;

		for (Node *node: ns)
		{
			/* check JUMP nodes */
			if (node->ir_type == NodeType::JUMP)
			{
				if (!node->inputs.empty())
				{
					if (const Node *target_entry = node->inputs[0];
						target_entry && target_entry->parent == target)
					{
						/* this is a jump to the target region;
						 * we need to check if it breaks dominance */
						if (!this->dominates_via_tree(target))
							return node;
					}
				}
			}
			else if (node->ir_type == NodeType::BRANCH)
			{
				if (node->inputs.size() >= 3)
				{
					const Node *true_entry = node->inputs[1];
					const Node *false_entry = node->inputs[2];

					if ((true_entry && true_entry->parent == target) ||
					    (false_entry && false_entry->parent == target))
					{
						/* this is a branch to the target region */
						if (!this->dominates_via_tree(target))
							return node;
					}
				}
			}
			else if (node->ir_type == NodeType::INVOKE)
			{
				if (node->inputs.size() >= 2)
				{
					const Node *normal_entry = node->inputs[node->inputs.size() - 2];
					const Node *exception_entry = node->inputs[node->inputs.size() - 1];

					if ((normal_entry && normal_entry->parent == target) ||
					    (exception_entry && exception_entry->parent == target))
					{
						if (!this->dominates_via_tree(target))
							return node;
					}
				}
			}
		}

		return nullptr;
	}

	bool Region::dominates_via_tree(const Region *possibly_dominated) const
	{
		if (!possibly_dominated)
			return false;

		if (this == possibly_dominated)
			return true;

		/* parent always dominates children in a pure tree-based dominance */
		const Region *ancestor = possibly_dominated->parent();
		while (ancestor)
		{
			if (ancestor == this)
				return true;
			ancestor = ancestor->parent();
		}

		return false;
	}

	bool Region::replace(Node *old_n, Node *new_n, bool rewire)
	{
		if (!old_n || !new_n)
			return false;

		const auto it = std::ranges::find(ns, old_n);
		if (it == ns.end())
			return false;

		/* replace in node list */
		*it = new_n;
		new_n->parent = this;
		old_n->parent = nullptr;

		if (rewire)
		{
			/* update all users of old_n to point to new_n */
			for (Node *user: old_n->users)
			{
				for (std::uint8_t i = 0; i < user->inputs.size(); i++)
				{
					if (user->inputs[i] == old_n)
					{
						user->inputs[i] = new_n;
						if (std::ranges::find(new_n->users, user) == new_n->users.end())
							new_n->users.push_back(user);
					}
				}
			}

			/* transfer inputs from old_n to new_n if new_n has none */
			if (new_n->inputs.empty() && !old_n->inputs.empty())
			{
				for (Node *input: old_n->inputs)
				{
					new_n->inputs.push_back(input);

					/* update input's users list */
					auto &input_users = input->users;
					if (const auto user_it = std::ranges::find(input_users, old_n);
						user_it != input_users.end())
					{
						*user_it = new_n;
					}
					else
						input_users.push_back(new_n);
				}
			}

			/* clear old connections */
			old_n->users.clear();
			old_n->inputs.clear();
		}

		return true;
	}

	Region *Region::find_lca(Region *other) const
	{
		if (!other || this == other)
			return const_cast<Region *>(this);

		std::size_t depth_a = 0, depth_b = 0;
		for (const Region *r = this->parent(); r; r = r->parent())
			++depth_a;
		for (const Region *r = other->parent(); r; r = r->parent())
			++depth_b;

		Region *a = const_cast<Region *>(this);
		Region *b = other;

		/* bring to same level and walk up together */
		while (depth_a > depth_b)
		{
			a = a->parent();
			--depth_a;
		}

		while (depth_b > depth_a)
		{
			b = b->parent();
			--depth_b;
		}

		while (a && b && a != b)
		{
			a = a->parent();
			b = b->parent();
		}

		return a;
	}

	void Region::walk_dominated_regions(const std::function<void(Region *)> &visitor) const
	{
		visitor(const_cast<Region *>(this));
		for (const Region *child: children())
			child->walk_dominated_regions(visitor);
	}

	bool Region::can_reach(Region *target) const
	{
		if (!target)
			return false;
		if (this == target)
			return true;

		std::unordered_set<const Region *> visited;
		std::queue<const Region *> worklist;
		worklist.push(this);

		while (!worklist.empty())
		{
			const Region *current = worklist.front();
			worklist.pop();

			if (visited.contains(current))
				continue;
			visited.insert(current);

			if (current == target)
				return true;

			/* follow direct control flow transfers only */
			for (Node *node: current->nodes())
			{
				switch (node->ir_type)
				{
					case NodeType::JUMP:
						/* unconditional transfer */
						if (!node->inputs.empty() && node->inputs[0] && node->inputs[0]->parent)
							worklist.push(node->inputs[0]->parent);
						break;

					case NodeType::BRANCH:
						/* conditional transfer; both paths */
						if (node->inputs.size() >= 3)
						{
							if (Node *true_entry = node->inputs[1];
								true_entry && true_entry->parent)
								worklist.push(true_entry->parent);
							if (Node *false_entry = node->inputs[2];
								false_entry && false_entry->parent)
								worklist.push(false_entry->parent);
						}
						break;

					case NodeType::INVOKE:
						/* function call with exception handling */
						if (node->inputs.size() >= 2)
						{
							if (Node *normal_entry = node->inputs[node->inputs.size() - 2];
								normal_entry && normal_entry->parent)
								worklist.push(normal_entry->parent);
							if (Node *except_entry = node->inputs[node->inputs.size() - 1];
								except_entry && except_entry->parent)
								worklist.push(except_entry->parent);
						}
						break;

					case NodeType::RET:
						/* return ends execution path */
						break;

					default:
						break;
				}
			}
		}

		return false;
	}
}
