/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <arc/foundation/pass.hpp>

namespace arc
{
	class Module;
	class PassManager;
	struct Node;
	class Region;
	class TypeBasedAliasResult;

	/**
	 * @brief Dead store elimination transform pass
	 *
	 * Eliminates stores that are overwritten before being read using a forward
	 * analysis approach. Tracks the last store to each memory location and marks
	 * previous stores as dead when they are overwritten.
	 */
	class DeadStoreEliminationPass final : public TransformPass
	{
	public:
		/**
		 * @brief Get the pass name
		 */
		[[nodiscard]] std::string name() const override;

		/**
		 * @brief Get required analysis passes
		 */
		[[nodiscard]] std::vector<std::string> require() const override;

		/**
		 * @brief Get analyses invalidated by this pass
		 */
		[[nodiscard]] std::vector<std::string> invalidates() const override;

		/**
		 * @brief Run dead store elimination on the module
		 */
		std::vector<Region*> run(Module& module, PassManager& pm) override;

	private:
		/**
		 * @brief Process a single function for dead store elimination
		 */
		std::vector<Region*> process_function(Region* func_region,
											 const TypeBasedAliasResult& tbaa_result);

		std::size_t process_region(Region *region, const TypeBasedAliasResult &tbaa_result);

		/**
		 * @brief Find dead stores across all regions in a function
		 */
		std::vector<Node*> find_dead_stores_cross_region(
			const std::vector<std::pair<Node*, Region*>>& nodes_with_regions,
			const TypeBasedAliasResult& tbaa_result);

		/**
		 * @brief Get the memory address being stored to
		 */
		static Node* get_store_address(Node* store);

		/**
		 * @brief Get the memory address being accessed
		 */
		static Node* get_memory_address(Node* memory_op);
	};
}
