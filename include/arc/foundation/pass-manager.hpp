/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <format>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass.hpp>
#include <arc/foundation/region.hpp>
#include <arc/support/allocator.hpp>

namespace arc
{
	class TaskGraph;

	/**
	 * @brief Execution policy for pass manager
	 */
	enum class ExecutionPolicy
	{
		SEQUENTIAL,  /* run passes one by one */
		PARALLEL     /* run independent passes concurrently */
	};

	/**
	 * @brief Pass manager for running optimization and analysis passes
	 */
	class PassManager
	{
	public:
		/**
		 * @brief Construct a new PassManager
		 * @param policy Execution policy (defaults to sequential)
		 */
		explicit PassManager(ExecutionPolicy policy = ExecutionPolicy::SEQUENTIAL);

		/**
		 * @brief Construct from a TaskGraph with dependency-aware execution
		 * @param task_graph TaskGraph to build execution plan from
		 * @param policy Execution policy (defaults to parallel for TaskGraph)
		 */
		explicit PassManager(TaskGraph&& task_graph,
		                    ExecutionPolicy policy = ExecutionPolicy::PARALLEL);

		/**
		 * @brief Destructor
		 */
		~PassManager() = default;

		PassManager(const PassManager&) = delete;
		PassManager& operator=(const PassManager&) = delete;
		PassManager(PassManager&&) = delete;
		PassManager& operator=(PassManager&&) = delete;

		/**
		 * @brief Add a pass to the execution sequence
		 * @tparam T Pass type (must derive from Pass)
		 * @tparam Args Constructor argument types
		 * @param args Arguments to pass to the pass constructor
		 * @return Reference to this PassManager for chaining
		 */
		template<PassType T, typename... Args>
		PassManager& add(Args&&... args)
		{
			ach::allocator<T> alloc;
			T* pass = alloc.allocate(1);
			std::construct_at(pass, std::forward<Args>(args)...);

			pass_registry[pass->name()] = pass;
			passes.push_back(pass);
			return *this;
		}

		/**
		 * @brief Get a cached analysis result
		 * @tparam T Analysis result type (must derive from Analysis)
		 * @return Reference to the cached analysis result
		 * @throws std::runtime_error if analysis is not available
		 */
		template<typename T>
			requires std::derived_from<T, Analysis>
		const T& get()
		{
			std::shared_lock lock(analyses_mutex); /* OK for concurrent reads */
			for (const auto& [name, analysis] : analyses)
			{
				if (auto* result = dynamic_cast<const T*>(analysis))
				{
					return *result;
				}
			}
			throw std::runtime_error("analysis result not available");
		}

		/**
		 * @brief Run all registered passes on the given module
		 * @param module Module to process
		 */
		void run(Module& module);

		/**
		 * @brief Check if an analysis is available
		 * @param name Name of the analysis
		 * @return true if analysis is cached, false otherwise
		 */
		[[nodiscard]] bool has_analysis(const std::string& name) const;

		/**
		 * @brief Get the number of registered passes
		 * @return Number of passes in the execution sequence
		 */
		[[nodiscard]] std::size_t pass_count() const;

		/**
		 * @brief Clear all cached analyses
		 */
		void clear_analyses();

	private:
		std::unordered_map<std::string, Analysis*> analyses;
		std::unordered_map<std::string, Pass*> pass_registry;
		std::vector<Pass*> passes;
		std::vector<std::vector<Pass*>> execution_batches; /* for TaskGraph mode */
		ExecutionPolicy exec_policy;
		mutable std::shared_mutex analyses_mutex;

		/**
		 * @brief Run passes sequentially
		 * @param module Module to process
		 */
		void run_sequential(Module& module);

		/**
		 * @brief Run passes in parallel batches
		 * @param module Module to process
		 */
		void run_parallel(Module& module);

		/**
		 * @brief Execute a single pass with proper validation
		 * @param pass Pass to execute
		 * @param module Module to process
		 */
		void execute_single_pass(Pass* pass, Module& module);

		/**
		 * @brief Execute a batch of passes using multithreading
		 * @param batch Passes to execute concurrently
		 * @param module Module to process
		 */
		void execute_batch(const std::vector<Pass*>& batch, Module& module);

		/**
		 * @brief Invalidate analyses affected by a transform
		 * @param modified_regions Regions that were modified
		 * @param invalidated_analyses Names of analyses to invalidate
		 */
		void invalidate_analyses(const std::vector<Region*>& modified_regions,
		                        const std::vector<std::string>& invalidated_analyses);

		/**
		 * @brief Check if all required dependencies are available
		 * @param pass Pass to check dependencies for
		 * @throws std::runtime_error if dependencies are missing
		 */
		void validate_dependencies(const Pass* pass) const;

		/**
		 * @brief Run a specific analysis pass and cache the result
		 * @param analysis Analysis pass to run
		 * @param module Module to analyze
		 */
		void run_analysis(AnalysisPass* analysis, const Module& module);

		/**
		 * @brief Run a specific transform pass
		 * @param transform Transform pass to run
		 * @param module Module to transform
		 */
		void run_transform(TransformPass* transform, Module& module);
	};
}
