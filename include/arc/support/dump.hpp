/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <iostream>

namespace arc
{
	class Module;
	class Region;
	struct Node;

	/**
	 * @brief Dump IR to output stream with module context
	 * @param module Module to dump
	 * @param os Output stream (defaults to stdout)
	 */
	void dump(Module& module, std::ostream& os = std::cout);

	/**
	 * @brief Dump region to output stream with module context
	 * @param region Region to dump
	 * @param module Module for context (string table, etc.)
	 * @param os Output stream (defaults to stdout)
	 */
	void dump(Region& region, Module& module, std::ostream& os = std::cout);

	/**
	 * @brief Dump node to output stream with module context
	 * @param node Node to dump
	 * @param module Module for context (string table, etc.)
	 * @param os Output stream (defaults to stdout)
	 */
	void dump(Node& node, Module& module, std::ostream& os = std::cout);

	/**
	 * @brief Debug dump to stderr (convenience functions)
	 */
	void dump_dbg(Module& module);
	void dump_dbg(Region& region, Module& module);
	void dump_dbg(Node& node, Module& module);
}
