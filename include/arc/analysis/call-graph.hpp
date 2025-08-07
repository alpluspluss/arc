/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <arc/foundation/pass.hpp>

namespace arc
{
	class Module;
	struct Node;
	class Region;

	/**
	 * @brief Represents a call relationship edge in the program's call graph
	 */
	struct CallEdge
	{
		/** @brief Function node that makes the call */
		Node *caller;
		/** @brief CALL or INVOKE node representing the call site */
		Node *call_site;
		/** @brief Function being called; nullptr for unresolved indirect calls */
		Node *callee;
		/** @brief true if call was resolved via function pointer chasing */
		bool indirect;
		/** @brief confidence level for indirect call resolution (0.0-1.0) */
		float confidence;
	};

	/**
	 * @brief Parameter flow and escapement information
	 */
	struct ParamInfo
	{
		/** @brief true if parameter escapes the function scope */
		bool escapes;
		/** @brief true if parameter is never modified within the function */
		bool read_only;
		/** @brief nodes that cause the parameter to escape */
		std::vector<Node *> escape_sites;
	};

	/**
	 * @brief Hash function for parameter info map keys
	 */
	struct ParamKeyHash
	{
		std::size_t operator()(const std::pair<Node *, std::size_t> &key) const
		{
			std::size_t h1 = std::hash<Node *> {}(key.first);
			std::size_t h2 = std::hash<std::size_t> {}(key.second);
			return h1 ^ (h2 << 1);
		}
	};

	/**
	 * @brief Call graph analysis result providing interprocedural information
	 *
	 * Maintains a complete picture of function call relationships within the module,
	 * including both direct calls and indirect calls resolved through function pointer
	 * analysis. The analysis respects Arc's function traits and uses conservative
	 * assumptions for module boundaries while providing precise information for
	 * internal functions.
	 */
	class CallGraphResult final : public Analysis
	{
	public:
		[[nodiscard]] std::string name() const override
		{
			return "call-graph-analysis";
		}

		/**
		 * @brief Update analysis results incrementally for modified regions
		 * @param modified_regions regions that were changed by optimization passes
		 * @return true if incremental update succeeded, false if full recomputation needed
		 */
		bool update(const std::vector<Region *> &modified_regions) override;

		/**
		 * @brief Get direct callee for a call site
		 * @param call_site CALL or INVOKE node
		 * @return direct callee function node, or nullptr for indirect calls
		 */
		Node *callee(Node *call_site) const;

		/**
		 * @brief Get all possible callees for a call site
		 * @param call_site CALL or INVOKE node
		 * @return all functions that might be called at this site
		 */
		std::vector<Node *> targets(Node *call_site) const;

		/**
		 * @brief Check if one function can transitively call another
		 * @param caller function that might make the call
		 * @param callee function that might be called
		 * @return true if caller can reach callee through call graph
		 */
		bool calls(Node *caller, Node *callee) const;

		/**
		 * @brief Get all functions directly called by a function
		 * @param func function to query
		 * @return functions called by this function
		 */
		std::vector<Node *> callees(Node *func) const;

		/**
		 * @brief Get all functions that directly call a function
		 * @param func function to query
		 * @return functions that call this function
		 */
		std::vector<Node *> callers(Node *func) const;

		/**
		 * @brief Check if function is recursive
		 * @param func function to check
		 * @return true if function can call itself directly or indirectly
		 */
		bool recursive(Node *func) const;

		/**
		 * @brief Check if a function parameter escapes
		 * @param func function containing the parameter
		 * @param param_idx zero-based parameter index
		 * @return true if parameter escapes the function
		 */
		bool escapes(Node *func, std::size_t param_idx) const;

		/**
		 * @brief Check if function has no observable side effects
		 * @param func function to check
		 * @return true if function is pure
		 */
		bool pure(Node *func) const;

		/**
		 * @brief Get all call sites within a function
		 * @param func function to query
		 * @return CALL and INVOKE nodes within the function
		 */
		std::vector<Node *> call_sites(Node *func) const;

		/**
		 * @brief Get the function containing a call site
		 * @param call_site CALL or INVOKE node
		 * @return function containing this call site
		 */
		Node *containing_fn(Node *call_site) const;

	private:
		std::vector<CallEdge> call_edges;
		std::unordered_map<Node *, std::vector<Node *> > caller_map;
		std::unordered_map<Node *, std::vector<Node *> > callee_map;
		std::unordered_map<Node *, std::vector<Node *> > scc_map;
		std::unordered_map<std::pair<Node *, std::size_t>, ParamInfo, ParamKeyHash> param_info;
		std::unordered_set<Node *> pure_functions;
		std::unordered_set<Node *> extern_functions;
		std::unordered_set<Node *> export_functions;
		std::unordered_map<Node *, Node *> call_site_to_function;
		std::unordered_map<Node *, std::vector<Node *> > function_call_sites;

		friend class CallGraphAnalysisPass;
	};

	/**
	 * @brief Call graph analysis pass
	 */
	class CallGraphAnalysisPass final : public AnalysisPass
	{
	public:
		/**
		 * @brief Get the pass name
		 * @return Pass identifier for dependency resolution
		 */
		[[nodiscard]] std::string name() const override;

		/**
		 * @brief Get required analysis passes
		 * @return Vector of analysis pass names needed by call graph analysis
		 */
		[[nodiscard]] std::vector<std::string> require() const override;

		/**
		 * @brief Run call graph analysis on the module
		 * @param module Module to analyze
		 * @return Call graph analysis result
		 */
		Analysis *run(const Module &module) override;

	private:
		void analyze_module(CallGraphResult *result, Module &module);

		static void classify_functions(CallGraphResult *result, Module &module);

		void analyze_function(CallGraphResult *result, Node *func, Module &module);

		void analyze_call_site(CallGraphResult *result, Node *call_node, Node *containing_func, Module& module);

		static void analyze_parameter_flow(CallGraphResult *result, Module &module);

		static void compute_function_purity(CallGraphResult *result, Module &module);

		static void compute_scc(CallGraphResult *result);

		std::vector<Node *> chase_function_pointer(Node *pointer_node, std::unordered_set<Node *> &visited, Module& module);

		void chase_pointer_def(Node *node, std::unordered_set<Node *> &functions, std::unordered_set<Node *> &visited, Module& module);

		void chase_memory_location(Node *load_node, std::unordered_set<Node *> &functions,
		                           std::unordered_set<Node *> &visited, Module& module);

		void find_stores_to_location(Node *location, std::unordered_set<Node *> &functions,
		                             std::unordered_set<Node *> &visited, Module& module);

		static bool parameter_escapes_analysis(Node *param);

		static bool escapes_via_return_only(Node *param);

		static bool analyze_function_purity(Node *func, const CallGraphResult &cg, Module& m);

		static bool is_assignment_context(Node *user);

		static Region *find_function_region(Node *func, Module &module);

		static Node *find_function_for_region(Region *region, Module &module);

		static std::size_t find_parameter_index(Node *func, Node *param);

		static void record_direct_call(CallGraphResult *result, Node *call_site, Node *caller, Node *callee);

		static void record_indirect_call(CallGraphResult *result, Node *call_site, Node *caller, Node *callee);
	};
}
