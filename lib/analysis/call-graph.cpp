/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <algorithm>
#include <queue>
#include <stack>
#include <arc/analysis/call-graph.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/region.hpp>
#include <arc/support/inference.hpp>

namespace arc
{
	bool CallGraphResult::update(const std::vector<Region *> &modified_regions)
	{
		/* call graph analysis depends on the structure of function calls and parameter
		 * flow which rarely changes during optimization passes. most transforms like
		 * CSE, DCE, or SROA don't modify call relationships, so we can usually just
		 * return true to indicate the cached result is still valid.
		 *
		 * if a pass actually inlines functions or changes call sites, it should
		 * explicitly invalidate this analysis rather than relying on incremental updates */
		return true;
	}

	Node *CallGraphResult::callee(Node *call_site) const
	{
		if (!call_site || (call_site->ir_type != NodeType::CALL && call_site->ir_type != NodeType::INVOKE))
			return nullptr;

		if (call_site->inputs.empty())
			return nullptr;

		/* operand 0 is always the function being called according to Arc's operand conventions.
		 * for direct calls this will be a FUNCTION node, for indirect calls it will be
		 * some other node type that we need to resolve through pointer analysis */
		Node *target = call_site->inputs[0];
		return (target && target->ir_type == NodeType::FUNCTION) ? target : nullptr;
	}

	std::vector<Node *> CallGraphResult::targets(Node *call_site) const
	{
		if (!call_site)
			return {};

		/* look for cached indirect call resolution results first. the call graph
		 * analysis pre-computes all possible targets for indirect calls during
		 * the analysis phase to avoid repeated pointer chasing */
		for (const CallEdge &edge: call_edges)
		{
			if (edge.call_site == call_site)
			{
				if (edge.callee)
				{
					/* found a resolved call edge; we collect all callees for this site.
					 * there might be multiple edges for the same call site if it's
					 * an indirect call with multiple possible targets */
					std::vector<Node *> result;
					for (const CallEdge &other_edge: call_edges)
					{
						if (other_edge.call_site == call_site && other_edge.callee)
							result.push_back(other_edge.callee);
					}
					return result;
				}
			}
		}

		/* if no cached results found, try direct resolution. this handles the
		 * simple case where we have a direct call to a known function */
		if (Node *direct_callee = callee(call_site))
			return { direct_callee };

		return {};
	}

	bool CallGraphResult::calls(Node *caller, Node *callee) const
	{
		if (!caller || !callee)
			return false;

		if (caller == callee)
			return recursive(caller);

		/* use BFS to check transitive reachability through the call graph.
		 * this is more efficient than DFS for this query pattern since most
		 * call relationships are relatively shallow */
		std::unordered_set<Node *> visited;
		std::queue<Node *> worklist;
		worklist.push(caller);

		while (!worklist.empty())
		{
			Node *current = worklist.front();
			worklist.pop();

			if (visited.contains(current))
				continue;
			visited.insert(current);

			if (auto it = callee_map.find(current);
				it != callee_map.end())
			{
				for (Node *target: it->second)
				{
					if (target == callee)
						return true;
					if (!visited.contains(target))
						worklist.push(target);
				}
			}
		}

		return false;
	}

	std::vector<Node *> CallGraphResult::callees(Node *func) const
	{
		if (auto it = callee_map.find(func);
			it != callee_map.end())
			return it->second;
		return {};
	}

	std::vector<Node *> CallGraphResult::callers(Node *func) const
	{
		if (auto it = caller_map.find(func);
			it != caller_map.end())
			return it->second;
		return {};
	}

	bool CallGraphResult::recursive(Node *func) const
	{
		if (auto it = scc_map.find(func);
			it != scc_map.end())
		{
			/* a function is recursive if its strongly connected component
			 * contains more than just itself, or if it directly calls itself */
			return it->second.size() > 1 ||
			       std::find(it->second.begin(), it->second.end(), func) != it->second.end();
		}
		return false;
	}

