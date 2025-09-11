/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_set>
#include <arc/codegen/insn-selector.hpp>
#include <arc/codegen/selection-dag.hpp>
#include <arc/foundation/region.hpp>

namespace arc
{
	/**
	 * @brief Register class enumeration for allocation
	 */
	enum class RegisterClass : std::uint8_t
	{
		GENERAL_PURPOSE,
		VECTOR,
		PREDICATE
	};

	/**
	 * @brief Register constraints for a region hierarchy
	 */
	template<typename Arch>
	struct Constraints
	{
		using register_type = typename Arch::register_type;

		std::unordered_map<RegisterClass, std::uint32_t> min_required;
		std::unordered_map<RegisterClass, std::uint32_t> max_simultaneous;
		std::unordered_map<RegisterClass, float> complexity;
		std::uint32_t loop_depth = 0;

		/**
		 * @brief Check if spilling is required given available registers
		 */
		[[nodiscard]] bool needs_spill(const std::unordered_map<RegisterClass, std::uint32_t> &available) const
		{
			return std::ranges::any_of(min_required, [&](const auto &pair)
			{
				const auto &[cls, required] = pair;
				auto it = available.find(cls);
				return it == available.end() || it->second < required;
			});
		}
	};

	/**
	 * @brief Register budget for hierarchical allocation
	 */
	template<typename Arch>
	struct Budget
	{
		using register_type = typename Arch::register_type;

		std::unordered_map<RegisterClass, std::unordered_set<register_type> > available;
		std::unordered_map<RegisterClass, std::uint32_t> allocated;
		float complexity_ratio = 1.0f;
	};

	/**
	 * @brief Allocation request from instruction selector
	 */
	template<typename Arch>
	struct Request
	{
		using register_type = typename Arch::register_type;

		RegisterClass cls = {};
		std::optional<register_type> hint;
		std::vector<register_type> forbidden;
		bool allow_spill = true;
		std::uint32_t priority = 0;
	};

	/**
	 * @brief Allocation result for instruction selector
	 */
	template<typename Arch>
	struct Result
	{
		using register_type = typename Arch::register_type;

		std::optional<register_type> reg;
		bool spilled = false;

		[[nodiscard]] bool allocated() const
		{
			return reg.has_value();
		}

		[[nodiscard]] bool on_stack() const
		{
			return spilled;
		}
	};

	/**
	 * @brief Target architecture concept for register allocation
	 */
	template<typename T>
	concept TargetArchitecture = InstructionSelectorTarget<T> && requires(T arch)
	{
		typename T::register_type;
		{ arch.count(RegisterClass {}) } -> std::convertible_to<std::uint32_t>;
		{ arch.caller_saved(RegisterClass {}) } -> std::convertible_to<std::vector<typename T::register_type> >;
		{ arch.callee_saved(RegisterClass {}) } -> std::convertible_to<std::vector<typename T::register_type> >;
		{ arch.spill_cost(typename T::register_type {}) } -> std::convertible_to<std::uint32_t>;
		{ arch.uses_vector_for_float() } -> std::convertible_to<bool>;
	};

	/**
	 * @brief Register allocator component for instruction selection
	 */
	template<TargetArchitecture Arch>
	class RegisterAllocator
	{
	public:
		using register_type = typename Arch::register_type;
		using instruction_type = typename Arch::instruction_type;
		using dag_type = SelectionDAG<instruction_type>;
		using dag_node = typename dag_type::DAGNode;

		explicit RegisterAllocator(const Arch &target, dag_type &dag) : arch(target), selection_dag(dag) {}

		Budget<Arch> allocate(Region *region, Budget<Arch> available)
		{
			region_budgets[region] = {
				.available = available.available,
				.allocated = available.allocated
			};
			return allocate_region(region, available);
		}

