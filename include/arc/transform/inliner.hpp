/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <arc/foundation/node.hpp>

namespace arc
{
	class Module;
	class Region;
	class CallGraphResult;

	/**
	 * @brief Component for inlining function calls within modules
	 *
	 * The Inliner performs function call inlining on simple functions without
	 * control flow (single basic block, single return). It uses configurable
	 * heuristics to balance code size growth against optimization opportunities,
	 * with special consideration for constant arguments that enable further
	 * optimization passes like SCCP. Integration with call graph analysis
	 * enables more sophisticated heuristics for recursion detection and
	 * benefit calculation.
	 */
	class Inliner
	{
	public:
		/**
		 * @brief Configuration parameters for inlining heuristics
		 */
		struct Config
		{
			std::size_t max_size = 30;     /* maximum function size to inline */
			float min_benefit = 2.0f;      /* minimum benefit score required */
			bool inline_recursive = false; /* whether to inline recursive calls */
		};

		/**
		 * @brief Inlining decision result with reasoning
		 */
		struct Decision
		{
			bool should_inline = false; /* whether inlining is beneficial */
			float benefit = 0.0f;       /* computed benefit score */
			std::size_t cost = 0;       /* estimated code size increase */
			std::string reason;         /* explanation for debugging */
		};

		/**
		 * @brief Result of inlining operation
		 */
		struct Result
		{
			Node *return_value = nullptr;   /* value that replaced the call */
			std::vector<Region *> modified; /* regions affected by inlining */
			bool success = false;           /* whether inlining succeeded */
		};

		/**
		 * @brief Set configuration parameters for inlining heuristics
		 * @param cfg Configuration to use
		 */
		void set_config(const Config &cfg);

		/**
		 * @brief Evaluate whether a call site should be inlined
		 *
		 * Analyzes the call site and callee function to determine if inlining
		 * would be beneficial. This method has no side effects and can be used
		 * to query inlining decisions without performing the transformation.
		 * Call graph analysis enables enhanced heuristics for recursion
		 * detection and benefit calculation.
		 *
		 * @param call_site Call or invoke node to potentially inline
		 * @param callee Function being called
		 * @param cg Call graph analysis for enhanced heuristics (optional)
		 * @return Decision with reasoning about whether to inline
		 */
		Decision evaluate(Node *call_site, Node *callee, const CallGraphResult *cg = nullptr) const;

		/**
		 * @brief Perform function inlining at the specified call site
		 *
		 * Clones the callee function body, substitutes parameters with arguments,
		 * and replaces the call site with the inlined code. Only works with
		 * simple functions (no control flow, single return). Dead function
		 * cleanup is left to subsequent DCE passes.
		 *
		 * @param call_site Call or invoke node to inline
		 * @param callee Function to inline
		 * @param module Module containing both caller and callee
		 * @param cg Call graph analysis for enhanced heuristics (optional)
		 * @return Result indicating success and what was modified
		 */
		Result inline_call(Node *call_site, Node *callee, Module &module,
		                   const CallGraphResult *cg = nullptr) const;

	private:
		Config config;

		/**
		 * @brief Estimate the code size cost of inlining a function
		 * @param func Function to analyze
		 * @return Estimated number of operations that would be inlined
		 */
		static std::size_t estimate_cost(Node *func);

		/**
		 * @brief Calculate the benefit score for inlining a call site
		 * @param call_site Call node being analyzed
		 * @param callee Function being called
		 * @param cg Call graph analysis for enhanced scoring (optional)
		 * @return Benefit score (higher is better)
		 */
		static float calc_benefit(Node *call_site, Node *callee, const CallGraphResult *cg);

		/**
		 * @brief Check if a function is suitable for inlining
		 * @param callee Function to check
		 * @param cg Call graph analysis for enhanced checks (optional)
		 * @return true if function can be safely inlined
		 */
		static bool is_inlinable(Node *callee, const CallGraphResult *cg);

		/**
		 * @brief Clone a function's body for inlining
		 * @param callee Function whose body to clone
		 * @param target_module Module to create cloned nodes in
		 * @param node_mapping Output mapping from original to cloned nodes
		 * @return Region containing cloned function body
		 */
		static Region *clone_function_body(Node *callee, Module &target_module,
		                                   std::unordered_map<Node *, Node *> &node_mapping);

		/**
		 * @brief Clone a single node for inlining
		 * @param original Node to clone
		 * @return Cloned node with same properties as original
		 */
		static Node *clone_node(Node *original);

		/**
		 * @brief Establish connections between cloned nodes
		 * @param node_mapping Mapping from original to cloned nodes
		 */
		static void patch_connections(const std::unordered_map<Node *, Node *> &node_mapping);

		/**
		 * @brief Replace function parameters with call arguments
		 * @param inlined_region Region containing inlined function body
		 * @param call_site Call node providing arguments
		 * @param node_mapping Mapping from original to cloned nodes
		 */
		static void substitute_parameters(Region *inlined_region, Node *call_site,
		                                  const std::unordered_map<Node *, Node *> &node_mapping);

		/**
		 * @brief Find the return value in an inlined function body
		 * @param inlined_region Region containing inlined code
		 * @return Node representing the return value, or nullptr if not found
		 */
		static Node *find_return_value(Region *inlined_region);

		/**
		 * @brief Find the region containing a function's implementation
		 * @param func Function node to find region for
		 * @param module Module containing the function
		 * @return Region containing function body, or nullptr if not found
		 */
		static Region *find_function_region(Node *func, Module &module);

		/**
		 * @brief Check if a call site has constant arguments
		 * @param call_site Call node to analyze
		 * @return true if any arguments are compile-time constants
		 */
		static bool has_constant_args(Node *call_site);

		/**
		 * @brief Check if inlining would create a recursive call
		 * @param call_site Call node being analyzed
		 * @param callee Function being called
		 * @param cg Call graph analysis for precise detection (optional)
		 * @return true if inlining would result in recursion
		 */
		static bool would_create_recursion(Node *call_site, Node *callee, const CallGraphResult *cg);
	};
}