	bool CallGraphResult::escapes(Node *func, std::size_t param_idx) const
	{
		/* extern functions are analyzed conservatively.
		 *
		 * we would assume all parameters might escape since
		 * we don't know what the external implementation does */
		if (extern_functions.contains(func))
			return true;

		if (const auto it = param_info.find({ func, param_idx });
			it != param_info.end())
		{
			return it->second.escapes;
		}

		/* if no parameter info is recorded, assume the parameter escapes to be safe.
		 * this can happen for malformed IR or functions that weren't fully analyzed */
		return true;
	}

	bool CallGraphResult::pure(Node *func) const
	{
		return pure_functions.contains(func);
	}

	std::vector<Node *> CallGraphResult::call_sites(Node *func) const
	{
		if (auto it = function_call_sites.find(func);
			it != function_call_sites.end())
			return it->second;
		return {};
	}

	Node *CallGraphResult::containing_fn(Node *call_site) const
	{
		if (auto it = call_site_to_function.find(call_site);
			it != call_site_to_function.end())
			return it->second;
		return nullptr;
	}

	std::string CallGraphAnalysisPass::name() const
	{
		return "call-graph-analysis";
	}

	std::vector<std::string> CallGraphAnalysisPass::require() const
	{
		return {};
	}

	Analysis *CallGraphAnalysisPass::run(const Module &module)
	{
		auto *result = allocate_result<CallGraphResult>();
		analyze_module(result, const_cast<Module &>(module));
		return result;
	}

	void CallGraphAnalysisPass::analyze_module(CallGraphResult *result, Module &module)
	{
		/* single-pass analysis that builds the complete call graph by walking through
		 * all functions and analyzing their call sites, parameter flow, and side effects.
		 * the order of analysis phases matters: we need to identify all call relationships
		 * before we can compute things like SCCs and purity */
		classify_functions(result, module);

		for (Node *func: module.functions())
		{
			if (func->ir_type == NodeType::FUNCTION)
				analyze_function(result, func, module);
		}

		compute_scc(result);
		analyze_parameter_flow(result, module);
		compute_function_purity(result, module);
	}

	void CallGraphAnalysisPass::classify_functions(CallGraphResult *result, Module &module)
	{
		/* classify functions based on their traits to enable different analysis strategies.
		 * extern functions get conservative treatment since we don't know their implementation,
		 * while export functions might be called externally so we need to be more careful
		 * about assumptions like purity */
		for (Node *func: module.functions())
		{
			if (func->ir_type != NodeType::FUNCTION)
				continue;

			if ((func->traits & NodeTraits::EXTERN) != NodeTraits::NONE)
				result->extern_functions.insert(func);

			if ((func->traits & NodeTraits::EXPORT) != NodeTraits::NONE)
				result->export_functions.insert(func);
		}
	}

	void CallGraphAnalysisPass::analyze_function(CallGraphResult *result, Node *func, Module &module)
	{
		Region *func_region = find_function_region(func, module);
		if (!func_region)
			return;

		/* walk through all regions dominated by this function to find call sites.
		 * Arc's region hierarchy makes this traversal straightforward since each
		 * function has a clear region boundary */
		func_region->walk_dominated_regions([&](Region *region)
		{
			for (Node *node: region->nodes())
			{
				if (node->ir_type == NodeType::CALL || node->ir_type == NodeType::INVOKE)
				{
					analyze_call_site(result, node, func, module);

					/* track which function contains each call site for reverse lookups */
					result->call_site_to_function[node] = func;
					result->function_call_sites[func].push_back(node);
				}
			}
		});
	}

