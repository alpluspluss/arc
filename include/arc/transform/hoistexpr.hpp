/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <cstdint>
#include <vector>
#include <arc/analysis/tbaa.hpp>
#include <arc/foundation/pass.hpp>

namespace arc
{
	class Module;
	class PassManager;
	struct Node;
	class Region;

	/**
	 * @brief Represents a candidate expression for hoisting
	 */
	struct HoistCandidate
	{
		/** @brief Expression node to be hoisted */
		Node *expr;
		/** @brief Current region containing the expression */
		Region *from;
		/** @brief Target region to move expression to */
		Region *to;
		/** @brief Estimated benefit score for prioritization */
		std::uint32_t benefit;
	};

	/**
	 * @brief Expression hoisting optimization pass
	 *
	 * Moves loop-invariant expressions from loop regions to dominating regions,
	 * reducing redundant computation in iterative code. Uses Arc's region structure
	 * to identify loops through back-edge detection without requiring complex loop
	 * analysis infrastructure.
	 *
	 * The pass identifies expressions whose operands are defined outside the loop
	 * region and safely relocates them to parent regions that dominate the loop.
	 * Memory operations are handled conservatively using TBAA to ensure no
	 * aliasing violations occur during hoisting.
	 */
	class HoistExpr final : public TransformPass
	{
	public:
		/**
		 * @brief Get the pass name
		 * @return Pass identifier for dependency resolution
		 */
		[[nodiscard]] std::string name() const override;

		/**
		 * @brief Get required analysis passes
		 * @return Vector of analysis pass names needed by expression hoisting
		 */
		[[nodiscard]] std::vector<std::string> require() const override;

		/**
		 * @brief Get analyses invalidated by this pass
		 * @return Vector of analysis names that become stale after hoisting
		 */
		[[nodiscard]] std::vector<std::string> invalidates() const override;

		/**
		 * @brief Run expression hoisting optimization on the module
		 * @param module Module to optimize
		 * @param pm Pass manager for accessing cached analyses
		 * @return Vector of regions that were modified
		 */
		std::vector<Region *> run(Module &module, PassManager &pm) override;

	private:
		/**
		 * @brief Find all viable hoisting candidates in the module
		 * @param module Module to analyze for hoisting opportunities
		 * @param tbaa_result TBAA analysis for memory aliasing queries
		 * @return Vector of candidate expressions with their target locations
		 */
		std::vector<HoistCandidate> find_candidates(Module &module, const TypeBasedAliasResult &tbaa_result);

		/**
		 * @brief Process all functions in the module for hoisting candidates
		 * @param module Module containing functions to process
		 * @param tbaa_result TBAA analysis result for alias queries
		 * @return Vector of hoisting candidates across all functions
		 */
		std::vector<HoistCandidate> process_module(Module &module, const TypeBasedAliasResult &tbaa_result);

		/**
		 * @brief Analyze a single region for hoisting opportunities
		 * @param region Region to analyze for invariant expressions
		 * @param tbaa_result TBAA analysis for memory safety checks
		 * @return Vector of candidates found in this region
		 */
		std::vector<HoistCandidate> process_region(Region *region, const TypeBasedAliasResult &tbaa_result);

		/**
		 * @brief Apply hoisting transformations to candidate expressions
		 * @param candidates Vector of validated hoisting candidates
		 * @return Vector of regions that were modified by hoisting
		 */
		std::vector<Region *> hoist_candidates(const std::vector<HoistCandidate> &candidates);
	};
}
