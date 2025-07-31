/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <queue>
#include <unordered_set>
#include <arc/foundation/pass.hpp>
#include <arc/foundation/module.hpp>
#include <arc/support/inference.hpp>

namespace arc
{
	/**
	 * @brief Constant folding optimization pass using worklist algorithm
	 *
	 * Evaluates constant expressions at compile time following Arc's type promotion
	 * rules and replaces them with computed literal values.
	 */
	class ConstantFoldingPass final : public TransformPass
	{
	public:
		/**
		 * @brief Get the pass name
		 * @return Pass identifier for dependency resolution
		 */
		std::string name() const override
		{
			return "constant-folding";
		}

		/**
		 * @brief Get analyses invalidated by this pass
		 * @return Empty vector - constant folding preserves analysis invariants
		 */
		std::vector<std::string> invalidates() const override
		{
			return {};
		}

		/**
		 * @brief Run constant folding on the module
		 * @param module Module to optimize
		 * @param pm Pass manager for accessing cached analyses
		 * @return Vector of regions that were modified
		 */
		std::vector<Region*> run(Module& module, PassManager& pm) override;

	private:
		std::queue<Node*> worklist;
		std::unordered_set<Node*> in_worklist;
		std::unordered_set<Region*> modified_regions;

		/**
		 * @brief Process all regions using worklist algorithm
		 * @param module Module to process
		 * @return Number of nodes folded
		 */
		std::size_t process_module(Module& module);

		/**
		 * @brief Collect potentially foldable nodes into worklist
		 * @param region Region to scan for foldable nodes
		 */
		void collect_nodes(Region* region);

		/**
		 * @brief Add node to worklist if not already present
		 * @param node Node to add
		 */
		void add_to_worklist(Node* node);

		/**
		 * @brief Add users of a node to worklist for re-evaluation
		 * @param node Node whose users should be re-evaluated
		 */
		void add_users(Node* node);

		/**
		 * @brief Process a single node from worklist
		 * @param node Node to attempt folding
		 * @return true if node was successfully folded
		 */
		bool process_node(Node* node);

		/**
		 * @brief Replace node with folded equivalent and update worklist
		 * @param original Original node to replace
		 * @param folded Folded replacement node
		 */
		void replace_node(Node* original, Node* folded);

		/**
		 * @brief Check if all inputs to a node are literals
		 * @param node Node to check
		 * @return true if all inputs are LIT nodes
		 */
		bool all_const(const Node* node) const;

		/**
		 * @brief Check if a node can be constant folded
		 * @param node Node to check
		 * @return true if node is eligible for folding
		 */
		bool is_foldable(const Node* node) const;

		/**
		 * @brief Create folded node for the given operation
		 * @param node Node to fold
		 * @return Folded node or nullptr if folding failed
		 */
		Node* create_folded(const Node* node) const;

		/**
		 * @brief Fold arithmetic operations with type promotion
		 * @param node Operation node (ADD, SUB, MUL, DIV, MOD)
		 * @return Folded literal or nullptr
		 */
		Node* fold_arith(const Node* node) const;

		/**
		 * @brief Fold comparison operations with type promotion
		 * @param node Comparison node (EQ, LT, GT, etc.)
		 * @return Boolean literal or nullptr
		 */
		Node* fold_cmp(const Node* node) const;

		/**
		 * @brief Fold bitwise operations
		 * @param node Bitwise operation node (BAND, BOR, BXOR, shifts)
		 * @return Folded literal or nullptr
		 */
		Node* fold_bitwise(const Node* node) const;

		/**
		 * @brief Fold unary operations
		 * @param node Unary operation node (BNOT)
		 * @return Folded literal or nullptr
		 */
		Node* fold_unary(const Node* node) const;

		/**
		 * @brief Fold FROM nodes (PHI-equivalent)
		 * @param node FROM node
		 * @return One of the inputs if all are identical literals, nullptr otherwise
		 */
		Node* fold_from(const Node* node) const;

		/**
		 * @brief Fold branch operations with constant conditions
		 * @param node BRANCH node
		 * @return JUMP node or nullptr
		 */
		Node* fold_branch(const Node* node) const;

		/**
		 * @brief Fold cast operations between compatible types
		 * @param node CAST node
		 * @return Folded literal or nullptr
		 */
		Node* fold_cast(const Node* node) const;

		/**
		 * @brief Check if two literals have the same value
		 * @param a First literal
		 * @param b Second literal
		 * @return true if literals represent the same value
		 */
		bool literals_equal(const Node* a, const Node* b) const;

		/**
		 * @brief Check for division by zero in arithmetic operations
		 * @param node Division or modulus node
		 * @return true if divisor is zero
		 */
		bool is_div_zero(const Node* node) const;
	};
}