	void CallGraphAnalysisPass::analyze_call_site(CallGraphResult *result, Node *call_node, Node *containing_func, Module& module)
	{
		if (call_node->inputs.empty())
			return;

		Node *callee_operand = call_node->inputs[0];
		if (callee_operand->ir_type == NodeType::FUNCTION)
		{
			/* direct call is the simple case where we know exactly which function
			 * is being called at compile time */
			record_direct_call(result, call_node, containing_func, callee_operand);
		}
		else
		{
			/* indirect call through a function pointer. we need to chase the pointer
			 * to find all possible target functions */
			std::unordered_set<Node *> visited;
			std::vector<Node *> targets = chase_function_pointer(callee_operand, visited, module);
			for (Node *target: targets)
			{
				if (target && target->ir_type == NodeType::FUNCTION)
					record_indirect_call(result, call_node, containing_func, target);
			}

			/* if we couldn't resolve any targets, create an unresolved call edge
			 * to track that there is a call site here even though we don't know
			 * what it calls */
			if (targets.empty())
			{
				auto edge = CallEdge();
				edge.caller = containing_func;
				edge.call_site = call_node;
				edge.callee = nullptr;
				edge.indirect = true;
				edge.confidence = 0.0f;
				result->call_edges.push_back(edge);
			}
		}
	}

	std::vector<Node *> CallGraphAnalysisPass::chase_function_pointer(Node *pointer_node,
	                                                                  std::unordered_set<Node *> &visited, Module& module)
	{
		if (!pointer_node || visited.contains(pointer_node))
			return {};

		std::unordered_set<Node *> functions;

		/* fast path optimization using Arc's pointer qualifiers. const+restrict pointers
		 * are essentially compile-time constants for indirect calls, and restrict pointers
		 * eliminate most aliasing concerns that make general pointer analysis difficult */
		if (pointer_node->type_kind == DataType::POINTER && pointer_node->value.type() == DataType::POINTER)
		{
			const auto &ptr_data = pointer_node->value.get<DataType::POINTER>();

			if (is_const_pointer(ptr_data) &&
			    has_qualifier(ptr_data, DataTraits<DataType::POINTER>::PtrQualifier::RESTRICT))
			{
				/* const+restrict pointer can only point to its original target and
				 * that target can't change, so this is essentially a direct call
				 * disguised as an indirect one */
				if (ptr_data.pointee && ptr_data.pointee->ir_type == NodeType::FUNCTION)
					return { ptr_data.pointee };
			}

			if (has_qualifier(ptr_data, DataTraits<DataType::POINTER>::PtrQualifier::RESTRICT))
			{
				/* restrict pointer has no aliasing, making the analysis much simpler
				 * since we only need to look at direct stores to this specific pointer */
				for (Node *user: pointer_node->users)
				{
					if (user->ir_type == NodeType::PTR_STORE && user->inputs.size() >= 2 &&
					    user->inputs[1] == pointer_node)
					{
						chase_pointer_def(user->inputs[0], functions, visited, module);
					}
				}
				return { functions.begin(), functions.end() };
			}
		}

		/* general case: full pointer chasing through Arc's use-def chains */
		chase_pointer_def(pointer_node, functions, visited, module);
		return { functions.begin(), functions.end() };
	}

	void CallGraphAnalysisPass::chase_pointer_def(Node *node, std::unordered_set<Node *> &functions, // NOLINT(*-no-recursion)
	                                              std::unordered_set<Node *> &visited, Module& module)
	{
		if (!node || visited.contains(node))
			return;

		visited.insert(node);
		switch (node->ir_type)
		{
			case NodeType::FUNCTION:
				/* found the actual function; this is what the pointer points to */
				functions.insert(node);
				break;

			case NodeType::ADDR_OF:
				/* address-of function creates function pointer to the given function.
				 * this is a common pattern in systems programming where we take the address of a function
				 * to pass it around as a pointer */
				if (!node->inputs.empty() && node->inputs[0]->ir_type == NodeType::FUNCTION)
					functions.insert(node->inputs[0]);
				break;
			case NodeType::LOAD:
			case NodeType::PTR_LOAD:
				/* loading from memory needs to find what was stored there.
				 * this is where the real complexity of pointer analysis lives */
				chase_memory_location(node, functions, visited, module);
				break;

			case NodeType::PARAM:
				/* function parameter could be any function passed in as an argument.
				 * we need to look at all call sites of the containing function to see
				 * what actual functions get passed for this parameter */
			{
				Region *func_region = node->parent;
				while (func_region && func_region->parent())
					func_region = func_region->parent();

				if (func_region)
				{
					/* find the function this parameter belongs to and then look at
					 * all call sites to see what gets passed for this parameter */
					if (Node* owner_func = find_function_for_region(func_region, module))
					{
						std::size_t param_idx = find_parameter_index(owner_func, node);
						for (Node *caller: owner_func->users)
						{
							if ((caller->ir_type == NodeType::CALL || caller->ir_type == NodeType::INVOKE) &&
								caller->inputs.size() > param_idx + 1)
							{
								chase_pointer_def(caller->inputs[param_idx + 1], functions, visited, module);
							}
						}
					}
				}
			}
			break;

			case NodeType::FROM:
				/* the pointer could come from any of the input paths.
				 * this is common at control flow join points where different branches
				 * might assign different functions to the same pointer */
				for (Node *input: node->inputs)
					chase_pointer_def(input, functions, visited, module);
				break;

			case NodeType::CAST:
				/* follow through to the original pointer. this is common
				 * when casting between different function pointer types */
				if (!node->inputs.empty())
					chase_pointer_def(node->inputs[0], functions, visited, module);
				break;

			default:
				/* unknown definition source; just assume any exported
				 * function could be the target since we can't track this precisely */
				for (Node *func: visited)
				{
					if (func->ir_type == NodeType::FUNCTION &&
					    (func->traits & NodeTraits::EXPORT) != NodeTraits::NONE)
					{
						functions.insert(func);
					}
				}
				break;
		}
	}

