/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <arc/foundation/pass.hpp>
#include <arc/foundation/typed-data.hpp>

namespace arc
{
	class Module;
	class PassManager;
	struct Node;

	/**
	 * @brief Result of type-based alias analysis between two memory accesses
	 */
	enum class TBAAResult : std::uint8_t
	{
		/** @brief Memory accesses never alias */
		NO_ALIAS,
		/** @brief Memory accesses definitely alias the same location */
		MUST_ALIAS,
		/** @brief Memory accesses may alias - cannot determine precisely */
		MAY_ALIAS,
		/** @brief Memory accesses partially overlap */
		PARTIAL_ALIAS
	};

	/**
	 * @brief Represents a memory location with allocation site and offset
	 */
	struct MemoryLocation
	{
		/** @brief Base allocation site (ALLOC node) */
		Node* allocation_site = nullptr;
		/** @brief Byte offset from allocation base (-1 if unknown) */
		std::int64_t offset = -1;
		/** @brief Access size in bytes */
		std::uint64_t size = 0;
		/** @brief Type being accessed */
		DataType access_type = DataType::VOID;

		bool operator==(const MemoryLocation& other) const
		{
			return allocation_site == other.allocation_site &&
			       offset == other.offset &&
			       size == other.size &&
			       access_type == other.access_type;
		}
	};

