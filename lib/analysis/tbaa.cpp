/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/analysis/tbaa.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/node.hpp>
#include <arc/foundation/region.hpp>
#include <arc/support/allocator.hpp>
#include <arc/support/inference.hpp>

namespace arc
{
	static bool types_compatible(DataType type1, DataType type2)
	{
		if (type1 == type2)
			return true;
		return infer_primitive_types(type1, type2) != DataType::VOID;
	}

	Node* access_ptr(Node* access)
	{
		switch (access->ir_type)
		{
			case NodeType::PTR_LOAD:
			case NodeType::ATOMIC_LOAD:
				return access->inputs.empty() ? nullptr : access->inputs[0];
			case NodeType::PTR_STORE:
			case NodeType::ATOMIC_STORE:
				return access->inputs.size() < 2 ? nullptr : access->inputs[1];
			default:
				return nullptr;
		}
	}

	TBAAResult check_memory_overlap(const MemoryLocation &loc1, const MemoryLocation &loc2)
	{
		/* if offsets are unknown, be conservative */
		if (loc1.offset == -1 || loc2.offset == -1)
			return TBAAResult::MAY_ALIAS;

		/* check for exact match */
		if (loc1.offset == loc2.offset && loc1.size == loc2.size && loc1.access_type == loc2.access_type)
			return TBAAResult::MUST_ALIAS;

		/* check for overlap */
		const std::int64_t loc1_end = loc1.offset + static_cast<std::int64_t>(loc1.size);
		const std::int64_t loc2_end = loc2.offset + static_cast<std::int64_t>(loc2.size);

		/* no overlap */
		if (loc1_end <= loc2.offset || loc2_end <= loc1.offset)
			return TBAAResult::NO_ALIAS;

		/* partial overlap */
		if (loc1.offset != loc2.offset || loc1.size != loc2.size)
			return TBAAResult::PARTIAL_ALIAS;

		/* complete overlap with same offset and size but different types */
		if (!types_compatible(loc1.access_type, loc2.access_type))
			return TBAAResult::NO_ALIAS;

		return TBAAResult::MAY_ALIAS;
	}

	bool TypeBasedAliasResult::update(const std::vector<Region*>&)
	{
		/* note: TBAA analysis is based on allocation sites and type information
		 * which should never change during optimization passes
		 *
		 * we are simply returning true here to indicate that we don't need the entire
		 * Result object to be recomputed after optimizations */
		return true;
	}

	TBAAResult TypeBasedAliasResult::alias(Node* access1, Node* access2) const
	{
		if (!access1 || !access2)
			return TBAAResult::MAY_ALIAS;

		/* same access always aliases itself */
		if (access1 == access2)
			return TBAAResult::MUST_ALIAS;

		/* if we can't determine location info, be conservative */
		const MemoryLocation* loc1 = memory_location(access1);
		const MemoryLocation* loc2 = memory_location(access2);
		if (!loc1 || !loc2)
			return TBAAResult::MAY_ALIAS;

		Node* ptr1 = access_ptr(access1);
		Node* ptr2 = access_ptr(access2);
		if ((ptr1 && is_restrict_pointer(ptr1)) || (ptr2 && is_restrict_pointer(ptr2)))
		{
			if (ptr1 != ptr2)
				return TBAAResult::NO_ALIAS;
		}

		/* special allocation site markers
		 * 1. nullptr means global memory or unknown allocation site
		 * 2. ALLOC node means automatic storage allocation
		 * 3. CALL node means function as an allocation site; we don't
		 *		know where exactly it allocates, but we assume it may alias
		 * 4. PARAM node: function parameter as an allocation site;
		 *		we have no info until we have call graph analysis
		 */
		if (loc1->allocation_site != loc2->allocation_site)
		{
			if (loc1->allocation_site && loc2->allocation_site &&
				!has_escaped(loc1->allocation_site) && !has_escaped(loc2->allocation_site))
			{
				return TBAAResult::NO_ALIAS; /* different non-escaped locals never alias */
			}

			if ((ptr1 && is_restrict_pointer(ptr1)) || (ptr2 && is_restrict_pointer(ptr2)))
				return TBAAResult::NO_ALIAS;

			if (!loc1->allocation_site || !loc2->allocation_site)
			{
				if (!types_compatible(loc1->access_type, loc2->access_type))
					return TBAAResult::NO_ALIAS; /* different types definitely don't alias */
				return TBAAResult::MAY_ALIAS; /* same type may alias */
			}

			/* different allocation sites with incompatible types definitely don't alias */
			if (!types_compatible(loc1->access_type, loc2->access_type))
				return TBAAResult::NO_ALIAS;

			/* different allocation sites with compatible types may alias;
			 * we should be conservative for cases like global memory and fn parameters
			 * to avoid undefined behavior */
			return TBAAResult::MAY_ALIAS;
		}
		/* check memory overlap if it's from the same allocation site */
		return check_memory_overlap(*loc1, *loc2);
	}