	void CallGraphAnalysisPass::chase_memory_location(Node *load_node, std::unordered_set<Node *> &functions, // NOLINT(*-no-recursion)
	                                                  std::unordered_set<Node *> &visited, Module& module)
	{
		Node *address = nullptr;
		if ((load_node->ir_type == NodeType::LOAD || load_node->ir_type == NodeType::PTR_LOAD) && !load_node->inputs.empty())
			address = load_node->inputs[0];

		if (address)
			find_stores_to_location(address, functions, visited, module);
	}

	void CallGraphAnalysisPass::find_stores_to_location(Node *location, std::unordered_set<Node *> &functions, // NOLINT(*-no-recursion)
	                                                    std::unordered_set<Node *> &visited, Module& module)
	{
		/* look for all STORE operations that write to this memory location.
		 * this is where we connect the dots between function pointer assignments
		 * and the loads that read those assignments */
		for (Node *user: location->users)
		{
			if (user->ir_type == NodeType::STORE && user->inputs.size() >= 2)
			{
				if (user->inputs[1] == location)
					chase_pointer_def(user->inputs[0], functions, visited, module);
			}
			else if (user->ir_type == NodeType::PTR_STORE && user->inputs.size() >= 2)
			{
				if (user->inputs[1] == location)
					chase_pointer_def(user->inputs[0], functions, visited, module);
			}
		}

		if (location->ir_type == NodeType::ADDR_OF && !location->inputs.empty())
		{
			/* if the location is an address-of operation, we need to find all stores
			 * to the original pointer definition to find where it was assigned */
			Node* alloc_site = location->inputs[0];
			find_stores_to_location(alloc_site, functions, visited, module);
		}
	}

