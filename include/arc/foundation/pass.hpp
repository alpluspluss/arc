/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <string>
#include <vector>

namespace arc
{
	class Module;
	class Region;
	class PassManager;

	/**
	 * @brief Base class for all analysis results
	 */
	class Analysis
	{
	public:
		virtual ~Analysis() = default;

		/**
		 * @brief Update analysis results incrementally for modified regions
		 * @return true if incremental update succeeded, false if full recomputation needed
		 */
		virtual bool update(const std::vector<Region *>&)
		{
			return false;
		}
	};

	/**
	 * @brief Base class for all passes
	 */
	class Pass
	{
	public:
		virtual ~Pass() = default;

		/**
		 * @brief Get the unique name of this pass
		 * @return Pass name used for dependency resolution
		 */
		[[nodiscard]] virtual std::string name() const = 0;

		/**
		 * @brief Get the list of passes this pass depends on
		 * @return Vector of pass names that must run before this pass
		 */
		[[nodiscard]] virtual std::vector<std::string> require() const
		{
			return {};
		}

		/**
		 * @brief Get the list of analyses this pass invalidates
		 * @return Vector of analysis names that become stale after this pass
		 */
		[[nodiscard]] virtual std::vector<std::string> invalidates() const
		{
			return {};
		}
	};

	/**
	 * @brief Base class for analysis passes that produce cached results
	 */
	class AnalysisPass : public Pass
	{
	public:
		/**
		 * @brief Run the analysis on a module
		 * @param module Module to analyze
		 * @return Raw pointer to analysis result
		 */
		virtual Analysis *run(const Module &module) = 0;
	};

	/**
	 * @brief Base class for transform passes that modify IR
	 */
	class TransformPass : public Pass
	{
	public:
		/**
		 * @brief Run the transform on a module
		 * @param module Module to transform
		 * @param pm Pass manager for accessing cached analyses
		 * @return Vector of regions that were modified by this transform
		 */
		virtual std::vector<Region *> run(Module &module, PassManager &pm) = 0;
	};

	/**
	 * @brief Concept to check if a type is an analysis pass
	 */
	template<typename T>
	concept AnalysisType = std::derived_from<T, AnalysisPass>;

	/**
	 * @brief Concept to check if a type is any kind of pass
	 */
	template<typename T>
	concept PassType = std::derived_from<T, Pass>;

	/**
	 * @brief Concept to check if a type is a transform pass
	 */
	template<typename T>
	concept TransformType = std::derived_from<T, TransformPass>;
}
