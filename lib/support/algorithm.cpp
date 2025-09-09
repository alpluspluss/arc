/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/foundation/node.hpp>
#include <arc/support/algorithm.hpp>

namespace arc
{
	bool update_connection(Node* node, Node* old_input, Node* new_input)
	{
		if (!node || !old_input || !new_input)
			return false;

		/* search for old_input in node's input list */
		for (std::size_t i = 0; i < node->inputs.size(); ++i)
		{
			if (node->inputs[i] == old_input)
			{
				/* remove node from old_input's user list then update the input connection;
				 * after that add node to new_input's user list if not already present */
				erase(old_input->users, node);
				node->inputs[i] = new_input;
				if (std::ranges::find(new_input->users, node) == new_input->users.end())
					new_input->users.push_back(node);

				return true;
			}
		}

		return false;
	}

	std::size_t update_all_connections(Node* old_node, Node* new_node)
	{
		if (!old_node || !new_node)
			return 0;

		std::size_t updated_count = 0;

		/* iterate over a copy of the users list since we'll be modifying it */
		for (const auto users_copy = old_node->users;
		     Node* user : users_copy)
		{
			if (update_connection(user, old_node, new_node))
				updated_count++;
		}

		/* clear old_node's user list since all connections have been transferred */
		old_node->users.clear();
		return updated_count;
	}

	std::int64_t extract_literal_value(Node* node)
	{
		if (!node || node->ir_type != NodeType::LIT)
			return 0;

		switch (node->type_kind)
		{
			case DataType::INT8:
				return node->value.get<DataType::INT8>();
			case DataType::INT16:
				return node->value.get<DataType::INT16>();
			case DataType::INT32:
				return node->value.get<DataType::INT32>();
			case DataType::INT64:
				return node->value.get<DataType::INT64>();
			case DataType::UINT8:
				return node->value.get<DataType::UINT8>();
			case DataType::UINT16:
				return node->value.get<DataType::UINT16>();
			case DataType::UINT32:
				return node->value.get<DataType::UINT32>();
			case DataType::UINT64:
				return static_cast<std::int64_t>(node->value.get<DataType::UINT64>());
			default:
				return 0;
		}
	}
}
