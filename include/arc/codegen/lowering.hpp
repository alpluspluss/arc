/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <unordered_map>
#include <vector>
#include <arc/foundation/pass.hpp>
#include <arc/foundation/typed-data.hpp>

namespace arc
{
	class Module;
	class PassManager;
	struct Node;
	class Region;

	/**
	 * @brief Lowering pass that converts high-level IR to target-independent lowered form
	 *
	 * This pass performs the following transformations:
	 * - ACCESS nodes → PTR_ADD operations with computed offsets
	 * - Complex CALL nodes → standardized calling sequences
	 * - High-level constructs → primitive operations suitable for instruction selection
	 */
	class IRLoweringPass final : public TransformPass
	{
	public:
		/**
		 * @brief Get the pass name
		 * @return Pass identifier for dependency resolution
		 */
		[[nodiscard]] std::string name() const override;

		/**
		 * @brief Get analyses invalidated by this pass
		 * @return Vector of analysis names that become stale after lowering
		 */
		[[nodiscard]] std::vector<std::string> invalidates() const override;

		/**
		 * @brief Run IR lowering on the module
		 * @param module Module to transform
		 * @param pm Pass manager for accessing cached analyses
		 * @return Vector of regions that were modified
		 */
		std::vector<Region *> run(Module &module, PassManager &pm) override;

	private:
		std::unordered_map<Node *, Node *> lowered_nodes;

		/**
		 * @brief Process all functions in the module
		 * @param module Module containing functions to process
		 * @return Number of nodes lowered
		 */
		std::size_t process_module(Module &module);

		/**
		 * @brief Process a single region
		 * @param region Region to process
		 * @return Number of nodes lowered in this region
		 */
		std::size_t process_region(Region *region);

		/**
		 * @brief Lower a single node if needed
		 * @param node Node to potentially lower
		 * @return Lowered node, or original node if no lowering needed
		 */
		static Node *lower_node(Node *node);

		/**
		 * @brief Lower ACCESS node to PTR_ADD operations
		 * @param access_node ACCESS node to lower
		 * @return Replacement node representing the computed address
		 */
		static Node *lower_access_node(Node *access_node);

		/**
		 * @brief Create a literal node with specified integer value and type
		 * @param value Integer value for the literal
		 * @param type Data type for the literal
		 * @return New literal node
		 */
		static Node* create_literal_node(std::int64_t value, DataType type);
	};

	/**
	 * @brief Extract integer value from a literal node
	 * @param node Literal node to extract from
	 * @return Integer value, or 0 if not a literal
	 */
	std::int64_t extract_literal_value(Node *node);
}