	void CallGraphAnalysisPass::analyze_parameter_flow(CallGraphResult *result, Module &module)
	{
	    /* analyze how parameters flow through functions to determine escapement.
	     * a parameter escapes if it can be observed outside the function through
	     * returns, stores to global memory, or passing to other functions that
	     * might let it escape */
	    for (Node *func: module.functions())
	    {
	        if (func->ir_type != NodeType::FUNCTION)
	            continue;

	        Region *func_region = find_function_region(func, module);
	        if (!func_region)
	            continue;

	        /* parameters are stored in the function's inputs list, not as separate
	         * nodes in the region. each input represents a parameter in declaration order */
	        for (std::size_t param_idx = 0; param_idx < func->inputs.size(); ++param_idx)
	        {
	            Node *param_node = func->inputs[param_idx];
	            if (!param_node || param_node->ir_type != NodeType::PARAM)
	                continue;

	            ParamInfo info;
	            info.escapes = parameter_escapes_analysis(param_node);
	            info.read_only = true;

	            /* check if parameter is ever modified within the function */
	            for (Node *user: param_node->users)
	            {
	                if (user->ir_type == NodeType::STORE || user->ir_type == NodeType::PTR_STORE)
	                {
	                    if (user->inputs.size() >= 2 && user->inputs[1] == param_node)
	                    {
	                        info.read_only = false;
	                        break;
	                    }
	                }
	            }

	            /* collect specific nodes that cause this parameter to escape */
	            for (Node *user: param_node->users)
	            {
	                bool causes_escape = false;

	                switch (user->ir_type)
	                {
	                    case NodeType::RET:
	                        if (!user->inputs.empty() && user->inputs[0] == param_node)
	                            causes_escape = true;
	                        break;

	                    case NodeType::STORE:
	                    case NodeType::PTR_STORE:
	                        if (user->inputs[0] == param_node)
	                            causes_escape = true;
	                        break;

	                    case NodeType::CALL:
	                    case NodeType::INVOKE:
	                        /* check if parameter is passed as argument to another function */
	                        for (std::size_t i = 1; i < user->inputs.size(); ++i)
	                        {
	                            if (user->inputs[i] == param_node)
	                            {
	                                causes_escape = true;
	                                break;
	                            }
	                        }
	                        break;

	                    case NodeType::ADDR_OF:
	                        if (user->inputs[0] == param_node)
	                            causes_escape = true;
	                        break;
	                    default:
	                        break;
	                }

	                if (causes_escape)
	                    info.escape_sites.push_back(user);
	            }

	            result->param_info[{func, param_idx}] = std::move(info);
	        }
	    }
	}

	bool CallGraphAnalysisPass::parameter_escapes_analysis(Node *param)
	{
		if (!param)
			return true;

		/* fast path optimizations using Arc's pointer qualifier system.
		 * these qualifiers give us strong guarantees about how pointers
		 * can be used, allowing us to skip expensive general analysis */
		if (param->type_kind == DataType::POINTER && param->value.type() == DataType::POINTER)
		{
			const auto &ptr_data = param->value.get<DataType::POINTER>();

			if (is_const_pointer(ptr_data))
			{
				/* const pointer means the data can't be modified, so it can only
				 * escape via return or passing to other functions */
				return escapes_via_return_only(param);
			}

			if (has_qualifier(ptr_data, DataTraits<DataType::POINTER>::PtrQualifier::RESTRICT))
			{
				/* restrict parameter means no aliasing concerns, which simplifies
				 * the analysis significantly */
				for (Node *user: param->users)
				{
					switch (user->ir_type)
					{
						case NodeType::RET:
						case NodeType::STORE:
						case NodeType::PTR_STORE:
						case NodeType::CALL:
						case NodeType::INVOKE:
						case NodeType::ADDR_OF:
							return true;
						default:
							break;
					}
				}
				return false;
			}
		}

		/* scalar parameters (integers, floats, booleans) can only escape via return.
		 * they can't be stored to memory or passed by reference, so the analysis
		 * is much simpler */
		if (is_integer_t(param->type_kind) || is_float_t(param->type_kind) ||
		    param->type_kind == DataType::BOOL)
		{
			return escapes_via_return_only(param);
		}

		/* general escapement analysis for complex cases */
		for (Node *user: param->users)
		{
			switch (user->ir_type)
			{
				case NodeType::RET:
					if (!user->inputs.empty() && user->inputs[0] == param)
						return true;
					break;

				case NodeType::STORE:
				case NodeType::PTR_STORE:
					if (user->inputs[0] == param)
						return true;
					break;

				case NodeType::CALL:
				case NodeType::INVOKE:
					/* conservatively assume any function call might cause the parameter
					 * to escape. a more sophisticated analysis could look at the callee
					 * to see if it actually stores the parameter anywhere */
					for (std::size_t i = 1; i < user->inputs.size(); ++i)
					{
						if (user->inputs[i] == param)
							return true;
					}
					break;

				case NodeType::ADDR_OF:
					if (user->inputs[0] == param)
						return true;
					break;

				default:
					/* pure computational uses don't cause escape */
					break;
			}
		}

		return false;
	}

