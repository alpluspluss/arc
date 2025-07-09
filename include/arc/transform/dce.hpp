/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <unordered_set>
#include <vector>
#include <arc/foundation/module.hpp>
#include <arc/foundation/node.hpp>
#include <arc/foundation/pass.hpp>
#include <arc/foundation/region.hpp>

namespace arc
{
	/**
	 * @brief Dead code elimination transform pass
	 *
	 * Eliminates nodes that have no observable effects on program behavior.
	 * Uses a mark-and-sweep algorithm starting from root nodes such as returns, stores,
	 * calls with side effects, etc.; marking all transitively used nodes as live.
	 */
	class DeadCodeElimination final : public TransformPass
	{
	public:
		/**
		 * @brief Get the pass name
		 * @return Pass identifier used for dependency resolution
		 */
		std::string name() const override;

		/**
		 * @brief Get the list of analyses this pass invalidates
		 * @return Vector of analysis names that become stale after DCE
		 */
		std::vector<std::string> invalidates() const override;

		/**
		 * @brief Run dead code elimination on the module
		 * @param module Module to optimize
		 * @param pm Pass manager for accessing cached analyses
		 * @return Vector of regions that were modified
		 */
		std::vector<Region *> run(Module &module, PassManager &pm) override;

	private:
		std::unordered_set<Node *> alive_nodes;
		std::unordered_set<Node *> dead_nodes;

		/**
		 * @brief Find all live nodes starting from root nodes
		 * @param region Region to analyze
		 */
		void find_live_nodes(Region *region);

		/**
		 * @brief Mark nodes not in alive set as dead
		 * @param region Region to analyze
		 */
		void find_dead_nodes(Region *region);

		/**
		 * @brief Remove dead nodes from the IR
		 * @param modified_regions Output vector of modified regions
		 * @return Number of nodes removed
		 */
		std::size_t remove_dead_nodes(std::vector<Region *> &modified_regions);

		/**
		 * @brief Check if a node should be considered a root/always live
		 * @param node Node to check
		 * @return true if node has observable side effects or is structural
		 */
		static bool is_root_node(const Node *node);

		/**
		 * @brief Check if a region is the global scope
		 * @param region Region to check
		 * @return true if this is the module's root region
		 */
		static bool is_global_scope(const Region *region);
	};
}