		/**
		 * @brief Allocate registers for a region hierarchy
		 * @param region Root region to allocate
		 * @param available Budget available from parent
		 * @return Allocated budget for this region
		 */
		Budget<Arch> allocate_region(Region *region, Budget<Arch> available)
		{
			/* bottom-up constraint analysis computes register requirements for the
			 * entire region hierarchy rooted at this region. this gives us accurate
			 * pressure estimates before we start making allocation decisions */
			auto constraints = analyze(region);

			/* if constraint analysis determines that register requirements exceed
			 * availability, try simple register reuse through use-def analysis
			 * before resorting to spilling. this catches obvious reuse opportunities
			 * without expensive interference graph construction */
			if (constraints.needs_spill(count_n(available)))
			{
				auto local_nodes = nodes(region);
				std::vector<dag_node *> reuse_candidates;

				/* identify values that can reuse registers from non-overlapping lifetimes */
				auto can_reuse = [&](dag_node *node) -> bool
				{
					auto node_range = compute_live_range(node);

					/* check if any other value has a non-overlapping lifetime that
					 * ends before this node's definition, allowing register reuse */
					for (auto *other: local_nodes)
					{
						if (other != node && needs_allocation(other) &&
						    infer_class(other->value_t) == infer_class(node->value_t))
						{
							if (std::pair<std::uint32_t, std::uint32_t> other_range = compute_live_range(other);
								other_range.second <= node_range.first)
							{
								return true;
							}
						}
					}
					return false;
				};

				for (auto *node: local_nodes)
				{
					if (needs_allocation(node) && can_reuse(node))
						reuse_candidates.push_back(node);
				}

				/* apply register reuse and mark successful reuses */
				for (auto *candidate: reuse_candidates)
				{
					/* find a register from a value whose lifetime has ended that this
					 * candidate can reuse, avoiding the need to allocate a new register */
					auto find_reusable = [&](dag_node *node) -> std::optional<register_type>
					{
						RegisterClass cls = infer_class(node->value_t);
						auto node_range = compute_live_range(node);

						/* look through all allocated values to find one whose lifetime
						 * ended before this node's definition */
						for (const auto &[allocated_node, result]: allocations)
						{
							if (result.allocated() &&
							    infer_class(allocated_node->value_t) == cls &&
							    allocated_node->source && allocated_node->source->parent == region)
							{
								auto allocated_range = compute_live_range(allocated_node);
								if (allocated_range.second <= node_range.first)
								{
									return result.reg;
								}
							}
						}
						return std::nullopt;
					};

					if (auto reused_reg = find_reusable(candidate))
					{
						/* mark register reuse by creating allocation with existing register */
						allocations[candidate] = Result<Arch> { .reg = *reused_reg };
					}
				}

				/* if reuse wasn't sufficient, proceed with spilling */
				auto updated_constraints = analyze(region);
				if (updated_constraints.needs_spill(count_n(available)))
				{
					auto spill_candidates = identify_spill_candidates(region);
					for (auto *node: spill_candidates)
						mark_for_spill(node);
				}
			}

			return allocate_proportional(region, available, constraints);
		}

		/**
		 * @brief Allocate register for DAG node which would be called by the instruction selector
		 * @param node DAG node needing allocation
		 * @param req Allocation preferences and constraints
		 * @return Register or spill marker
		 */
		Result<Arch> allocate_node(dag_node *node, const Request<Arch> &req)
		{
			if (!node || !node->source)
				return {};
			/* check if this node already has an allocation from previous requests.
			 * this can happen when the instruction selector queries the same node
			 * multiple times during pattern matching */
			if (auto it = allocations.find(node);
				it != allocations.end())
			{
				return it->second;
			}

			Region *node_region = node->source->parent;
			if (!node_region)
				return {};

			Result<Arch> result = perform_allocation(node_region, req);
			allocations[node] = result;
			return result;
		}

		/**
		 * @brief Release register when value lifetime ends
		 * @param node DAG node whose register can be freed
		 */
		void release(dag_node *node)
		{
			auto it = allocations.find(node);
			if (it == allocations.end())
				return;

			auto &result = it->second;
			if (result.reg)
			{
				/* return the register to the available pool for this region.
				 * this allows later allocations in the same region to reuse
				 * registers from values whose lifetimes have ended */
				Region *node_region = node->source->parent;
				auto &budget = region_budgets[node_region];
				RegisterClass cls = infer_class(node->value_t);
				budget.available[cls].insert(*result.reg);
				--budget.allocated[cls];
			}
		}

