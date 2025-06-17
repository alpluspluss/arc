/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <string_view>
#include <vector>
#include <arc/foundation/node.hpp>
#include <arc/support/allocator.hpp>

namespace arc
{
	class Region;

	class Module
	{
	public:
		/**
		 * @brief Construct a new module
		 * @param name Name of the module
		 */
		explicit Module(std::string_view name);

		/**
		 * @brief Module destructor
		 */
		~Module();

		Module(const Module &) = delete;

		Module &operator=(const Module &) = delete;

		Module(Module &&) = delete;

		Module &operator=(Module &&) = delete;

		/**
		 * @brief Get the name of the module
		 */
		[[nodiscard]] std::string_view name() const;

		/**
		 * @brief Get the root region of the module
		 */
		[[nodiscard]] Region* root() const;

		/**
		 * @brief Construct a new region in this module
		 *
		 * @param name Name of the region
		 * @param parent Parent region; defaults to the root region
		 * @return Pointer to the created region
		 */
		Region* create_region(std::string_view name, Region* parent = nullptr);

		/**
		 * @brief Find a function by name
		 * @param name Name of the function
		 * @return Pointer to the function node or nullptr if not found
		 */
		[[nodiscard]] Node* find_fn(std::string_view name);

		/**
		 * @brief Add a function node into this module
		 * @param fn Function node to register
		 */
		void add_fn(Node* fn);

		/**
		 * @brief Add a literal node to .rodata region
		 */
		void add_rodata(Node* node);

		/**
		 * @brief Intern a string literal
		 * @param str String to intern
		 * @return String id
		 */
		StringTable::StringId intern_str(std::string_view str);

		/**
		 * @brief Check if this module contains the specified function
		 * @return true if found, otherwise false
		 */
		bool contains(Node* fn);

		/**
		 * @brief Check if this module contains the specified region
		 * @return true if found, otherwise false
		 */
		bool contains(Region* region);

		/**
		 * @brief Get all functions in this module
		 * @return All function nodes
		 */
		[[nodiscard]]
		const std::vector<Node*>& functions() const;

		/**
		 * @brief Get the read-only data region
		 * @return The read-only data section
		 */
		[[nodiscard]]
		Region* rodata() const;

		/**
		 * @brief Get the string table
		 * @return String table
		 */
		StringTable& strtable();

	private:
		std::vector<Node*> fns;
		std::vector<Region*> regions;
		/* the global region; if `Node::parent` is equal
		 * to `Module::root()` then that node belongs to the global scope */
		Region* root_region;
		Region* rodata_region; /* read-only section */
		StringTable strtb;
		StringTable::StringId mod_id;
		/* note: the allocator share the same thread local storage
		 * the point of declaring both is specifically for semantics */
		ach::allocator<Region> region_alloc;
	};
}