	bool CallGraphAnalysisPass::escapes_via_return_only(Node *param)
	{
		/* check if parameter only escapes through return statements, not through
		 * stores or calls that might persist it beyond the function lifetime */
		for (Node *user: param->users)
		{
			switch (user->ir_type)
			{
				case NodeType::RET:
					if (!user->inputs.empty() && user->inputs[0] == param)
						return true;
					break;

				case NodeType::STORE:
				case NodeType::PTR_STORE:
				case NodeType::CALL:
				case NodeType::INVOKE:
				case NodeType::ADDR_OF:
					return true;

				default:
					break;
			}
		}
		return false;
	}

	void CallGraphAnalysisPass::compute_function_purity(CallGraphResult *result, Module &module)
	{
		/* compute which functions are pure (have no observable side effects).
		 * a function is pure if it doesn't modify global state, doesn't call
		 * impure functions, and doesn't perform I/O operations */
		for (Node *func: module.functions())
		{
			if (func->ir_type != NodeType::FUNCTION)
				continue;

			if (analyze_function_purity(func, *result, module))
				result->pure_functions.insert(func);
		}
	}

	bool CallGraphAnalysisPass::analyze_function_purity(Node *func, const CallGraphResult &cg, Module& m)
	{
		/* extern functions are conservatively assumed to be impure since we
		 * don't know what their implementation does */
		if (cg.extern_functions.contains(func))
			return false;

		Region *func_region = find_function_region(func, m);
		if (!func_region)
			return false;

		bool is_pure = true;
		func_region->walk_dominated_regions([&](Region *region)
		{
			for (Node *node: region->nodes())
			{
				switch (node->ir_type)
				{
					case NodeType::STORE:
					case NodeType::PTR_STORE:
						/* storing to memory is a side effect unless it's through a
						 * writeonly pointer, which is specifically for output parameters */
						if (node->inputs.size() >= 2)
						{
							Node *target = node->inputs[1];
							if (!is_writeonly_pointer(target))
							{
								is_pure = false;
								return;
							}
						}
						else
						{
							is_pure = false;
							return;
						}
						break;

					case NodeType::ATOMIC_STORE:
					case NodeType::ATOMIC_CAS:
						/* atomic operations always have observable side effects */
						is_pure = false;
						return;

					case NodeType::CALL:
					case NodeType::INVOKE:
						/* calling impure functions makes this function impure too.
						 * this creates a dependency between purity analysis and the
						 * call graph, which is why we compute purity after building
						 * the complete call graph */
					{
						std::vector<Node *> call_targets = cg.targets(node);
						for (Node *target: call_targets)
						{
							if (!cg.pure(target))
							{
								is_pure = false;
								return;
							}
						}
					}
					break;

					default:
						break;
				}
			}
		});

		return is_pure;
	}

	void CallGraphAnalysisPass::compute_scc(CallGraphResult *result)
	{
		/* compute strongly connected components using Tarjan's algorithm to identify
		 * recursive function groups. this is essential for termination analysis and
		 * enables optimizations that depend on knowing which functions can call
		 * themselves directly or indirectly */
		std::unordered_map<Node *, int> indices;
		std::unordered_map<Node *, int> lowlinks;
		std::unordered_set<Node *> on_stack;
		std::stack<Node *> stack;
		int index = 0;

		std::function<void(Node *)> strongconnect = [&](Node *func)
		{
			indices[func] = index;
			lowlinks[func] = index;
			index++;
			stack.push(func);
			on_stack.insert(func);

			/* examine all functions called by this function */
			if (auto it = result->callee_map.find(func);
				it != result->callee_map.end())
			{
				for (Node *callee: it->second)
				{
					if (indices.find(callee) == indices.end())
					{
						/* callee hasn't been visited yet - recurse */
						strongconnect(callee);
						lowlinks[func] = std::min(lowlinks[func], lowlinks[callee]);
					}
					else if (on_stack.contains(callee))
					{
						/* callee is on the stack, so it's part of the current SCC */
						lowlinks[func] = std::min(lowlinks[func], indices[callee]);
					}
				}
			}

			/* if this is the root of an SCC, pop the entire component */
			if (lowlinks[func] == indices[func])
			{
				std::vector<Node *> component;
				Node *current;
				do
				{
					current = stack.top();
					stack.pop();
					on_stack.erase(current);
					component.push_back(current);
				}
				while (current != func);

				/* record the SCC for each function in the component */
				for (Node *member: component)
					result->scc_map[member] = component;
			}
		};

		/* run Tarjan's algorithm on all functions that haven't been visited yet */
		for (const auto &[func, callees]: result->callee_map)
		{
			if (!indices.contains(func))
				strongconnect(func);
		}
	}

