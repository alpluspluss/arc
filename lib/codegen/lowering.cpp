/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <queue>
#include <arc/codegen/lowering.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/foundation/region.hpp>
#include <arc/support/algorithm.hpp>
#include <arc/support/inference.hpp>

namespace arc
{
	namespace
	{
		bool needs_lowering(const Node* node)
		{
			if (!node)
				return false;

			switch (node->ir_type)
			{
				case NodeType::ACCESS:
					return true;
				default:
					return false;
			}
		}

		std::uint64_t compute_struct_field_offset(Node* struct_node, std::uint64_t field_index)
		{
			if (!struct_node || struct_node->type_kind != DataType::STRUCT)
				return 0;

			if (struct_node->value.type() != DataType::STRUCT)
				return 0;

			const auto& struct_data = struct_node->value.get<DataType::STRUCT>();
			std::uint64_t offset = 0;
			std::uint64_t actual_field_count = 0;

			for (const auto& [name_id, field_type, field_data] : struct_data.fields)
			{
				if (struct_node->parent)
				{
					Module& mod = struct_node->parent->module();
					std::string field_name(mod.strtable().get(name_id));
					if (field_name.starts_with("__pad"))
					{
						offset += elem_sz(field_type);
						continue;
					}
				}

				if (actual_field_count == field_index)
					return offset;

				offset += elem_sz(field_type);
				actual_field_count++;
			}

			return offset;
		}

		Node* create_addr_of_node(Node* container, Region* parent_region, Node* insert_before)
		{
			ach::shared_allocator<Node> alloc;
			Node* addr_node = alloc.allocate(1);
			std::construct_at(addr_node);

			addr_node->ir_type = NodeType::ADDR_OF;
			addr_node->type_kind = DataType::POINTER;
			addr_node->parent = parent_region;
			addr_node->inputs.push_back(container);
			container->users.push_back(addr_node);

			DataTraits<DataType::POINTER>::value ptr_data = {};
			ptr_data.pointee = container;
			ptr_data.addr_space = 0;
			addr_node->value.set<decltype(ptr_data), DataType::POINTER>(ptr_data);

			parent_region->insert_before(insert_before, addr_node);
			return addr_node;
		}

		Node* create_ptr_add_node(Node* base_addr, Node* offset_node, Region* parent_region, Node*)
		{
			ach::shared_allocator<Node> alloc;
			Node* ptr_add = alloc.allocate(1);
			std::construct_at(ptr_add);

			ptr_add->ir_type = NodeType::PTR_ADD;
			ptr_add->type_kind = base_addr->type_kind;
			ptr_add->parent = parent_region;

			ptr_add->inputs.push_back(base_addr);
			ptr_add->inputs.push_back(offset_node);
			base_addr->users.push_back(ptr_add);
			offset_node->users.push_back(ptr_add);

			if (base_addr->value.type() == DataType::POINTER)
				ptr_add->value = base_addr->value;

			/* we don't insert it here; the caller will do that via `Region::replace` */
			return ptr_add;
		}

		Node* create_mul_node(Node* index_node, Node* element_size_lit, Region* parent_region, Node* insert_before)
		{
			ach::shared_allocator<Node> alloc;
			Node* mul_node = alloc.allocate(1);
			std::construct_at(mul_node);

			mul_node->ir_type = NodeType::MUL;
			mul_node->type_kind = index_node->type_kind;
			mul_node->parent = parent_region;

			mul_node->inputs.push_back(index_node);
			mul_node->inputs.push_back(element_size_lit);
			index_node->users.push_back(mul_node);
			element_size_lit->users.push_back(mul_node);

			parent_region->insert_before(insert_before, mul_node);
			return mul_node;
		}
	}

	std::string IRLoweringPass::name() const
	{
		return "ir-lowering";
	}

	std::vector<std::string> IRLoweringPass::invalidates() const
	{
		/* and probably most analysis passes */
		return { "type-based-alias-analysis" };
	}

	std::vector<Region*> IRLoweringPass::run(Module& module, PassManager& pm)
	{
		lowered_nodes.clear();
		std::vector<Region*> modified_regions;

		if (const std::size_t lowered_count = process_module(module);
			lowered_count > 0)
		{
			std::queue<Region*> region_worklist;
			region_worklist.push(module.root());

			while (!region_worklist.empty())
			{
				Region* current = region_worklist.front();
				region_worklist.pop();

				bool region_modified = false;
				for (const auto& [original, lowered] : lowered_nodes)
				{
					if (original->parent == current)
					{
						region_modified = true;
						break;
					}
				}

				if (region_modified)
					modified_regions.push_back(current);

				for (Region* child : current->children())
					region_worklist.push(child);
			}
		}

		return modified_regions;
	}

	std::size_t IRLoweringPass::process_module(Module& module)
	{
		std::size_t total_lowered = 0;

		total_lowered += process_region(module.root());

		for (const Node* func_node : module.functions())
		{
			if (func_node->ir_type != NodeType::FUNCTION)
				continue;

			const std::string_view func_name = module.strtable().get(func_node->str_id);
			for (Region* child : module.root()->children())
			{
				if (child->name() == func_name)
				{
					total_lowered += process_region(child);
					break;
				}
			}
		}

		return total_lowered;
	}

