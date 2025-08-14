/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <algorithm>
#include <sstream>
#include <arc/analysis/call-graph.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/region.hpp>
#include <arc/support/algorithm.hpp>
#include <arc/support/allocator.hpp>
#include <arc/transform/inliner.hpp>

namespace arc
{
	void Inliner::set_config(const Config &cfg)
	{
		config = cfg;
	}

	Inliner::Decision Inliner::evaluate(Node *call_site, Node *callee, const CallGraphResult *cg) const
	{
		Decision decision;

		/* validate basic preconditions for inlining */
		if (!call_site || !callee)
		{
			decision.reason = "null call site or callee";
			return decision;
		}

		/* only handle direct function calls and invokes; indirect calls through
		 * function pointers require additional complexity to resolve the target */
		if (call_site->ir_type != NodeType::CALL && call_site->ir_type != NodeType::INVOKE)
		{
			decision.reason = "not a call or invoke node";
			return decision;
		}

		/* ensure we're calling an actual function, not some other node type
		 * that might have been passed as the call target */
		if (callee->ir_type != NodeType::FUNCTION)
		{
			decision.reason = "callee is not a function";
			return decision;
		}

		/* check structural constraints - we only inline simple functions
		 * without control flow to avoid the complexity of merging multiple
		 * regions and handling control flow edges */
		if (!is_inlinable(callee, cg))
		{
			decision.reason = "function not suitable for inlining";
			return decision;
		}

		/* recursion detection prevents infinite inlining cycles; if call graph
		 * analysis is available, use it for precise detection, otherwise fall
		 * back to simple region hierarchy walking */
		if (!config.inline_recursive && would_create_recursion(call_site, callee, cg))
		{
			decision.reason = "would create recursion";
			return decision;
		}

		/* estimate code size increase from inlining; this prevents excessive
		 * code bloat while still allowing profitable small function inlining */
		decision.cost = estimate_cost(callee);
		if (decision.cost > config.max_size)
		{
			std::ostringstream oss;
			oss << "function too large (" << decision.cost << " > " << config.max_size << ")";
			decision.reason = oss.str();
			return decision;
		}

		/* calculate optimization benefits vs costs; call graph analysis enables
		 * more sophisticated heuristics based on function usage patterns */
		decision.benefit = calc_benefit(call_site, callee, cg);
		if (decision.benefit < config.min_benefit)
		{
			std::ostringstream oss;
			oss << "insufficient benefit (" << decision.benefit << " < " << config.min_benefit << ")";
			decision.reason = oss.str();
			return decision;
		}

		/* profit :money_mouth: */
		decision.should_inline = true;
		decision.reason = "profitable to inline";
		return decision;
	}

	Inliner::Result Inliner::inline_call(Node *call_site, Node *callee, Module &module,
	                                     const CallGraphResult *cg) const
	{
		Result result;

		/* re-verify inlining decision; this ensures consistency between evaluate()
		 * and inline_call() even if the IR changed between calls */
		Decision decision = evaluate(call_site, callee, cg);
		if (!decision.should_inline)
			return result;

		/* step 1: clone the callee function body into a temporary region
		 * this creates a copy of all nodes except structural ones (ENTRY, EXIT)
		 * that we can then modify without affecting the original function */
		std::unordered_map<Node *, Node *> node_mapping;
		Region *inlined_region = clone_function_body(callee, module, node_mapping);
		if (!inlined_region)
			return result;

		/* step 2: establish connections between cloned nodes
		 * the initial cloning creates nodes with the same properties but no
		 * connections; this phase rebuilds the use-def chains within the
		 * cloned subgraph to match the original function's structure */
		patch_connections(node_mapping);

		/* step 3: replace function parameters with call site arguments
		 * this is the key transformation that specializes the cloned function
		 * body for this specific call site by substituting the actual argument
		 * values for the formal parameters */
		substitute_parameters(inlined_region, call_site, node_mapping);

		/* step 4: find the return value from the inlined function
		 * simple functions have exactly one return statement; we extract the
		 * returned value to replace the call site in the caller */
		Node *return_value = find_return_value(inlined_region);
		if (return_value)
		{
			/* replace all uses of the call site with the return value
			 * this effectively "returns" the inlined function's result to
			 * all places that were using the original call */
			update_all_connections(call_site, return_value);
		}

		/* step 5: integrate inlined nodes into caller's region
		 * move all meaningful nodes from the temporary inlined region into
		 * the caller's region, placing them just before the original call site
		 * to maintain execution order */
		Region *caller_region = call_site->parent;
		for (Node *node: inlined_region->nodes())
		{
			/* only move actual computation nodes; skip structural nodes that
			 * don't represent actual operations in the inlined code */
			if (node->ir_type != NodeType::ENTRY &&
			    node->ir_type != NodeType::EXIT &&
			    node->ir_type != NodeType::PARAM &&
			    node->ir_type != NodeType::RET)
			{
				caller_region->insert_before(call_site, node);
			}
		}

		/* step 6: remove the original call site
		 * now that the call has been replaced with the inlined function body,
		 * the original call is no longer needed */
		caller_region->remove(call_site);

		/* record what was modified for pass manager invalidation */
		result.return_value = return_value;
		result.modified.push_back(caller_region);
		result.success = true;
		return result;
	}

