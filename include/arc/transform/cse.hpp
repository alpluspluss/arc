/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <arc/analysis/tbaa.hpp>
#include <arc/foundation/pass.hpp>

namespace arc
{
	class Module;
	class PassManager;
	struct Node;
	class Region;

	using ValueNumber = std::uint64_t;

	/**
	 * @brief Common Subexpression Elimination pass using value numbering with TBAA integration
	 *
	 * Eliminates redundant computations by identifying expressions that compute the same value
	 * and reusing previously computed results. Uses hash-based value numbering combined with
	 * type-based alias analysis to safely eliminate redundant memory operations.
	 */
	class CommonSubexpressionEliminationPass final : public TransformPass
	{
	public:
		/**
		 * @brief Get the pass name
		 * @return Pass identifier for dependency resolution
		 */
		[[nodiscard]] std::string name() const override;

		/**
		 * @brief Get required analysis passes
		 * @return Vector of analysis pass names needed by CSE
		 */
		[[nodiscard]] std::vector<std::string> require() const override;

		/**
		 * @brief Get analyses invalidated by this pass
		 * @return Vector of analysis names that become stale after CSE
		 */
		[[nodiscard]] std::vector<std::string> invalidates() const override;

		/**
		 * @brief Run CSE optimization on the module
		 * @param module Module to optimize
		 * @param pm Pass manager for accessing cached analyses
		 * @return Vector of regions that were modified
		 */
		std::vector<Region *> run(Module &module, PassManager &pm) override;

	private:
		std::unordered_map<Node *, ValueNumber> value_numbers;
		std::unordered_map<ValueNumber, Node *> expression_to_node;
		ValueNumber next_value_number = 1;

		/**
		 * @brief Process all functions in the module
		 * @param module Module containing functions to process
		 * @param tbaa_result TBAA analysis result for alias queries
		 * @return Number of expressions eliminated
		 */
		std::size_t process_module(Module &module, const TypeBasedAliasResult &tbaa_result);

		/**
		 * @brief Process a single region using worklist algorithm
		 * @param region Region to process
		 * @param tbaa_result TBAA analysis result for alias queries
		 * @return Number of expressions eliminated in this region
		 */
		std::size_t process_region(Region *region, const TypeBasedAliasResult &tbaa_result);

		/**
		 * @brief Compute value number for a node
		 * @param node Node to compute value number for
		 * @param tbaa_result TBAA analysis result for memory operations
		 * @return Value number, or 0 if computation failed
		 */
		ValueNumber compute_value_number(Node *node, const TypeBasedAliasResult &tbaa_result);

		/**
		 * @brief Compute value number for literal nodes
		 * @param node Literal node to hash
		 * @return Value number based on literal value and type
		 */
		ValueNumber compute_literal_value_number(Node *node);

		/**
		 * @brief Compute value number for expression nodes
		 * @param node Expression node to hash
		 * @return Value number based on operation and operand value numbers
		 */
		ValueNumber compute_expression_value_number(Node *node);

		/**
		 * @brief Compute value number for memory load operations
		 * @param node Load operation node
		 * @param tbaa_result TBAA analysis for location information
		 * @return Value number incorporating memory location
		 */
		ValueNumber compute_load_value_number(Node *node, const TypeBasedAliasResult &tbaa_result);

		/**
		 * @brief Compute value number for vector literal nodes
		 * @param node Vector literal node
		 * @return Value number based on vector type and elements
		 */
		ValueNumber compute_vector_literal_value_number(Node *node);

		/**
		 * @brief Check if a node represents a memory load operation
		 * @param node Node to check
		 * @return true if node is a load operation
		 */
		[[nodiscard]] static bool is_load_operation(const Node *node);

		/**
		 * @brief Check if a node is eligible for CSE optimization
		 * @param node Node to check
		 * @return true if node can be safely eliminated
		 */
		[[nodiscard]] static bool is_eligible_for_cse(const Node *node);

		/**
		 * @brief Check if an operation type is commutative
		 * @param type Operation type to check
		 * @return true if operands can be reordered
		 */
		[[nodiscard]] static bool is_commutative(NodeType type);

		/**
		 * @brief Check if two load operations may alias
		 * @param a First load operation
		 * @param b Second load operation
		 * @param tbaa_result TBAA analysis for alias queries
		 * @return true if loads may access the same memory
		 */
		[[nodiscard]] static bool loads_may_alias(Node *a, Node *b, const TypeBasedAliasResult &tbaa_result);

		/**
		 * @brief Replace all uses of a node with another node
		 * @param node_to_replace Node whose uses should be replaced
		 * @param replacement_node Node to use as replacement
		 * @return true if replacement was successful
		 */
		static bool replace_all_uses(Node *node_to_replace, Node *replacement_node);

		/**
		 * @brief Hash combiner for building composite hash values
		 * @param seed Current hash value
		 * @param value Value to combine into hash
		 * @return Combined hash value
		 */
		template<typename T>
		static ValueNumber hash_combine(ValueNumber seed, const T &value)
		{
			return seed ^ (std::hash<T> {}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2));
		}
	};
}
