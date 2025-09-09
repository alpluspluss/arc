/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <algorithm>
#include <arc/codegen/instruction.hpp>
#include <arc/support/slice.hpp>

namespace arc
{
	/**
	 * @brief Remove all occurrences of a value from a slice
	 * @tparam T Element type
	 * @tparam SizeType Size type of the slice
	 * @param slice Slice to remove from
	 * @param value Value to remove
	 * @return Number of elements removed
	 */
	template<typename T, typename SizeType>
	std::size_t erase(slice<T, SizeType>& slice, const T& value)
	{
		auto new_end = std::remove(slice.begin(), slice.end(), value);
		const std::size_t removed_count = std::distance(new_end, slice.end());
		slice.erase(new_end, slice.end());
		return removed_count;
	}

	/**
	 * @brief Update a node's input connection, maintaining use-def chains
	 *
	 * Replaces old_input with new_input in node's input list and updates
	 * the corresponding use-def chains. If old_input is not found in node's
	 * inputs, no changes are made.
	 *
	 * @param node Node to update connection for
	 * @param old_input Current input node to replace
	 * @param new_input New input node to connect
	 * @return true if connection was updated, false if old_input not found
	 */
	bool update_connection(Node* node, Node* old_input, Node* new_input);

	/**
	 * @brief Replace all uses of old_node with new_node across all users
	 *
	 * Updates all nodes that use old_node as an input to use new_node instead.
	 * Maintains proper use-def chain invariants by updating both input lists
	 * and user lists. After this operation, old_node will have no users.
	 *
	 * @param old_node Node whose uses should be replaced
	 * @param new_node Node to replace old_node with
	 * @return Number of connections updated
	 */
	std::size_t update_all_connections(Node* old_node, Node* new_node);

	template<TargetInstruction T>
	class SelectionDAG;

	/**
	 * @brief Add operand relationship between two DAG nodes
	 * @param user User node
	 * @param operand Operand node
	 */
	template<TargetInstruction T>
	void add_operand(typename SelectionDAG<T>::DAGNode* user, typename SelectionDAG<T>::DAGNode* operand)
	{
		if (!user || !operand)
			return;

		user->operands.push_back(operand);
		operand->users.push_back(user);
	}

	/**
	 * @brief Extract integer value from a literal node
	 * @param node Literal node to extract from
	 * @return Integer value, or 0 if not a literal
	 */
	std::int64_t extract_literal_value(Node *node);
}