	/**
	 * @brief Hash function for MemoryLocation
	 */
	struct MemoryLocationHash
	{
		std::size_t operator()(const MemoryLocation& loc) const
		{
			std::size_t h = std::hash<Node*>{}(loc.allocation_site);
			h = h * 31 + std::hash<std::int64_t>{}(loc.offset);
			h = h * 31 + std::hash<std::uint64_t>{}(loc.size);
			h = h * 31 + std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(loc.access_type));
			return h;
		}
	};

	/**
	 * @brief Check for overlap between two memory ranges
	 * @param loc1 First memory location
	 * @param loc2 Second memory location
	 * @return TBAA result for the overlap analysis
	 */
	TBAAResult check_memory_overlap(const MemoryLocation& loc1, const MemoryLocation& loc2);

	/**
	 * @brief Result class for type-based alias analysis
	 */
	class TypeBasedAliasResult final : public Analysis
	{
	public:
		[[nodiscard]] std::string name() const override
		{
			return "type-based-alias-analysis";
		}

		/**
		 * @brief Update analysis results incrementally for modified regions
		 * @param modified_regions Regions that were modified
		 * @return true if incremental update succeeded, false if full recomputation needed
		 */
		bool update(const std::vector<Region*>& modified_regions) override;

		/**
		 * @brief Check aliasing relationship between two memory accesses
		 * @param access1 First memory access node
		 * @param access2 Second memory access node
		 * @return TBAA result indicating aliasing relationship
		 */
		TBAAResult alias(Node* access1, Node* access2) const;

		/**
		 * @brief Check if two memory accesses may alias
		 * @param access1 First memory access node
		 * @param access2 Second memory access node
		 * @return true if accesses may alias
		 */
		bool may_alias(Node* access1, Node* access2) const
		{
			const TBAAResult result = alias(access1, access2);
			return result == TBAAResult::MAY_ALIAS ||
			       result == TBAAResult::MUST_ALIAS ||
			       result == TBAAResult::PARTIAL_ALIAS;
		}

		/**
		 * @brief Check if two memory accesses definitely do not alias
		 * @param access1 First memory access node
		 * @param access2 Second memory access node
		 * @return true if accesses definitely do not alias
		 */
		bool no_alias(Node* access1, Node* access2) const
		{
			return alias(access1, access2) == TBAAResult::NO_ALIAS;
		}

		/**
		 * @brief Add a memory access with its computed location
		 * @param access Memory access node
		 * @param location Memory location information
		 */
		void add_memory_access(Node* access, const MemoryLocation& location);

		/**
		 * @brief Get memory location for an access
		 * @param access Memory access node
		 * @return Pointer to memory location, or nullptr if not found
		 */
		const MemoryLocation* memory_location(Node* access) const;

		/**
		 * @brief Register an allocation site
		 * @param alloc_node ALLOC node
		 * @param size Allocation size in bytes
		 */
		void add_allocation_site(Node* alloc_node, std::uint64_t size);

		/**
		 * @brief Check if a node is a tracked allocation site
		 * @param node Node to check
		 * @return true if node is an allocation site
		 */
		bool is_allocation_site(Node* node) const;

		/**
		 * @brief Get all tracked memory accesses
		 * @return Vector of memory access nodes
		 */
		[[nodiscard]] const std::vector<Node*>& memory_accesses() const;

		/**
		 * @brief Mark an allocation site as escaped
		 */
		void mark_escaped(Node* allocation_site);

		/**
		 * @brief Check if allocation has escaped
		 */
		bool has_escaped(Node* allocation_site) const;

	private:
		std::unordered_map<Node*, MemoryLocation> access_locations;
		std::unordered_set<Node*> allocation_sites;
		std::unordered_set<Node*> escaped_allocations;
		std::unordered_map<Node*, std::uint64_t> allocation_sizes;
		std::vector<Node*> mem_accesses;
	};

	/**
	 * @brief Type-based alias analysis pass
	 *
	 * Performs alias analysis using allocation sites, pointer arithmetic tracking,
	 * and type information to determine precise aliasing relationships.
	 */
	class TypeBasedAliasAnalysisPass final : public AnalysisPass
	{
	public:
		/**
		 * @brief Get the pass name
		 * @return Pass identifier for dependency resolution
		 */
		[[nodiscard]] std::string name() const override;

		/**
		 * @brief Get the list of passes this pass depends on
		 * @return Vector of required pass names
		 */
		[[nodiscard]] std::vector<std::string> require() const override;

		/**
		 * @brief Run type-based alias analysis on the module
		 * @param module Module to analyze
		 * @return TBAA analysis result
		 */
		Analysis* run(const Module& module) override;

	private:
		/**
		 * @brief Analyze a function for memory accesses and allocations
		 * @param result TBAA result to populate
		 * @param func Function node to analyze
		 * @param module Module containing the function
		 */
		void analyze_function(TypeBasedAliasResult* result, Node* func, Module& module);

		/**
		 * @brief Analyze a region for memory operations
		 * @param result TBAA result to populate
		 * @param region Region to analyze
		 */
		void analyze_region(TypeBasedAliasResult* result, Region* region);

		/**
		 * @brief Analyze a single node
		 * @param result TBAA result to populate
		 * @param node Node to analyze
		 */
		void analyze_node(TypeBasedAliasResult* result, Node* node);

		/**
		 * @brief Handle memory access node (load/store)
		 * @param result TBAA result to populate
		 * @param node Memory access node
		 */
		void handle_memory_access(TypeBasedAliasResult* result, Node* node);

		/**
		 * @brief Compute memory location for an access
		 * @param node Memory access node
		 * @return Memory location information
		 */
		MemoryLocation compute_memory_location(Node* node) const;

		/**
		 * @brief Trace pointer arithmetic to find base allocation and offset
		 * @param pointer Pointer node
		 * @param offset Output parameter for computed offset
		 * @return Base allocation node, or nullptr if cannot determine
		 */
		Node* trace_pointer_base(Node* pointer, std::int64_t& offset) const;

		/**
		 * @brief Extract integer literal value from a node
		 * @param node Node to extract from
		 * @return Integer value, or 0 if not a literal
		 */
		static std::int64_t extract_literal_value(Node* node) ;

		/**
		 * @brief Check if a node represents a memory access operation
		 * @param node Node to check
		 * @return true if node is a memory access
		 */
		static bool is_memory_access(Node* node);
	};
}
