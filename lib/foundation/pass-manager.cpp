/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/foundation/region.hpp>

namespace arc
{
	void PassManager::run(Module &module)
	{
		for (Pass *pass: passes)
		{
			validate_dependencies(pass);
			if (auto *analysis = dynamic_cast<AnalysisPass *>(pass))
				run_analysis(analysis, module);
			else if (auto *transform = dynamic_cast<TransformPass *>(pass))
				run_transform(transform, module);
		}
	}

	bool PassManager::has_analysis(const std::string &name) const
	{
		return analyses.contains(name);
	}

	std::size_t PassManager::pass_count() const
	{
		return passes.size();
	}

	void PassManager::clear_analyses()
	{
		/* the allocator with clean the whole thing up when the thread lifetime ends,
		 * so we just clear the map */
		analyses.clear();
	}

	void PassManager::invalidate_analyses(const std::vector<Region *> &modified_regions,
	                                      const std::vector<std::string> &invalidated_analyses)
	{
		for (const std::string &analysis_name: invalidated_analyses)
		{
			if (auto it = analyses.find(analysis_name);
				it != analyses.end())
			{
				Analysis *analysis = it->second;
				if (!analysis)
				{
					analyses.erase(it);
					continue;
				}

				/* attempt to update the analysis incrementally;
				 * if it fails, just do a full invalidation by removing it
				 * from the cache entirely */
				if (!analysis->update(modified_regions))
					analyses.erase(it);

				/* if `Analysis::update` returns true, it can stay cache with updated results */
			}
		}
	}

	void PassManager::validate_dependencies(const Pass *pass) const
	{
		for (const auto required = pass->require();
		     const std::string &dep: required)
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
			if (dynamic_cast<const AnalysisPass *>(pass_it->second))
			{
				if (!has_analysis(dep))
				{
					throw std::runtime_error(
						std::format("pass '{}' requires analysis '{}' which hasn't been run",
						            pass->name(), dep)
					);
				}
			}
		}
	}

	void PassManager::run_analysis(AnalysisPass *analysis, const Module &module)
	{
		/* skip if already cached */
		if (has_analysis(analysis->name()))
			return;

		if (Analysis *result = analysis->run(module))
			analyses[analysis->name()] = result;
		else
		{
			throw std::runtime_error(
				std::format("analysis '{}' returned null result", analysis->name())
			);
		}
	}

	void PassManager::run_transform(TransformPass *transform, Module &module)
	{
		if (const std::vector<Region *> modified_regions = transform->run(module, *this);
			!modified_regions.empty())
		{
			invalidate_analyses(modified_regions, transform->invalidates());
		}
	}
}
