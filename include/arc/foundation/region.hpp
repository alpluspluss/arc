/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <functional>
#include <string_view>
#include <vector>
#include <arc/foundation/node.hpp>

namespace arc
{
	class Module;

	class Region
	{
	public:
		/**
		 * @brief Construct a new region
		 * @param name Name of the region
		 * @param mod Module that owns this region
		 * @param parent And optional parameter to specific the parent region
		 */
		Region(std::string_view name, Module& mod, Region* parent = nullptr);

		/**
		 * @brief Destructor
		 */
		~Region();

		Region(const Region &) = delete;

		Region &operator=(const Region &) = delete;

		Region(Region &&) = delete;

		Region &operator=(Region &&) = delete;

		/**
		 * @brief Get the name of the region
		 */
		[[nodiscard]] std::string_view name() const;

		/**
		 * @brief Get the parent region
		 */
		[[nodiscard]] Region* parent() const;

		/**
		 * @brief Get the module that owns this region
		 */
		Module& module();

		/**
		 * @brief Get the non-modifyable module that owns this region
		 */
		[[nodiscard]] const Module& module() const;

		/**
		 * @brief Add a child region
		 * @param child Child region to add
		 */
		void add_child(Region* child);

		/**
		 * @brief Get all children regions
		 */
		[[nodiscard]] const std::vector<Region*>& children() const;

		/**
		 * @brief Append an existing node to this region
		 */
		void append(Node* node);

		/**
		 * @brief Remove the specified node from this region
		 */
		void remove(Node* node);

		/**
		 * @brief Bulk remove the specified vector of nodes from this region
		 */
		void remove(const std::vector<Node*>& nodes);

		/**
		 * @brief Get all nodes in this region
		 */
		[[nodiscard]] const std::vector<Node*>& nodes() const;

		/**
		 * @brief Insert a node before another node in the region
		 * @param before Node to insert before
		 * @param node Node to insert
		 */
		void insert_before(Node* before, Node* node);

		/**
		 * @brief Insert a node after a node in the region
		 * @param before The node to insert after
		 * @param node The node to insert
		 */
		void insert_after(Node* before, Node* node);

		/**
		 * @brief Insert a node at the beginning of the region
		 * @param node Node to insert
		 */
		void insert(Node* node);

		/**
		 * @brief Check if this region is terminated e.g. ends with return, branch
		 *	and similar structs
		 */
		[[nodiscard]] bool is_terminated() const;

		/**
		 * @brief Check if this region dominates another region
		 */
		bool dominates(const Region* possibly_dominated) const;

		/**
		 * @brief Check if this region contains unstructured control flow targeting another region
		 * @param target The target region to check for unstructured jumps to
		 * @return Node that has the jump to another target
		 */
		Node* has_unstructured_jumps_to(const Region* target) const;

		/**
		 * @brief Check dominance using only parent-child hierarchy
		 * @param possibly_dominated The region to check if it's dominated by this region
		 * @return true if this region dominates the target via hierarchical structure, false otherwise
		 */
		bool dominates_via_tree(const Region* possibly_dominated) const;

		/**
		 * @brief Replace a node with another node in this region
		 * @param old_n Node to be replaced
		 * @param new_n Node to replace with
		 * @param rewire Whether to update the connections between nodes
		 */
		bool replace(Node* old_n, Node* new_n, bool rewire = true);

		/**
		 * @brief Return the entry node inside region
		 * @return The ENTRY node of the region
		 */
		Node* entry()
		{
			if (!ns.empty() && ns[0]->ir_type == NodeType::ENTRY)
				return ns[0];

			const auto it = std::ranges::find_if(ns,
			                               [](const Node* node)
			                               {
				                               return node && node->ir_type == NodeType::ENTRY;
			                               });
			return it != ns.end() ? *it : nullptr;
		}

		/**
		 * @brief Find the lowest common ancestor with another region
		 * @param other Region to find LCA with
		 * @return LCA region, or nullptr if no common ancestor
		 */
		[[nodiscard]] Region* find_lca(Region* other) const;

		/**
		 * @brief Walk this region and all dominated regions in pre-order
		 * @param visitor Function to call for each region
		 */
		void walk_dominated_regions(const std::function<void(Region*)>& visitor) const;

		/**
		 * @brief Check if control flow can reach another region
		 * @param target Region to check reachability to
		 * @return true if there's a control flow path from this region to target
		 */
		[[nodiscard]] bool can_reach(Region* target) const;

		/**
		 * @brief Check if this region is an immediate predecessor of the target region
		 * @param target Target region to check
		 * @return true if this region directly transfers control to target
		 */
		bool imm_predecessor_of(const Region* target) const;

	private:
		std::vector<Region*> childs; /** @brief Children regions */
		std::vector<Node*> ns; /** @brief Nodes the region contains */
		Module& mod; /** @brief Module that owns this region */
		Region* prnt; /** @brief The parent region */
		StringTable::StringId region_id; /** @brief Region id to intern */
	};
}