		/**
		 * @brief Query register availability in region
		 * @param region Region to check
		 * @param cls Register class
		 * @param reg Specific register ID
		 * @return True if register is available for allocation
		 */
		bool available(Region *region, RegisterClass cls, register_type reg) const
		{
			auto it = region_budgets.find(region);
			if (it == region_budgets.end())
				return false;

			auto &available_regs = it->second.available.at(cls);
			return available_regs.contains(reg);
		}

		/**
		 * @brief Get current register pressure for region
		 * @param region Region to query
		 * @param cls Register class
		 * @return Number of registers currently allocated
		 */
		std::uint32_t pressure(Region *region, RegisterClass cls) const
		{
			auto it = region_budgets.find(region);
			if (it == region_budgets.end())
			{
				return 0;
			}

			auto pressure_it = it->second.allocated.find(cls);
			auto result = pressure_it != it->second.allocated.end() ? pressure_it->second : 0;
			return result;
		}

		/**
		 * @brief Force spill a value (called by instruction selector)
		 * @param node DAG node to spill
		 * @return Spill result marker
		 */
		Result<Arch> force_spill(dag_node *node)
		{
			if (!node || !node->source)
				return {};

			/* mark the value as spilled. the actual spill code generation
			 * (stack slot allocation, spill/reload instructions) is handled
			 * by the code generator, not the register allocator */
			Result<Arch> result;
			result.spilled = true;

			allocations[node] = result;
			return result;
		}

		/**
		 * @brief Get allocation result for node
		 * @param node DAG node to query
		 * @return Current allocation result
		 */
		Result<Arch> get(dag_node *node) const
		{
			auto it = allocations.find(node);
			return it != allocations.end() ? it->second : Result<Arch> {};
		}

	private:
		const Arch &arch;
		dag_type &selection_dag;

		/**
		 * @brief Budget and allocation state for a region
		 */
		struct RegionBudget
		{
			std::unordered_map<RegisterClass, std::unordered_set<register_type> > available;
			std::unordered_map<RegisterClass, std::uint32_t> allocated;
		};

		std::unordered_map<Region *, RegionBudget> region_budgets;
		std::unordered_map<dag_node *, Result<Arch> > allocations;
		std::unordered_map<Region *, Constraints<Arch> > region_constraints;

		/**
		 * @brief Bottom-up constraint analysis with temporal overlap computation
		 */
		Constraints<Arch> analyze(Region *region)
		{
			/* compute local constraints for this region first, then recursively
			 * analyze children and merge their constraints upward. this gives us
			 * a complete picture of register requirements for the entire hierarchy */
			Constraints<Arch> local = compute_local(region);

			for (Region *child: region->children())
			{
				auto child_constraints = analyze(child);
				merge(local, child_constraints);
			}

			/* temporal overlap analysis computes the maximum number of values that
			 * will be simultaneously live across all possible execution states of
			 * this region. this is the key insight that makes hierarchical allocation
			 * work instead of conservative summation, we compute actual interference */
			compute_overlap(local, region);
			region_constraints[region] = local;
			return local;
		}

		/**
		 * @brief Compute register constraints for a single region
		 */
		Constraints<Arch> compute_local(Region *region)
		{
			Constraints<Arch> constraints;

			/* loop nesting depth affects register pressure because inner loops
			 * create overlapping live ranges across iterations. values that are
			 * live across loop backedges effectively have extended lifetimes */
			constraints.loop_depth = compute_loop_depth(region);

			auto local_nodes = nodes(region);
			for (RegisterClass cls: { RegisterClass::GENERAL_PURPOSE, RegisterClass::VECTOR, RegisterClass::PREDICATE })
			{
				auto pressure_info = compute_pressure(local_nodes, cls);

				/* loop complexity multiplier models the reality that inner loops
				 * execute more frequently and create more register pressure. the
				 * quadratic scaling reflects that deeply nested loops create
				 * exponentially more pressure due to overlapping iterations */
				float loop_multiplier = 1.0f + (static_cast<float>(std::pow(constraints.loop_depth, 2)) * 0.3f);

				constraints.min_required[cls] = pressure_info.min_required;
				constraints.max_simultaneous[cls] = pressure_info.max_simultaneous;
				constraints.complexity[cls] = pressure_info.complexity * loop_multiplier;
			}

			return constraints;
		}

