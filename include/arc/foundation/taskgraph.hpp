/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <arc/foundation/pass.hpp>
#include <arc/support/allocator.hpp>

namespace arc
{
	class PassManager;
	enum class ExecutionPolicy;

	struct TaskNode
	{
		Pass* pass;
		std::string name;
		std::vector<std::string> dependencies;
		std::vector<std::string> invalidations;
		std::vector<TaskNode*> depends_on;     /* incoming edges */
		std::vector<TaskNode*> dependents;     /* outgoing edges */
		std::size_t in_degree = 0;             /* for topological sort */
		std::size_t batch_id = 0;              /* execution batch assignment */
	};

	/**
	 * @brief Dependency-aware task graph for parallel pass execution
	 */
	class TaskGraph
	{
	public:
		/**
		 * @brief Construct a new TaskGraph
		 */
		TaskGraph() = default;

		/**
		 * @brief Destructor
		 */
		~TaskGraph() = default;

		/**
		 * @brief Add a pass to the task graph
		 * @tparam T Pass type (must derive from Pass)
		 * @tparam Args Constructor argument types
		 * @param args Arguments to pass to the pass constructor
		 * @return Reference to this TaskGraph for chaining
		 */
		template<PassType T, typename... Args>
		TaskGraph& add(Args&&... args)
		{
			ach::allocator<T> alloc;
			T* pass = alloc.allocate(1);
			std::construct_at(pass, std::forward<Args>(args)...);

			add_pass(pass);
			return *this;
		}

		/**
		 * @brief Build a PassManager with optimal execution order
		 * @param policy Execution policy for the PassManager
		 * @return PassManager configured with dependency-aware execution
		 */
		PassManager build(ExecutionPolicy policy = ExecutionPolicy::SEQUENTIAL);

		/**
		 * @brief Get the number of registered passes
		 * @return Number of passes in the task graph
		 */
		[[nodiscard]] std::size_t pass_count() const;

		/**
		 * @brief Get execution batches for debugging/visualization
		 * @return Vector of batches, each containing pass names that can run in parallel
		 */
		[[nodiscard]] std::vector<std::vector<std::string>> get_execution_batches() const;

		/**
		 * @brief Validate that all dependencies can be resolved
		 * @throws std::runtime_error if circular dependencies or missing passes found
		 */
		void validate() const;

		/**
		 * @brief Build dependency edges between task nodes
		 */
		void build_dependencies();

		/**
		 * @brief Perform topological sort and assign execution batches
		 * @return Vector of execution batches
		 */
		std::vector<std::vector<TaskNode*>> compute_execution_batches();

	private:
		std::vector<TaskNode*> nodes;
		std::unordered_map<std::string, TaskNode*> name_to_node;

		/**
		 * @brief Add a pass to the internal graph structure
		 * @param pass Pass to add
		 */
		void add_pass(Pass* pass);

		/**
		 * @brief Check for circular dependencies using DFS
		 * @throws std::runtime_error if cycles found
		 */
		void check_for_cycles() const;

		/**
		 * @brief DFS helper for cycle detection
		 * @param node Current node
		 * @param visited Visited nodes
		 * @param in_stack Nodes in current DFS stack
		 */
		void dfs_cycle_check(const TaskNode* node,
		                    std::unordered_set<const TaskNode*>& visited,
		                    std::unordered_set<const TaskNode*>& in_stack) const;

		friend class PassManager;
	};
}