	Region *CallGraphAnalysisPass::find_function_region(Node *func, Module &module)
	{
		if (!func || func->ir_type != NodeType::FUNCTION)
			return nullptr;

		/* find the region corresponding to this function by looking for a child
		 * region of the module root with the same name as the function */
		std::string_view func_name = module.strtable().get(func->str_id);
		for (Region *child: module.root()->children())
		{
			if (child->name() == func_name)
				return child;
		}

		return nullptr;
	}

	Node *CallGraphAnalysisPass::find_function_for_region(Region *region, Module &module)
	{
		if (!region)
			return nullptr;

		/* find the function node that corresponds to this region by matching names */
		std::string_view region_name = region->name();
		for (Node *func: module.functions())
		{
			if (func->ir_type == NodeType::FUNCTION)
			{
				std::string_view func_name = module.strtable().get(func->str_id);
				if (func_name == region_name)
					return func;
			}
		}

		return nullptr;
	}

	std::size_t CallGraphAnalysisPass::find_parameter_index(Node *func, Node *param)
	{
		if (!func || !param || func->ir_type != NodeType::FUNCTION)
			return SIZE_MAX;

		/* parameters are stored in the function's inputs list in order, so we can
		 * find the parameter index by searching through the inputs */
		for (std::size_t i = 0; i < func->inputs.size(); ++i)
		{
			if (func->inputs[i] == param)
				return i;
		}

		return SIZE_MAX;
	}

	bool CallGraphAnalysisPass::is_assignment_context(Node *user)
	{
		/* determine if a node represents a context where a function could be
		 * assigned to a pointer, making it a potential target for indirect calls */
		switch (user->ir_type)
		{
			/*
			 * - function stored to memory location
			 * - function returned from function becomes unknown after return
			 * - function passed as argument to another function
			 */
			case NodeType::STORE:
			case NodeType::PTR_STORE:
			case NodeType::RET:
			case NodeType::CALL:
			case NodeType::INVOKE:
				return true;

			default:
				return false;
		}
	}

	void CallGraphAnalysisPass::record_direct_call(CallGraphResult *result, Node *call_site, Node *caller, Node *callee)
	{
		/* record a direct function call in the call graph. these are the easy cases
		 * where we know exactly which function is being called at compile time */
		auto edge = CallEdge();
		edge.caller = caller;
		edge.call_site = call_site;
		edge.callee = callee;
		edge.indirect = false;
		edge.confidence = 1.0f;

		result->call_edges.push_back(edge);
		result->caller_map[callee].push_back(caller);
		result->callee_map[caller].push_back(callee);
	}

	void CallGraphAnalysisPass::record_indirect_call(CallGraphResult *result, Node *call_site, Node *caller,
	                                                 Node *callee)
	{
		/* record an indirect function call that was resolved through pointer analysis.
		 * these have lower confidence since pointer analysis isn't always perfect,
		 * especially in the presence of complex control flow or external functions */
		auto edge = CallEdge();
		edge.caller = caller;
		edge.call_site = call_site;
		edge.callee = callee;
		edge.indirect = true;
		edge.confidence = 0.8f; /* high confidence for our pointer chasing algorithm; maybe adjusted later */

		result->call_edges.push_back(edge);
		result->caller_map[callee].push_back(caller);
		result->callee_map[caller].push_back(callee);
	}
}