	void TypeBasedAliasResult::add_memory_access(Node* access, const MemoryLocation& location)
	{
		if (!access)
			return;

		access_locations[access] = location;
		mem_accesses.push_back(access);
	}

	const MemoryLocation* TypeBasedAliasResult::memory_location(Node* access) const
	{
		const auto it = access_locations.find(access);
		return (it != access_locations.end()) ? &it->second : nullptr;
	}

	void TypeBasedAliasResult::add_allocation_site(Node* alloc_node, std::uint64_t size)
	{
		if (!alloc_node)
			return;

		allocation_sites.insert(alloc_node);
		allocation_sizes[alloc_node] = size;
	}

	bool TypeBasedAliasResult::is_allocation_site(Node* node) const
	{
		return allocation_sites.contains(node);
	}

	const std::vector<Node*>& TypeBasedAliasResult::memory_accesses() const
	{
		return mem_accesses;
	}

	bool TypeBasedAliasResult::has_escaped(Node *allocation_site) const
	{
		return escaped_allocations.contains(allocation_site);
	}

	void TypeBasedAliasResult::mark_escaped(Node *allocation_site)
	{
		if (allocation_site)
			escaped_allocations.insert(allocation_site);
	}

	std::string TypeBasedAliasAnalysisPass::name() const
	{
		return "type-based-alias-analysis";
	}

	std::vector<std::string> TypeBasedAliasAnalysisPass::require() const
	{
		return {}; /* TBAA has no dependencies */
	}

	Analysis* TypeBasedAliasAnalysisPass::run(const Module& module)
	{
		ach::shared_allocator<TypeBasedAliasResult> alloc;
		TypeBasedAliasResult* result = alloc.allocate(1);
		std::construct_at(result);

		for (Node* func : module.functions())
		{
			if (func->ir_type == NodeType::FUNCTION)
				analyze_function(result, func, const_cast<Module&>(module));
		}

		return result;
	}

	void TypeBasedAliasAnalysisPass::analyze_function(TypeBasedAliasResult* result, Node* func, Module& module)
	{
		const std::string_view func_name = module.strtable().get(func->str_id);

		/* find the function's region */
		for (Region* child : module.root()->children())
		{
			if (child->name() == func_name)
			{
				analyze_region(result, child);
				break;
			}
		}
	}

	void TypeBasedAliasAnalysisPass::analyze_region(TypeBasedAliasResult* result, Region* region) // NOLINT(*-no-recursion)
	{
		if (!region)
			return;

		/* analyze all nodes in this region */
		for (Node* node : region->nodes())
			analyze_node(result, node);

		/* recursively analyze child regions */
		for (Region* child : region->children())
			analyze_region(result, child);
	}