	std::size_t Inliner::estimate_cost(Node *func)
	{
		/* find the function's implementation region; functions in Arc are
		 * implemented as regions containing the function body */
		Region *func_region = find_function_region(func, const_cast<Module &>(func->parent->module()));
		if (!func_region)
			return 1000; /* unknown function is considered very expensive */

		/* count meaningful operations that would be inlined
		 * structural nodes (ENTRY, EXIT, PARAM) don't represent actual
		 * computation so they don't contribute to code size */
		std::size_t cost = 0;
		for (Node *node: func_region->nodes())
		{
			if (node->ir_type != NodeType::ENTRY &&
			    node->ir_type != NodeType::EXIT &&
			    node->ir_type != NodeType::PARAM)
			{
				cost++;
			}
		}

		return cost;
	}

	float Inliner::calc_benefit(Node *call_site, Node *callee, const CallGraphResult *cg)
	{
		/* start with base benefit for eliminating function call overhead
		 * this includes register save/restore, parameter passing, and
		 * potential cache misses from jumping to a different code location */
		float benefit = 2.0f;

		/* constant arguments are highly valuable because they enable constant
		 * propagation (SCCP) in the inlined function body, potentially
		 * eliminating entire branches and exposing more optimization opportunities */
		if (has_constant_args(call_site))
			benefit += 5.0f;

		/* size-based heuristics: small functions are almost always profitable
		 * to inline because the call overhead is significant relative to the
		 * function body, and larger functions have diminishing returns */
		std::size_t cost = estimate_cost(callee);
		if (cost <= 5)
			benefit += 3.0f; /* very small functions are excellent candidates */

		if (cost > 15)
			benefit = std::max(benefit - 2.0f, 1.0f); /* penalize large functions */

		/* call graph analysis enables more sophisticated heuristics based on
		 * global program structure and function usage patterns */
		if (cg)
		{
			/* functions with few callers are good candidates because:
			 * 1. less code size explosion (fewer copies created)
			 * 2. function might become dead after inlining all call sites */
			auto callers = cg->callers(callee);
			if (callers.size() == 1)
				benefit += 3.0f; /* only caller - function will become dead */
			else if (callers.size() > 10)
				benefit -= 2.0f; /* widely used - risk of code size explosion */

			/* pure functions (no side effects) are excellent inlining candidates
			 * because they enable more aggressive optimization like common
			 * subexpression elimination and dead code elimination */
			if (cg->pure(callee))
				benefit += 2.0f;
		}

		return benefit;
	}