		/**
		 * @brief Merge child constraints into parent
		 */
		void merge(Constraints<Arch> &parent, Constraints<Arch> &child)
		{
			/* merging child constraints requires careful consideration of temporal
			 * relationships. we can't just sum requirements because child regions
			 * might not execute concurrently. the temporal overlap analysis later
			 * refines these conservative estimates */
			for (const auto &[cls, child_req]: child.min_required)
			{
				parent.min_required[cls] = std::max(parent.min_required[cls], child_req);
				auto it = parent.max_simultaneous.find(cls);
				parent.max_simultaneous[cls] = std::max(parent.max_simultaneous[cls], child.max_simultaneous[cls]);
				parent.complexity[cls] += child.complexity[cls];
			}
		}

		/**
		 * @brief Compute temporal overlap for maximum simultaneous register requirements
		 */
		void compute_overlap(Constraints<Arch> &constraints, Region *region)
		{
			/* reset max simultaneous to compute accurate temporal overlap rather
			 * than using the conservative estimates from constraint merging */
			for (auto &[cls, max_sim]: constraints.max_simultaneous)
				max_sim = 0;

			/* analyze each possible execution state of this region to find the
			 * maximum number of values that could be simultaneously live. this
			 * is the heart of the hierarchical allocation algorithm - instead of
			 * building global interference graphs, we use region structure to
			 * compute interference relationships directly */
			auto exe_state = execution_states(region);
			for (const auto &state: exe_state)
			{
				std::unordered_map<RegisterClass, std::uint32_t> concurrent;

				/* count register requirements from all child regions that could
				 * be executing concurrently in this execution state. this gives
				 * us the actual interference, not a conservative upper bound */
				for (Region *child: state)
				{
					if (auto it = region_constraints.find(child);
						it != region_constraints.end())
					{
						for (const auto &[cls, req]: it->second.min_required)
						{
							concurrent[cls] += req;
						}
					}
				}

				/* update maximum simultaneous interference across all execution states */
				for (const auto &[cls, active_req]: concurrent)
					constraints.max_simultaneous[cls] = std::max(constraints.max_simultaneous[cls], active_req);
			}
		}

		/**
		 * @brief Complexity-proportional budget allocation strategy
		 */
		Budget<Arch> allocate_proportional(Region *region, Budget<Arch> available, const Constraints<Arch> &constraints)
		{
			/* complexity-proportional budget allocation distributes registers based
			 * on computational complexity rather than arbitrary ratios. regions with
			 * more complex computations get more registers since they benefit more
			 * from keeping values in registers rather than spilling */
			float total_complexity = compute_total_complexity(constraints);
			float parent_complexity = compute_parent_complexity(constraints, region);
			float child_complexity = total_complexity - parent_complexity;

			float parent_ratio = (child_complexity > 0)
				                     ? parent_complexity / (parent_complexity + child_complexity)
				                     : 1.0f;

			Budget<Arch> parent_budget = allocate_parent_budget(available, parent_ratio);

			/* allocate local values with FROM node optimization to eliminate
			 * unnecessary move instructions at control flow merge points */
			allocate_local_values(region, parent_budget);

			/* distribute remaining budget to children based on their requirements */
			Budget<Arch> remaining = compute_remaining(available, parent_budget);
			allocate_children(region, remaining);

			return parent_budget;
		}

		/**
		 * @brief Spill-first allocation when constraints indicate spilling is needed
		 */
		Budget<Arch> spill_first(Region *region, Budget<Arch> available, const Constraints<Arch> &constraints)
		{
			/* when constraint analysis determines that register requirements exceed
			 * availability, immediately identify spill candidates based on usage
			 * patterns and value importance. this avoids the complexity of trying
			 * to allocate and then backtracking when we run out of registers */
			auto spill_candidates = identify_spill_candidates(region);
			/* mark candidates for spilling. the actual spill code generation
			 * happens later during code generation, not during allocation */
			for (auto *node: spill_candidates)
				mark_for_spill(node);

			return allocate_proportional(region, available, constraints);
		}

