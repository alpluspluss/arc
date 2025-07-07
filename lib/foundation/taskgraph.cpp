/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <algorithm>
#include <format>
#include <queue>
#include <arc/foundation/pass-manager.hpp>
#include <arc/foundation/taskgraph.hpp>

namespace arc
{
	void TaskGraph::add_pass(Pass *pass)
	{
		if (!pass)
			return;

		ach::allocator<TaskNode> alloc;
		TaskNode *node = alloc.allocate(1);
		std::construct_at(node);

		node->pass = pass;
		node->name = pass->name();
		node->dependencies = pass->require();
		node->invalidations = pass->invalidates();

		name_to_node[node->name] = node;
		nodes.push_back(node);
	}

	PassManager TaskGraph::build(ExecutionPolicy policy)
	{
		return PassManager(std::move(*this), policy);
	}

	std::size_t TaskGraph::pass_count() const
	{
		return nodes.size();
	}

	std::vector<std::vector<std::string> > TaskGraph::get_execution_batches() const
	{
		/* create a temporary copy for sorting since this is const */
		TaskGraph temp_graph;
		for (const TaskNode *node: nodes)
			temp_graph.add_pass(node->pass);

		temp_graph.build_dependencies();

		const auto batches = temp_graph.compute_execution_batches();
		std::vector<std::vector<std::string> > result;
		for (const auto &batch: batches)
		{
			std::vector<std::string> batch_names;
			for (const TaskNode *node: batch)
				batch_names.push_back(node->name);

			result.push_back(std::move(batch_names));
		}

		return result;
	}

	void TaskGraph::validate() const
	{
		/* check that all dependencies exist */
		for (TaskNode *node: nodes)
		{
			for (const std::string &dep: node->dependencies)
			{
				if (!name_to_node.contains(dep))
				{
					throw std::runtime_error(
						std::format("pass '{}' depends on unknown pass '{}'", node->name, dep)
					);
				}
			}
		}
		/* then check for any circular dependencies */
		check_for_cycles();
	}

	void TaskGraph::build_dependencies()
	{
		/* reset all dependency connections */
		for (TaskNode *node: nodes)
		{
			node->depends_on.clear();
			node->dependents.clear();
			node->in_degree = 0;
		}

		/* build edges based on dependency declarations */
		for (TaskNode *node: nodes)
		{
			for (const std::string &dep_name: node->dependencies)
			{
				if (auto it = name_to_node.find(dep_name);
					it != name_to_node.end())
				{
					TaskNode *dep_node = it->second;

					/* add edge: dep_node -> node */
					dep_node->dependents.push_back(node);
					node->depends_on.push_back(dep_node);
					node->in_degree++;
				}
			}
		}
	}

	std::vector<std::vector<TaskNode *> > TaskGraph::compute_execution_batches()
	{
		std::vector<std::vector<TaskNode *> > batches;
		std::queue<TaskNode *> ready_queue;

		/* find all nodes with no dependencies */
		for (TaskNode *node: nodes)
		{
			if (node->in_degree == 0)
				ready_queue.push(node);
		}

		std::size_t processed_count = 0;
		std::size_t current_batch = 0;
		while (!ready_queue.empty())
		{
			/* we create a new batch from all currently ready nodes
			 * and reduce in-degree for all dependents */
			std::vector<TaskNode *> current_batch_nodes;
			const std::size_t ready_count = ready_queue.size();
			for (std::size_t i = 0; i < ready_count; ++i)
			{
				TaskNode *node = ready_queue.front();
				ready_queue.pop();
				node->batch_id = current_batch;
				current_batch_nodes.push_back(node);
				processed_count++;
				for (TaskNode *dependent: node->dependents)
				{
					dependent->in_degree--;
					if (dependent->in_degree == 0)
						ready_queue.push(dependent);
				}
			}

			/* sort batch to prioritize analyses over transforms;
			 * this decision is from the fact that read-only passes
			 * are natually more parallelizable and should be run first as transform passes
			 * depend on them */
			std::ranges::sort(current_batch_nodes,
			                  [](TaskNode *a, TaskNode *b)
			                  {
				                  bool a_is_analysis = dynamic_cast<AnalysisPass *>(a->pass) != nullptr;
				                  bool b_is_analysis = dynamic_cast<AnalysisPass *>(b->pass) != nullptr;

				                  if (a_is_analysis && !b_is_analysis)
					                  return true;
				                  if (!a_is_analysis && b_is_analysis)
					                  return false;
				                  return a->name < b->name; /* tie-breaker for deterministic ordering */
			                  });

			batches.push_back(std::move(current_batch_nodes));
			current_batch++;
		}

		if (processed_count != nodes.size())
			throw std::runtime_error("circular dependency detected in task graph");

		return batches;
	}

	void TaskGraph::check_for_cycles() const
	{
		std::unordered_set<const TaskNode *> visited;
		std::unordered_set<const TaskNode *> in_stack;

		for (const TaskNode *node: nodes)
		{
			if (!visited.contains(node))
				dfs_cycle_check(node, visited, in_stack);
		}
	}

	void TaskGraph::dfs_cycle_check(const TaskNode *node,
	                                std::unordered_set<const TaskNode *> &visited,
	                                std::unordered_set<const TaskNode *> &in_stack) const
	{
		visited.insert(node);
		in_stack.insert(node);
		for (const TaskNode *dependent: node->dependents)
		{
			if (in_stack.contains(dependent))
			{
				throw std::runtime_error(
					std::format("circular dependency detected: '{}' -> '{}'", node->name, dependent->name)
				);
			}

			if (!visited.contains(dependent))
				dfs_cycle_check(dependent, visited, in_stack);
		}

		in_stack.erase(node);
	}
}