	bool Inliner::is_inlinable(Node *callee, const CallGraphResult *cg)
	{
		/* find the function's implementation region */
		Region *func_region = find_function_region(callee, const_cast<Module &>(callee->parent->module()));
		if (!func_region)
			return false;

		/* restriction: only inline functions without control flow
		 * functions with child regions contain branches, loops, or other
		 * control flow constructs that would require complex region merging
		 * logic to inline properly */
		if (!func_region->children().empty())
			return false;

		/* restriction: only inline functions with exactly one return
		 * multiple returns would require complex control flow merging and
		 * phi node insertion at the call site to handle the different
		 * possible return values */
		std::size_t return_count = 0;
		for (Node *node: func_region->nodes())
		{
			if (node->ir_type == NodeType::RET)
				return_count++;
		}

		if (return_count != 1)
			return false;

		/* call graph analysis enables additional safety checks */
		if (cg)
		{
			/* functions with escaping parameters are harder to optimize after
			 * inlining because the parameter values might be modified through
			 * aliases, reducing the benefits of constant propagation */
			for (std::size_t i = 0; i < callee->inputs.size(); ++i)
			{
				if (cg->escapes(callee, i))
					return false;
			}
		}

		return true;
	}

	Region *Inliner::clone_function_body(Node *callee, Module &target_module,
	                                     std::unordered_map<Node *, Node *> &node_mapping)
	{
		/* locate the source region containing the function implementation */
		Region *source_region = find_function_region(callee, target_module);
		if (!source_region)
			return nullptr;

		/* create a temporary region to hold the cloned function body
		 * this region will later be dissolved when we move its nodes into
		 * the caller's region */
		std::string inline_name = std::string(target_module.strtable().get(callee->str_id)) + "_inlined";
		Region *target_region = target_module.create_region(inline_name);

		/* clone all meaningful nodes from the source function
		 * we skip structural nodes (ENTRY, EXIT) because they represent
		 * function boundaries that don't make sense in the inlined context */
		for (Node *original: source_region->nodes())
		{
			if (original->ir_type == NodeType::ENTRY ||
			    original->ir_type == NodeType::EXIT)
			{
				continue; /* skip function boundary markers */
			}

			/* create a copy of this node with the same properties but no connections */
			Node *cloned = clone_node(original);
			if (cloned)
			{
				target_region->append(cloned);
				node_mapping[original] = cloned; /* record mapping for connection patching */
			}
		}

		return target_region;
	}

	Node *Inliner::clone_node(Node *original)
	{
		if (!original)
			return nullptr;

		/* allocate a new node using shared allocation for consistency with
		 * other IR node allocation throughout the system */
		ach::shared_allocator<Node> alloc;
		Node *cloned = alloc.allocate(1);
		std::construct_at(cloned);

		/* copy all intrinsic properties of the node
		 * note: we deliberately don't copy inputs/users as these will be
		 * patched up later based on the cloned subgraph structure */
		cloned->ir_type = original->ir_type;     /* operation type */
		cloned->type_kind = original->type_kind; /* data type */
		cloned->value = original->value;         /* constant data or type info */
		cloned->traits = original->traits;       /* optimization hints/constraints */
		cloned->str_id = original->str_id;       /* string table reference */
		return cloned;
	}

	void Inliner::patch_connections(const std::unordered_map<Node *, Node *> &node_mapping)
	{
		/* rebuild use-def chains within the cloned subgraph
		 * this phase establishes the same connectivity pattern as the original
		 * function, but using the cloned nodes instead of the originals */
		for (const auto &[original, cloned]: node_mapping)
		{
			/* start with empty input lists but preserve user lists
			 *
			 * we clear inputs because they will be rebuilt from scratch based on
			 * the cloned subgraph. however, we do NOT clear user lists because
			 * they accumulate as we process other nodes--when node A connects to
			 * node B, A gets added to B's user list. clearing B's user list later
			 * would break the bidirectional connection that A already established */
			cloned->inputs.clear();

			/* rebuild input connections by mapping original inputs to cloned inputs */
			for (Node *original_input: original->inputs)
			{
				/* only connect to inputs that are also part of the cloned subgraph
				 * inputs from outside the function, e.g. parameters, will be handled
				 * during parameter substitution */
				if (auto it = node_mapping.find(original_input);
					it != node_mapping.end())
				{
					Node *cloned_input = it->second;
					cloned->inputs.push_back(cloned_input);
					cloned_input->users.push_back(cloned); /* maintain bidirectional links */
				}
			}
		}
	}