	std::size_t IRLoweringPass::process_region(Region* region)
	{
		if (!region)
			return 0;

		std::size_t lowered_count = 0;
		std::queue<Region*> worklist;
		worklist.push(region);

		while (!worklist.empty())
		{
			Region* current_region = worklist.front();
			worklist.pop();

			/* create a copy of nodes since we'll be modifying the region during iteration */
			for (auto nodes_to_process = current_region->nodes();
			     Node* node : nodes_to_process)
			{
				if (needs_lowering(node))
				{
					if (Node* lowered = lower_node(node);
						lowered && lowered != node)
					{
						/* just update all use-def connections;
						 * replacing would be pointless and buggy because all
						 * intermediate nodes are already inserted via `Region::insert_before` */
						current_region->replace(node, lowered, false);
						update_all_connections(node, lowered);
						lowered_nodes[node] = lowered;
						lowered_count++;
					}
				}
			}

			for (Region* child : current_region->children())
				worklist.push(child);
		}

		return lowered_count;
	}

	Node* IRLoweringPass::lower_node(Node* node)
	{
		if (!node)
			return nullptr;

		switch (node->ir_type)
		{
			case NodeType::ACCESS:
				return lower_access_node(node);
			default:
				return node;
		}
	}

	Node* IRLoweringPass::lower_access_node(Node* access_node)
	{
	    if (access_node->inputs.size() < 2)
	        return access_node;

	    Node* container = access_node->inputs[0];
	    Node* index_node = access_node->inputs[1];

	    if (!container || !index_node)
	        return access_node;

	    Node* base_addr = nullptr;
	    Node* offset_node = nullptr;

	    if (container->ir_type == NodeType::ADDR_OF ||
	        container->type_kind == DataType::POINTER ||
	        container->ir_type == NodeType::PTR_ADD)
	    {
	        base_addr = container;
	    	if (index_node->ir_type == NodeType::LIT)
	        {
	            const auto index = static_cast<std::uint64_t>(extract_literal_value(index_node));
	            std::uint64_t byte_offset = 0;
	    		if (access_node->type_kind != DataType::VOID)
	            {
	                byte_offset = index * elem_sz(access_node->type_kind);
	            }

	            offset_node = create_literal_node(static_cast<std::int64_t>(byte_offset), index_node->type_kind);
	        }
	    }
	    else
	    {
	        base_addr = create_addr_of_node(container, access_node->parent, access_node);

	        if (container->type_kind == DataType::STRUCT)
	        {
	            if (index_node->ir_type == NodeType::LIT)
	            {
	                const auto field_index = static_cast<std::uint64_t>(extract_literal_value(index_node));
	                const std::uint64_t byte_offset = compute_struct_field_offset(container, field_index);
	                offset_node = create_literal_node(static_cast<std::int64_t>(byte_offset), index_node->type_kind);
	            }
	        }
	        else if (container->type_kind == DataType::ARRAY)
	        {
	            if (container->value.type() != DataType::ARRAY)
	                return access_node;

	            const auto& array_data = container->value.get<DataType::ARRAY>();
	            const std::uint64_t element_size = elem_sz(array_data.elem_type);

	            if (index_node->ir_type == NodeType::LIT)
	            {
	                const std::int64_t index = extract_literal_value(index_node);
	                const std::uint64_t byte_offset = static_cast<std::uint64_t>(index) * element_size;
	                offset_node = create_literal_node(static_cast<std::int64_t>(byte_offset), index_node->type_kind);
	            }
	            else
	            {
	                Node* element_size_lit = create_literal_node(static_cast<std::int64_t>(element_size), index_node->type_kind);
	                offset_node = create_mul_node(index_node, element_size_lit, access_node->parent, access_node);
	            }
	        }
	    }

	    if (!offset_node)
	        return access_node;

		offset_node->parent = access_node->parent;
	    return create_ptr_add_node(base_addr, offset_node, access_node->parent, access_node);
	}

	Node* IRLoweringPass::create_literal_node(std::int64_t value, DataType type)
	{
		ach::shared_allocator<Node> alloc;
		Node* lit_node = alloc.allocate(1);
		std::construct_at(lit_node);

		lit_node->ir_type = NodeType::LIT;
		lit_node->type_kind = type;

		switch (type)
		{
			case DataType::INT8:
				lit_node->value.set<std::int8_t, DataType::INT8>(static_cast<std::int8_t>(value));
				break;
			case DataType::INT16:
				lit_node->value.set<std::int16_t, DataType::INT16>(static_cast<std::int16_t>(value));
				break;
			case DataType::INT32:
				lit_node->value.set<std::int32_t, DataType::INT32>(static_cast<std::int32_t>(value));
				break;
			case DataType::INT64:
				lit_node->value.set<std::int64_t, DataType::INT64>(value);
				break;
			case DataType::UINT8:
				lit_node->value.set<std::uint8_t, DataType::UINT8>(static_cast<std::uint8_t>(value));
				break;
			case DataType::UINT16:
				lit_node->value.set<std::uint16_t, DataType::UINT16>(static_cast<std::uint16_t>(value));
				break;
			case DataType::UINT32:
				lit_node->value.set<std::uint32_t, DataType::UINT32>(static_cast<std::uint32_t>(value));
				break;
			case DataType::UINT64:
				lit_node->value.set<std::uint64_t, DataType::UINT64>(static_cast<std::uint64_t>(value));
				break;
			default:
				lit_node->type_kind = DataType::UINT64;
				lit_node->value.set<std::uint64_t, DataType::UINT64>(static_cast<std::uint64_t>(value));
				break;
		}

		return lit_node;
	}
}
