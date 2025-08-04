/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <vector>
#include <arc/foundation/pass.hpp>

namespace arc
{
	class Module;
	class PassManager;
	class Region;

	/**
	 * @brief Memory-to-register promotion pass
	 *
	 * Promotes stack-allocated memory (ALLOC nodes) to SSA values by replacing
	 * load/store operations with direct value propagation and phi nodes.
	 */
	class Mem2RegPass final : public TransformPass
	{
	public:
		/**
		 * @brief Get the pass name
		 * @return Pass identifier for dependency resolution
		 */
		[[nodiscard]] std::string name() const override;

		/**
		 * @brief Get required analysis passes
		 * @return Vector of analysis pass names needed by mem2reg
		 */
		[[nodiscard]] std::vector<std::string> require() const override;

		/**
		 * @brief Get analyses invalidated by this pass
		 * @return Vector of analysis names that become stale after mem2reg
		 */
		[[nodiscard]] std::vector<std::string> invalidates() const override;

		/**
		 * @brief Run Mem2reg optimization on the module
		 * @param module Module to optimize
		 * @param pm Pass manager for accessing cached analyses
		 * @return Vector of regions that were modified
		 */
		std::vector<Region*> run(Module& module, PassManager& pm) override;

	private:
		std::vector<Region*> process_function(Region* func_region, const class TypeBasedAliasResult& tbaa);
	};
}