		/**
		 * @brief Allocate values local to this region
		 */
		void allocate_local_values(Region *region, Budget<Arch> &budget)
		{
		    auto local_nodes = nodes(region);
		    /* separate FROM nodes from other values because FROM nodes have special
		     * register reuse opportunities that can eliminate move instructions at
		     * control flow merge points */
		    std::vector<dag_node *> from_nodes;
		    std::vector<dag_node *> other_nodes;

		    for (auto *node: local_nodes)
		    {
		        if (node->source && node->source->ir_type == NodeType::FROM)
		            from_nodes.push_back(node);
		        else if (needs_allocation(node))
		            other_nodes.push_back(node);
		    }

		    /* allocate FROM nodes first because they have the highest potential
		     * for register reuse, which can significantly reduce code size and
		     * improve performance by eliminating move instructions */
		    for (auto *node: from_nodes)
		        allocate_from_node(node, budget);

		    /* sort other nodes by value_id to process in topological order */
		    std::ranges::sort(other_nodes, [](const dag_node *a, const dag_node *b)
		    {
		        return a->value_id < b->value_id;
		    });

		    for (auto *node: other_nodes)
		    {
		        /* release registers from values whose lifetimes have ended before
		         * allocating for this node. this enables register reuse in sequential
		         * dependency chains where earlier values die before later ones are defined */
		        std::uint32_t current_pos = node->value_id;

		        /* collect dead allocations to avoid modifying map during iteration */
		        std::vector<dag_node*> dead_nodes;
		        for (auto &[allocated_node, result]: allocations)
		        {
		            if (result.allocated() && allocated_node->source &&
		                allocated_node->source->parent == region)
		            {
			            /* if this value's lifetime has ended, mark for release */
		                if (auto live_range = compute_live_range(allocated_node);
			                live_range.second < current_pos)  /* use < instead of <= to avoid premature release */
		                {
		                    dead_nodes.push_back(allocated_node);
		                }
		            }
		        }

		        /* release registers from dead values */
		        for (auto *dead_node : dead_nodes)
		        {
		            auto &result = allocations[dead_node];
		            RegisterClass cls = infer_class(dead_node->value_t);
		            register_type released_reg = *result.reg;

		            /* add register back to available pool */
		            budget.available[cls].insert(released_reg);
		            --budget.allocated[cls];

		            /* update master region tracking for pressure reporting */
		            auto &master_budget = region_budgets[region];
		            master_budget.available[cls].insert(released_reg);
		            --master_budget.allocated[cls];

		            /* mark as released by clearing the allocation */
		            result.reg = std::nullopt;
		        }

		    	allocate_regular(node, budget);
		    }
		}

		/**
		 * @brief Allocate FROM node with register reuse optimization
		 */
		void allocate_from_node(dag_node *node, Budget<Arch> &budget)
		{
			RegisterClass cls = infer_class(node->value_t);

			/* we try to reuse registers from source values in order to
			 * eliminate move instructions. since FROM nodes represent control
			 * flow merges, reusing a source register means the value can flow
			 * directly from one path to the merge point without an explicit move */
			for (auto *source: node->operands)
			{
				if (auto alloc = get(source);
					alloc.allocated())
				{
					if (is_available(budget, cls, *alloc.reg))
					{
						allocate_specific(node, *alloc.reg, cls, budget);
						return;
					}
				}
				else if (source->source && source->source->parent != node->source->parent)
				{
					/* if the source value is from a different region and hasn't been
					 * allocated yet, we can try to allocate it now to enable reuse
					 * as they are more likely to be available at control flow merge points */
					auto caller_saved = arch.caller_saved(cls);
					if (!caller_saved.empty() && is_available(budget, cls, caller_saved[0]))
					{
						allocate_specific(node, caller_saved[0], cls, budget);
						return;
					}
				}
			}

			/* if no source register can be reused, fall back to regular allocation.
			 * this will require move instructions to be generated during code
			 * generation to handle the control flow merge */
			allocate_regular(node, budget);
		}

		/**
		 * @brief Register pressure information for a register class
		 */
		struct PressureInfo
		{
			std::uint32_t min_required;
			std::uint32_t max_simultaneous;
			float complexity;
		};