	void TypeBasedAliasAnalysisPass::analyze_node(TypeBasedAliasResult* result, Node* node)
	{
		if (!node)
			return;

		if (node->ir_type == NodeType::ALLOC)
			handle_allocation(result, node);
		else if (is_memory_access(node))
			handle_memory_access(result, node);
		else if (node->ir_type == NodeType::CALL)
		{
			/* unknown size (0) since we don't know where
			   exactly it allocates */
			if (node->type_kind == DataType::POINTER)
				result->add_allocation_site(node, 0);

			/* i = 1 because we skip the function itself */
			for (std::size_t i = 1; i < node->inputs.size(); ++i)
			{
				if (Node* arg = node->inputs[i];
					arg->type_kind == DataType::POINTER)
				{
					std::int64_t dummy_offset = 0; /* this is actually unused in trace_pointer_base */
					Node* allocation = trace_pointer_base(arg, dummy_offset);
					if (!is_const_pointer(arg))
						result->mark_escaped(allocation);
				}
			}
		}
		else if (node->ir_type == NodeType::RET)
		{
			if (!node->inputs.empty() && node->inputs[0]->type_kind == DataType::POINTER)
			{
				std::int64_t dummy_offset = 0;
				if (Node* allocation = trace_pointer_base(node->inputs[0], dummy_offset))
					result->mark_escaped(allocation);
			}
		}
		else if (node->ir_type == NodeType::STORE || node->ir_type == NodeType::PTR_STORE)
		{
			if (node->inputs.size() >= 1 && node->inputs[0]->type_kind == DataType::POINTER)
			{
				std::int64_t dummy_offset = 0;
				if (Node* allocation = trace_pointer_base(node->inputs[0], dummy_offset))
					result->mark_escaped(allocation);
			}
		}
	}

	void TypeBasedAliasAnalysisPass::handle_allocation(TypeBasedAliasResult* result, Node* node)
	{
		if (!node->inputs.empty())
		{
			/* get allocation size from count parameter */
			std::uint64_t count = 1;
			if (Node* count_node = node->inputs[0]; count_node->ir_type == NodeType::LIT)
				count = static_cast<std::uint64_t>(extract_literal_value(count_node));

			std::uint64_t elem_size = elem_sz(node->type_kind);
			std::uint64_t total_size = count * elem_size;

			result->add_allocation_site(node, total_size);
		}
		else
		{
			result->add_allocation_site(node, 0); /* allocation with unknown size */
		}
	}

	void TypeBasedAliasAnalysisPass::handle_memory_access(TypeBasedAliasResult* result, Node* node)
	{
		MemoryLocation location = compute_memory_location(node);
		if (location.allocation_site)
			result->add_memory_access(node, location);
	}

	MemoryLocation TypeBasedAliasAnalysisPass::compute_memory_location(Node* node) const
	{
		MemoryLocation location;
		bool is_global = node->parent && node->parent->parent() == nullptr;
		switch (node->ir_type)
		{
			case NodeType::LOAD:
			case NodeType::STORE:
			{
				if (is_global)
				{
					location.allocation_site = nullptr;
					location.offset = -1;
					location.access_type = (node->ir_type == NodeType::STORE) ?
										  node->inputs[0]->type_kind : node->type_kind;
					location.size = elem_sz(location.access_type);
					break;
				}

				/* direct access to named location */
				if (!node->inputs.empty())
				{
					Node* target = (node->ir_type == NodeType::STORE) ? node->inputs[1] : node->inputs[0];

					std::int64_t offset = 0;
					location.allocation_site = trace_pointer_base(target, offset);
					location.offset = offset;
					location.access_type = (node->ir_type == NodeType::STORE) ?
					                      node->inputs[0]->type_kind : node->type_kind;
					location.size = elem_sz(location.access_type);
				}
				break;
			}

			case NodeType::PTR_LOAD:
			case NodeType::PTR_STORE:
			case NodeType::ATOMIC_LOAD:
			case NodeType::ATOMIC_STORE:
			{
				/* pointer-based access */
				Node* pointer = nullptr;
				if (node->ir_type == NodeType::PTR_STORE || node->ir_type == NodeType::ATOMIC_STORE)
				{
					if (node->inputs.size() >= 2)
						pointer = node->inputs[1];
				}
				else
				{
					if (!node->inputs.empty())
						pointer = node->inputs[0];
				}

				if (pointer)
				{
					/* get pointee type for pointer operations */
					std::int64_t offset = 0;
					location.allocation_site = trace_pointer_base(pointer, offset);
					location.offset = offset;
					if (pointer->type_kind == DataType::POINTER && pointer->value.type() == DataType::POINTER)
					{
						const auto&[pointee, addr_space, qual] = pointer->value.get<DataType::POINTER>();
						if (pointee)
							location.access_type = pointee->type_kind;
					}
					location.size = elem_sz(location.access_type);
				}
				break;
			}

			default:
				break;
		}

		return location;
	}

