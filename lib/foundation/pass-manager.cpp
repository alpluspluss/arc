/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <exception>
#include <format>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/foundation/region.hpp>
#include <arc/foundation/taskgraph.hpp>

namespace arc
{
	PassManager::PassManager(const ExecutionPolicy policy) : exec_policy(policy) {}

	PassManager::PassManager(TaskGraph&& task_graph, ExecutionPolicy policy) : exec_policy(policy)
	{
		task_graph.validate();
		task_graph.build_dependencies();

		/* convert TaskNode batches to Pass batches and register passes */
		for (const auto batches = task_graph.compute_execution_batches();
		     const auto& batch : batches)
		{
			std::vector<Pass*> pass_batch;
			for (const TaskNode* node : batch)
			{
				Pass* pass = node->pass;
				pass_registry[pass->name()] = pass;
				passes.push_back(pass);
				pass_batch.push_back(pass);
			}
			execution_batches.push_back(std::move(pass_batch));
		}
	}

	void PassManager::run(Module& module)
	{
		if (execution_batches.empty())
		{
			/* standard sequential execution for manually added passes */
			run_sequential(module);
		}
		else
		{
			/* batch execution from TaskGraph */
			if (exec_policy == ExecutionPolicy::PARALLEL)
				run_parallel(module);
			else
				run_sequential(module);
		}
	}

	bool PassManager::has_analysis(const std::string& name) const
	{
		std::shared_lock lock(analyses_mutex);
		return analyses.contains(name);
	}

	std::size_t PassManager::pass_count() const
	{
		return passes.size();
	}

	void PassManager::clear_analyses()
	{
		/* don't deallocate because the allocator will clean up
		 * when thread lifetime ends anyway so we just clear the map */
		std::unique_lock lock(analyses_mutex);
		analyses.clear();
	}

	void PassManager::run_sequential(Module& module)
	{
		if (!execution_batches.empty())
		{
			for (const auto& batch : execution_batches)
			{
				for (Pass* pass : batch)
					execute_single_pass(pass, module);
			}
		}
		else
		{
			for (Pass* pass : passes)
				execute_single_pass(pass, module);
		}
	}

	void PassManager::run_parallel(Module& module)
	{
		for (const auto& batch : execution_batches)
		{
			if (batch.size() == 1)
				execute_single_pass(batch[0], module);
			else
				execute_batch(batch, module);
		}
	}

	void PassManager::execute_single_pass(Pass* pass, Module& module)
	{
		validate_dependencies(pass);
		if (auto* analysis = dynamic_cast<AnalysisPass*>(pass))
			run_analysis(analysis, module);
		else if (auto* transform = dynamic_cast<TransformPass*>(pass))
			run_transform(transform, module);
	}

	void PassManager::execute_batch(const std::vector<Pass*>& batch, Module& module)
	{
		struct WorkerData
		{
			Pass* pass = nullptr;
			std::vector<Region*> modified_regions;
			std::exception_ptr exception;
		};

		std::vector<WorkerData> worker_data(batch.size());
		std::vector<std::thread> workers;

		for (std::size_t i = 0; i < batch.size(); ++i)
			worker_data[i].pass = batch[i];

		for (std::size_t i = 0; i < batch.size(); ++i)
		{
			workers.emplace_back([this, &worker_data, i, &module]()
			{
				try
				{
					Pass* pass = worker_data[i].pass;
					validate_dependencies(pass);
					if (auto* analysis = dynamic_cast<AnalysisPass*>(pass))
					{
						/* worker handles its own locking */
						run_analysis(analysis, module);
					}
					else if (auto* transform = dynamic_cast<TransformPass*>(pass))
					{
						worker_data[i].modified_regions = transform->run(module, *this);
					}
				}
				catch (...)
				{
					worker_data[i].exception = std::current_exception();
				}
			});
		}

		/* wait for all workers to finish */
		for (auto& worker : workers)
			worker.join();

		/* check for exceptions and process results */
		for (const auto&[pass, modified_regions, exception] : worker_data)
		{
			if (exception)
				std::rethrow_exception(exception);

			if (!modified_regions.empty())
			{
				if (const auto* transform = dynamic_cast<TransformPass*>(pass))
					invalidate_analyses(modified_regions, transform->invalidates());
			}
		}
	}

	void PassManager::invalidate_analyses(const std::vector<Region*>& modified_regions,
									  const std::vector<std::string>& invalidated_analyses)
	{
		std::unique_lock lock(analyses_mutex);
		for (const std::string& pass_name : invalidated_analyses)
		{
			/* convert pass name to result name for lookup */
			auto mapping_it = pass_to_result.find(pass_name);
			if (mapping_it == pass_to_result.end())
				continue; /* pass was never run */

			const std::string& result_name = mapping_it->second;
			auto analysis_it = analyses.find(result_name);
			if (analysis_it == analyses.end())
				continue; /* already invalidated */

			Analysis* analysis = analysis_it->second;
			if (!analysis)
			{
				analyses.erase(analysis_it);
				continue;
			}

			/* attempt to update the analysis incrementally;
			 * if it fails, just do a full invalidation by removing it
			 * from the cache entirely */
			if (!analysis->update(modified_regions))
			{
				analyses.erase(analysis_it);
				pass_to_result.erase(mapping_it); /* also remove mapping */
			}
			/* if `Analysis::update` returns true, it can stay cached with updated results */
		}
	}

	void PassManager::validate_dependencies(const Pass* pass) const
	{
		for (const auto required = pass->require(); const std::string& dep : required)
		{
			/* check if dependency is a registered pass */
			auto pass_it = pass_registry.find(dep);
			if (pass_it == pass_registry.end())
			{
				throw std::runtime_error(
					std::format("pass '{}' depends on unknown pass '{}'", pass->name(), dep)
				);
			}

			/* if dependency is an analysis, check if it's been run */
			if (dynamic_cast<const AnalysisPass*>(pass_it->second))
			{
				if (!has_analysis(dep))
				{
					auto mapping_it = pass_to_result.find(dep);
					if (mapping_it == pass_to_result.end() || !has_analysis(mapping_it->second))
					{
						throw std::runtime_error(
							std::format("pass '{}' requires analysis '{}' which hasn't been run",
									   pass->name(), dep)
						);
					}
				}
			}
		}
	}

	void PassManager::run_analysis(AnalysisPass* analysis, const Module& module)
	{
		const std::string pass_name = analysis->name();
		/* skip if already cached */
		if (const auto it = pass_to_result.find(pass_name); it != pass_to_result.end())
		{
			if (has_analysis(it->second))
				return;
		}

		if (Analysis* result = analysis->run(module))
		{
			std::unique_lock lock(analyses_mutex); /* OK for single writer */
			analyses[result->name()] = result;
			pass_to_result[pass_name] = result->name();
		}
		else
		{
			throw std::runtime_error(
				std::format("analysis pass: '{}' returned null result", pass_name)
			);
		}
	}

	void PassManager::run_transform(TransformPass* transform, Module& module)
	{
		if (const std::vector<Region*> modified_regions = transform->run(module, *this);
		   !modified_regions.empty())
		{
			invalidate_analyses(modified_regions, transform->invalidates());
		}
	}
}