		/**
		 * @brief Compute register pressure using live range analysis
		 */
		PressureInfo compute_pressure(const std::vector<dag_node *> &nodes, RegisterClass cls) const
		{
			PressureInfo info { 0, 0, 0.0f };

			/* use a simplified live range analysis based on DAG node positions.
			 * while not as precise as full dataflow analysis, this is sufficient
			 * for the hierarchical approach because region boundaries naturally
			 * limit the scope of interference */
			std::vector<std::pair<std::uint32_t, int> > events;

			for (auto *node: nodes)
			{
				if (infer_class(node->value_t) == cls && needs_allocation(node))
				{
					auto range = compute_live_range(node);
					events.emplace_back(range.first, 1);   /* live range start */
					events.emplace_back(range.second, -1); /* live range end */
					info.complexity += compute_node_complexity(node);
				}
			}

			/* compute maximum simultaneous live values using a sweep line algorithm.
			 * this gives us the minimum number of registers needed to avoid spilling */
			std::ranges::sort(events);
			std::uint32_t current_live = 0;
			for (const auto &[pos, delta]: events)
			{
				current_live += static_cast<std::uint32_t>(delta);
				info.max_simultaneous = std::max(info.max_simultaneous, current_live);
			}

			info.min_required = info.max_simultaneous;
			return info;
		}

		std::uint32_t compute_loop_depth(Region *region) const
		{
			/* compute loop nesting depth by walking up the region hierarchy and
			 * counting how many parent regions represent loops. this is used to
			 * apply complexity multipliers for loop-based register pressure */
			std::uint32_t depth = 0;
			Region *current = region;
			while (current)
			{
				if (is_loop_region(current))
					depth++;
				current = current->parent();
			}
			return depth;
		}

		bool is_loop_region(Region *region) const
		{
			if (!region)
				return false;

			Node *entry = region->entry();
			if (!entry)
				return false;

			/* detect loops using dominance-based back edge analysis. a region is
			 * a loop if there are control flow instructions in dominated regions
			 * that target this region's entry. this creates the characteristic
			 * back edge pattern that defines natural loops */
			for (Node *user: entry->users)
			{
				if ((user->ir_type == NodeType::JUMP || user->ir_type == NodeType::BRANCH) &&
				    user->parent && region->dominates(user->parent))
				{
					return true;
				}
			}
			return false;
		}

		std::vector<std::vector<Region *> > execution_states(Region *region) const
		{
			/* compute possible execution states for temporal overlap analysis.
			 * each execution state represents a set of child regions that could
			 * be executing concurrently, which determines interference patterns */
			std::vector<std::vector<Region *> > states;

			/* for most regions, children execute sequentially so we only need
			 * to consider one child active at a time. this is the common case
			 * for structured control flow */
			for (Region *child: region->children())
			{
				states.push_back({ child });
			}

			/* conservative estimate: assume all children could be active simultaneously.
			 * this handles complex control flow patterns where precise analysis is
			 * difficult. it's better to overestimate register pressure than to
			 * underestimate and cause spills */
			if (!region->children().empty())
			{
				std::vector<Region *> all_children(region->children().begin(), region->children().end());
				states.push_back(all_children);
			}

			return states;
		}

		std::vector<dag_node *> nodes(Region *region) const
		{
			/* extract DAG nodes that belong to this region by checking the source
			 * IR node's parent region. this gives us the set of values that need
			 * register allocation within this region */
			std::vector<dag_node *> ns;
			for (auto *node: selection_dag.nodes())
			{
				if (node->source && node->source->parent == region)
					ns.push_back(node);
			}
			return ns;
		}

		[[nodiscard]] RegisterClass infer_class(DataType type) const
		{
			/* infer register class from value type using target-specific rules.
			 * different architectures have different conventions for where floating
			 * point values are stored (general registers vs vector registers) */
			switch (type)
			{
				case DataType::VECTOR:
					return RegisterClass::VECTOR;
				case DataType::FLOAT32:
				case DataType::FLOAT64:
					return arch.uses_vector_for_float() ? RegisterClass::VECTOR : RegisterClass::GENERAL_PURPOSE;
				default:
					return RegisterClass::GENERAL_PURPOSE;
			}
		}