	Node* TypeBasedAliasAnalysisPass::trace_pointer_base(Node* pointer, std::int64_t& offset) const
	{
		offset = 0;
		Node* current = pointer;

		while (current)
		{
			switch (current->ir_type)
			{
				case NodeType::ALLOC:
					return current; /* found base allocation */

				/* address-of creates pointer to allocation */
				case NodeType::ADDR_OF:
					if (!current->inputs.empty())
						return trace_pointer_base(current->inputs[0], offset);
					return nullptr;

				case NodeType::PTR_ADD:
					if (current->inputs.size() >= 2)
					{
						Node* base = current->inputs[0];
						if (Node* offset_node = current->inputs[1];
							offset_node->ir_type == NodeType::LIT)
						{
							std::int64_t add_offset = extract_literal_value(offset_node);
							offset += add_offset;
							current = base;
							continue;
						}
					}
					return nullptr;

				case NodeType::ACCESS:
					/* struct field or array index access */
					if (current->inputs.size() >= 2)
					{
						Node* container = current->inputs[0];
						Node* index_node = current->inputs[1];
						if (index_node->ir_type == NodeType::LIT)
						{
							if (container->type_kind == DataType::STRUCT)
							{
								auto fi = extract_literal_value(index_node);
								if (container->value.type() != DataType::STRUCT)
									throw std::runtime_error("struct node missing type information");

								const auto& struct_data = container->value.get<DataType::STRUCT>();
								std::uint64_t offset_accumulator = 0;
								std::uint32_t actual_field_count = 0;
								auto& m = index_node->parent->module(); /* note: hacky but works */
								for (const auto& [name_id, field_type, field_data] : struct_data.fields)
								{
									std::string field_name(m.strtable().get(name_id));
									if (field_name.starts_with("__pad"))
									{
										offset_accumulator += elem_sz(field_type);
										continue;
									}

									if (actual_field_count == fi)
									{
										offset += offset_accumulator;
										break;
									}

									offset_accumulator += elem_sz(field_type);
									actual_field_count++;
								}

								if (actual_field_count <= fi)
									throw std::runtime_error("struct field index out of bounds");
							}
							else if (container->type_kind == DataType::ARRAY)
							{
								const auto array_index = extract_literal_value(index_node);
								if (container->value.type() == DataType::ARRAY)
								{
									const auto& arr_data = container->value.get<DataType::ARRAY>();
									const std::uint64_t elem_size = elem_sz(arr_data.elem_type);
									offset += array_index * elem_size;
								}
							}
							current = container;
							continue;
						}
					}
					return nullptr;

				/* function parameters are treated as unknown allocation sites as
				 * we don't know where exactly is it from; need global AA for this */
				case NodeType::PARAM:
					return current;

				case NodeType::CAST:
					/* follow through pointer casts */
					if (!current->inputs.empty())
					{
						current = current->inputs[0];
						continue;
					}
					return nullptr;

				case NodeType::CALL:
					if (current->type_kind == DataType::POINTER)
						return current;
					return nullptr;

				default:
					return nullptr;
			}
		}

		return nullptr;
	}

	std::int64_t TypeBasedAliasAnalysisPass::extract_literal_value(Node* node)
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

	bool TypeBasedAliasAnalysisPass::is_memory_access(Node* node)
	{
		switch (node->ir_type)
		{
			case NodeType::LOAD:
			case NodeType::STORE:
			case NodeType::PTR_LOAD:
			case NodeType::PTR_STORE:
			case NodeType::ATOMIC_LOAD:
			case NodeType::ATOMIC_STORE:
				return true;
			default:
				return false;
		}
	}
}