	void Inliner::substitute_parameters(Region *inlined_region, Node *call_site,
	                                    const std::unordered_map<Node *, Node *> & /* node_mapping */)
	{
		/* collect all parameter nodes from the cloned function body
		 * parameters represent the formal arguments that need to be replaced
		 * with the actual arguments from the call site */
		std::vector<Node *> param_nodes;
		for (Node *node: inlined_region->nodes())
		{
			if (node->ir_type == NodeType::PARAM)
			{
				param_nodes.push_back(node);
			}
		}

		/* determine how many arguments the call site provides
		 * we exclude the function operand which is input[0] and any control flow
		 * targets for INVOKE nodes (which have exception handling edges) */
		std::size_t arg_count = call_site->inputs.size() - 1; /* exclude function operand */
		if (call_site->ir_type == NodeType::INVOKE)
			arg_count -= 2; /* exclude normal and exception targets */

		/* replace each parameter with its corresponding argument
		 * this specializes the cloned function body for this specific call site
		 * by substituting actual values for formal parameters */
		for (std::size_t i = 0; i < std::min(param_nodes.size(), arg_count); ++i)
		{
			Node *param = param_nodes[i];
			Node *arg = call_site->inputs[i + 1]; /* +1 to skip function operand */
			if (param && arg)
			{
				/* replace all uses of the parameter with the argument
				 * this updates use-def chains to connect the inlined code
				 * directly to the caller's argument values then remove
				 * the now-unused parameter node from the inlined region */
				update_all_connections(param, arg);
				inlined_region->remove(param);
			}
		}
	}

	Node *Inliner::find_return_value(Region *inlined_region)
	{
		if (!inlined_region)
			return nullptr;

		/* locate the single return statement in the inlined function
		 * simple functions have exactly one return, so we find it and
		 * extract the returned value to replace the call site */
		for (Node *node: inlined_region->nodes())
		{
			if (node->ir_type == NodeType::RET && !node->inputs.empty())
				return node->inputs[0]; /* return value is the first input */
		}

		return nullptr; /* void function or malformed return */
	}

	Region *Inliner::find_function_region(Node *func, Module &module)
	{
		if (!func || func->ir_type != NodeType::FUNCTION)
			return nullptr;

		/* in Arc's IR structure, functions are implemented as child regions
		 * of the module root, with the region name matching the function name */
		std::string_view func_name = module.strtable().get(func->str_id);
		for (Region *child: module.root()->children())
		{
			if (child->name() == func_name)
				return child;
		}
		return nullptr; /* function declaration without implementation */
	}

	bool Inliner::has_constant_args(Node *call_site)
	{
		if (call_site->inputs.size() <= 1)
			return false; /* no arguments to check */

		/* scan through the call site's arguments looking for literal constants
		 * constant arguments are valuable because they enable constant propagation
		 * in the inlined function body */
		std::size_t arg_start = 1; /* skip function operand */
		std::size_t arg_end = call_site->inputs.size();
		if (call_site->ir_type == NodeType::INVOKE)
			arg_end -= 2; /* exclude control flow targets for invoke */

		for (std::size_t i = arg_start; i < arg_end; ++i)
		{
			if (call_site->inputs[i] && call_site->inputs[i]->ir_type == NodeType::LIT)
				return true; /* found at least one constant argument */
		}

		return false;
	}

	bool Inliner::would_create_recursion(Node *call_site, Node *callee, const CallGraphResult *cg)
	{
		if (cg)
		{
			/* use call graph analysis for precise recursion detection
			 * this can detect both direct recursion (f calls f) and indirect
			 * recursion (f calls g calls f) through strongly connected components */
			return cg->recursive(callee);
		}

		/* fallback to simple region hierarchy walking when call graph isn't available
		 * this only detects direct recursion by checking if we're already inside
		 * the callee function based on the region naming structure */
		Region *current = call_site->parent;
		std::string_view callee_name = current->module().strtable().get(callee->str_id);
		while (current)
		{
			if (current->name() == callee_name)
				return true; /* found the callee function in our call stack */
			current = current->parent();
		}

		return false; /* no recursion detected */
	}
}