		bool needs_allocation(dag_node *node) const
		{
			/* determine if a DAG node represents a value that needs register allocation.
			 * only value-producing nodes need registers; structural nodes like control
			 * flow and memory operations often don't produce allocatable values */
			return node->kind == NodeKind::VALUE && node->value_t != DataType::VOID;
		}

		/**
		 * @note: this REQUIRES selection dag to call sort() before live range analysis
		 * to establish topological ordering. This is guaranteed by the instruction selection
		 * integration which calls sort() during pattern matching.
		 */
		std::pair<std::uint32_t, std::uint32_t> compute_live_range(dag_node *node) const
		{
			/* compute live range as def position to last use position within the
			 * DAG. this is a simplified analysis that works well with the hierarchical
			 * approach because region boundaries naturally limit interference scope */
			std::uint32_t def_pos = node->value_id;
			std::uint32_t last_use = def_pos;

			for (auto *user: node->users)
				last_use = std::max(last_use, user->value_id);
			return { def_pos, last_use };
		}

		float compute_node_complexity(dag_node *node) const
		{
			/* assign complexity weights based on operation cost. more expensive
			 * operations benefit more from being kept in registers rather than
			 * spilled, so they get higher priority in allocation decisions */
			switch (node->source->ir_type)
			{
				case NodeType::MUL:
					return 3.0f;
				case NodeType::DIV:
				case NodeType::MOD:
					return 10.0f;
				case NodeType::CALL:
					return 20.0f;
				case NodeType::LOAD:
				case NodeType::PTR_LOAD:
					return 2.0f;
				default:
					return 1.0f;
			}
		}

		std::unordered_map<RegisterClass, std::uint32_t> count_n(const Budget<Arch> &budget) const
		{
			/* extract register counts by class from budget for constraint checking */
			std::unordered_map<RegisterClass, std::uint32_t> counts;
			for (const auto &[cls, regs]: budget.available)
				counts[cls] = regs.size();
			return counts;
		}

		float compute_total_complexity(const Constraints<Arch> &constraints) const
		{
			/* sum complexity across all register classes to get total computational
			 * complexity for this region hierarchy. used for proportional budgeting */
			float total = 0.0f;
			for (const auto &[cls, weight]: constraints.complexity)
				total += weight;
			return total;
		}

		float compute_parent_complexity(const Constraints<Arch> &constraints, Region *region) const
		{
			if (region->children().empty())
				return compute_total_complexity(constraints);
			/* estimate what fraction of total complexity belongs to the parent region
			 * versus child regions. this is used for proportional budget allocation
			 * between parent and children */
			return compute_total_complexity(constraints) * 0.3f;
		}

		Budget<Arch> allocate_parent_budget(Budget<Arch> &available, float ratio) const
		{
			/* allocate a portion of available registers to the parent region based
			 * on complexity ratio. remaining registers will be distributed to children */
			Budget<Arch> parent;
			parent.complexity_ratio = ratio;

			for (const auto &[cls, regs]: available.available)
			{
				auto parent_count = static_cast<std::uint32_t>(regs.size() * ratio);
				auto reg_iter = regs.begin();
				for (std::uint32_t i = 0; i < parent_count && reg_iter != regs.end(); ++i, ++reg_iter)
				{
					parent.available[cls].insert(*reg_iter);
				}
			}

			return parent;
		}

		Budget<Arch> compute_remaining(const Budget<Arch> &total, const Budget<Arch> &used) const
		{
			/* compute remaining budget after parent allocation for distribution to children */
			Budget<Arch> remaining;

			for (const auto &[cls, total_regs]: total.available)
			{
				auto &used_regs = used.available.at(cls);
				for (register_type reg: total_regs)
				{
					if (used_regs.find(reg) == used_regs.end())
						remaining.available[cls].insert(reg);
				}
			}

			return remaining;
		}

		void allocate_children(Region *region, Budget<Arch> &remaining)
		{
			/* recursively allocate registers for child regions using remaining budget */
			for (Region *child: region->children())
			{
				if (auto it = region_constraints.find(child);
					it != region_constraints.end())
				{
					allocate(child, remaining);
				}
			}
		}

		std::vector<dag_node *> identify_spill_candidates(Region *region) const
		{
			/* identify values that are good candidates for spilling based on usage
			 * patterns. values with single uses or in non-critical paths are preferred
			 * because spilling them has less performance impact */
			auto local_nodes = nodes(region);
			std::vector<dag_node *> candidates;

			for (auto *node: local_nodes)
			{
				/* single-use values are good spill candidates because the reload
				 * can often be combined with the use instruction, reducing overhead */
				if (needs_allocation(node) && node->users.size() <= 1)
					candidates.push_back(node);
			}

			return candidates;
		}

		void mark_for_spill(dag_node *node)
		{
			/* mark a value for spilling. the register allocator only makes the
			 * decision about what to spill; actual spill code generation (stack
			 * slot allocation, spill/reload instructions) is handled by the code
			 * generator that runs after instruction selection */
			if (!node)
				return;

			Result<Arch> result;
			result.spilled = true;
			allocations[node] = result;
		}

		bool is_available(const Budget<Arch> &budget, RegisterClass cls, register_type reg) const
		{
			/* check if a specific register is available for allocation in this budget */
			auto cls_it = budget.available.find(cls);
			return cls_it != budget.available.end() && cls_it->second.contains(reg);
		}

		void allocate_specific(dag_node *node, register_type reg, RegisterClass cls, Budget<Arch> &budget)
		{
			/* allocate a specific register to a DAG node and update budget state */
			allocations[node] = Result<Arch> { .reg = reg };
			budget.available[cls].erase(reg);
			++budget.allocated[cls];

			/* update master budget for the region to keep track of overall pressure */
			Region *node_region = node->source->parent;
			auto &master_budget = region_budgets[node_region];
			master_budget.available[cls].erase(reg);
			++master_budget.allocated[cls];
		}

		void allocate_regular(dag_node *node, Budget<Arch> &budget)
		{
			/* allocate any available register for a value that doesn't have special
			 * requirements or optimization opportunities like FROM nodes do */
			RegisterClass cls = infer_class(node->value_t);
			auto &available_regs = budget.available[cls];
			if (!available_regs.empty())
			{
				/* take the first available register. in a more sophisticated
				 * implementation, this could consider register preferences based
				 * on instruction encoding constraints or calling conventions */
				register_type reg = *available_regs.begin();
				allocate_specific(node, reg, cls, budget);
				auto stored_result = allocations[node];
			}
			else
			{
				/* no registers available. we mark for spilling. the code generator
				 * will handle actual spill slot allocation and reload generation */
				mark_for_spill(node);
			}
		}

		Result<Arch> perform_allocation(Region *region, const Request<Arch> &req)
		{
			/* perform actual register allocation for a node given instruction selector
			 * preferences and constraints. this implements the policy for choosing
			 * between hint registers, forbidden registers, and spilling */
			auto &budget = region_budgets[region];
			auto &available_regs = budget.available[req.cls];

			Result<Arch> result;

			/* try hint register first because hints come from instruction selection
			 * constraints like FROM node sources or target-specific preferences
			 * that can improve code quality or eliminate move instructions */
			if (req.hint && available_regs.contains(*req.hint))
			{
				bool forbidden = std::find(req.forbidden.begin(), req.forbidden.end(),
				                           *req.hint) != req.forbidden.end();
				if (!forbidden)
				{
					result.reg = *req.hint;
					available_regs.erase(*req.hint);
					++budget.allocated[req.cls];
					return result;
				}
			}

			/* find any available register that isn't forbidden by instruction
			 * encoding constraints or other architectural limitations */
			for (auto reg_id: available_regs)
			{
				const bool forbidden = std::find(req.forbidden.begin(), req.forbidden.end(),
				                                 reg_id) != req.forbidden.end();
				if (!forbidden)
				{
					result.reg = reg_id;
					available_regs.erase(reg_id);
					++budget.allocated[req.cls];
					return result;
				}
			}

			/* no suitable register found so we just spill if allowed by the request.
			 * the instruction selector might disallow spilling for certain
			 * critical values that must be in registers for correctness */
			if (req.allow_spill)
				result.spilled = true;

			return result;
		}
	};
}
