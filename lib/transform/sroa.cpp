/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <arc/analysis/tbaa.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/foundation/region.hpp>
#include <arc/support/algorithm.hpp>
#include <arc/support/allocator.hpp>
#include <arc/support/inference.hpp>
#include <arc/transform/sroa.hpp>

namespace arc
{
	namespace
	{
		bool is_promotable_allocation(Node* alloc, const TypeBasedAliasResult& tbaa)
		{
			if (!alloc || alloc->ir_type != NodeType::ALLOC)
				return false;

			/* must be a struct type allocation */
			if (alloc->type_kind != DataType::STRUCT)
				return false;

			/* allocation must not have escaped */
			if (tbaa.has_escaped(alloc) || !tbaa.is_allocation_site(alloc))
				return false;

			/* don't promote volatile allocations */
			if ((alloc->traits & NodeTraits::VOLATILE) != NodeTraits::NONE)
				return false;

			return true;
		}

		/**
		 * @brief Check if a node represents a load operation
		 * @param node Node to check
		 * @return true if node is a load operation
		 */
		bool is_load_operation(const Node* node)
		{
			if (!node)
				return false;

			/* exclude atomic operations as they have ordering constraints */
			return node->ir_type == NodeType::LOAD ||
			       node->ir_type == NodeType::PTR_LOAD;
		}

		bool is_store_operation(const Node* node)
		{
			if (!node)
				return false;

			/* exclude atomic operations as they have ordering constraints */
			return node->ir_type == NodeType::STORE ||
			       node->ir_type == NodeType::PTR_STORE;
		}

		std::size_t extract_field_index(Node* access_node)
		{
			if (!access_node || access_node->ir_type != NodeType::ACCESS)
				return SIZE_MAX;

			if (access_node->inputs.size() < 2)
				return SIZE_MAX;

			Node* index_node = access_node->inputs[1];
			if (!index_node || index_node->ir_type != NodeType::LIT)
				return SIZE_MAX;

			std::int64_t index_value = 0;
			switch (index_node->type_kind)
			{
				case DataType::INT32:
					index_value = index_node->value.get<DataType::INT32>();
					break;
				case DataType::INT64:
					index_value = index_node->value.get<DataType::INT64>();
					break;
				case DataType::UINT32:
					index_value = index_node->value.get<DataType::UINT32>();
					break;
				case DataType::UINT64:
					index_value = static_cast<std::int64_t>(index_node->value.get<DataType::UINT64>());
					break;
				default:
					return SIZE_MAX;
			}

			if (index_value < 0)
				return SIZE_MAX;

			return index_value;
		}

		void collect_field_accesses(Node* alloc, AllocationInfo& info, const TypeBasedAliasResult& tbaa)
		{
			/* check for address-taken operations that prevent promotion
			 * and mark all fields as escaped since address was taken */
			for (Node* user : alloc->users)
			{
				if (user->ir_type == NodeType::ADDR_OF)
				{
					info.fully_promotable = false;
					if (alloc->value.type() == DataType::STRUCT)
					{
						const auto& struct_data = alloc->value.get<DataType::STRUCT>();
						std::size_t logical_field_index = 0;
						for (const auto& [name_id, field_type, field_data] : struct_data.fields)
						{
							std::string_view field_name = alloc->parent->module().strtable().get(name_id);
							if (!field_name.starts_with("__pad"))
							{
								info.escaped_fields.insert(logical_field_index);
								logical_field_index++;
							}
						}
					}
					return;
				}
			}

			/* collect ACCESS nodes that reference our allocation */
			for (Node* user : alloc->users)
			{
				if (user->ir_type == NodeType::ACCESS)
				{
					std::size_t field_index = extract_field_index(user);
					if (field_index == SIZE_MAX) [[unlikely]]
					{
						/* mark as non-promotable because we are unable to determine the access index
						 * this shouldn't happen at all */
						info.fully_promotable = false;
						return;
					}

					/* collect all uses of this ACCESS node */
					for (Node* access_user : user->users)
					{
						FieldAccess field_access;
						field_access.access_node = access_user;
						field_access.field_index = field_index;
						field_access.access_intermediate = user;

						if (is_load_operation(access_user)) [[likely]]
						{
							field_access.is_store = false;
							info.field_accesses.push_back(field_access);
						}
						else if (is_store_operation(access_user))
						{
							field_access.is_store = true;
							info.field_accesses.push_back(field_access);
						}
						else [[unlikely]]
						{
							/* ACCESS node used for something other than load/store;
							 * this also shouldn't happen either */
							info.escaped_fields.insert(field_index);
							info.fully_promotable = false;
						}
					}
				}
			}
		}

		bool analyze_struct_uses(AllocationInfo& info)
		{
			if (!info.alloc_node || info.alloc_node->value.type() != DataType::STRUCT)
				return false;

			/* count logical fields; no padding counted */
			const auto& struct_data = info.alloc_node->value.get<DataType::STRUCT>();
			std::size_t logical_field_count = 0;
			for (const auto& [name_id, field_type, field_data] : struct_data.fields)
			{
				std::string_view field_name = info.alloc_node->parent->module().strtable().get(name_id);
				if (!field_name.starts_with("__pad"))
					logical_field_count++;
			}

			/* check for ACCESS nodes that might escape through calls or returns */
			for (Node* user : info.alloc_node->users)
			{
				if (user->ir_type == NodeType::ACCESS)
				{
					std::size_t field_index = extract_field_index(user);
					if (field_index == SIZE_MAX || field_index >= logical_field_count)
						continue;

					/* check if this ACCESS node escapes */
					for (Node* access_user : user->users)
					{
						/* field address passed to function, returned from function or address of field taken
						 * are NOT promotable */
						if (access_user->ir_type == NodeType::CALL || access_user->ir_type == NodeType::INVOKE
							|| access_user->ir_type == NodeType::RET || access_user->ir_type == NodeType::ADDR_OF)
						{

							info.escaped_fields.insert(field_index);
							info.fully_promotable = false;
						}
					}
				}
			}

			/* determine if partial promotion is worthwhile;
			 * only do partial promotion if we can promote at least some fields */
			if (!info.fully_promotable)
			{
				std::size_t promotable_fields = logical_field_count - info.escaped_fields.size();
				return promotable_fields > 0;
			}

			return true;
		}

		void make_scalar_allocations(AllocationInfo& info, Module& module)
		{
			if (!info.alloc_node || info.alloc_node->value.type() != DataType::STRUCT)
				return;

			const auto& struct_data = info.alloc_node->value.get<DataType::STRUCT>();
			Region* alloc_region = info.alloc_node->parent;
			if (!alloc_region)
				return;

			/* count logical fields and create scalar allocations */
			std::size_t logical_field_index = 0;
			Node* insert_point = info.alloc_node;

			for (const auto& [name_id, field_type, field_data] : struct_data.fields)
			{
				std::string_view field_name = module.strtable().get(name_id);
				if (field_name.starts_with("__pad"))
					continue;

				if (logical_field_index >= info.scalar_allocs.size())
					info.scalar_allocs.resize(logical_field_index + 1, nullptr);

				/* skip escaped fields */
				if (info.escaped_fields.contains(logical_field_index))
				{
					logical_field_index++;
					continue;
				}

				/* create scalar allocation for this field */
				ach::shared_allocator<Node> alloc;
				Node* scalar_alloc = alloc.allocate(1);
				std::construct_at(scalar_alloc);

				/* there are actually `DataType::VOID` to handle
				 * but it should never happen here */
				scalar_alloc->ir_type = NodeType::ALLOC;
				scalar_alloc->type_kind = field_type;
				scalar_alloc->parent = alloc_region;
				set_t(scalar_alloc->value, field_type);

				/* insert after the previous allocation */
				alloc_region->insert_after(insert_point, scalar_alloc);
				info.scalar_allocs[logical_field_index] = scalar_alloc;
				insert_point = scalar_alloc;

				logical_field_index++;
			}
		}

		void replace_field_accesses(const AllocationInfo& info)
		{
			std::unordered_set<Node*> access_nodes_to_remove;
			for (const FieldAccess& access : info.field_accesses)
			{
				std::size_t field_idx = access.field_index;

				/* skip escaped fields */
				if (info.escaped_fields.contains(field_idx))
					continue;

				/* skip if no scalar allocation was created */
				if (field_idx >= info.scalar_allocs.size() || !info.scalar_allocs[field_idx])
					continue;

				Node* scalar_alloc = info.scalar_allocs[field_idx];
				Node* access_node = access.access_node;

				/* replace the ACCESS node input with scalar allocation */
				if (access.is_store)
				{
					/* for stores, replace the location/pointer input */
					if (access_node->ir_type == NodeType::STORE && access_node->inputs.size() >= 2)
					{
						Node* old_location = access_node->inputs[1];
						access_node->inputs[1] = scalar_alloc;

						if (old_location)
						{
							auto& old_users = old_location->users;
							arc::erase(old_users, access_node);
						}
						scalar_alloc->users.push_back(access_node);
					}
					else if (access_node->ir_type == NodeType::PTR_STORE && access_node->inputs.size() >= 2)
					{
						Node* old_pointer = access_node->inputs[1];
						access_node->inputs[1] = scalar_alloc;
						if (old_pointer)
						{
							auto& old_users = old_pointer->users;
							arc::erase(old_users, access_node);
						}
						scalar_alloc->users.push_back(access_node);
					}
				}
				else
				{
					if (access_node->ir_type == NodeType::LOAD && !access_node->inputs.empty())
					{
						Node* old_location = access_node->inputs[0];
						access_node->inputs[0] = scalar_alloc;
						if (old_location)
						{
							auto& old_users = old_location->users;
							erase(old_users, access_node);
						}
						scalar_alloc->users.push_back(access_node);
					}
					else if (access_node->ir_type == NodeType::PTR_LOAD && !access_node->inputs.empty())
					{
						Node* old_pointer = access_node->inputs[0];
						access_node->inputs[0] = scalar_alloc;
						if (old_pointer)
						{
							auto& old_users = old_pointer->users;
							erase(old_users, access_node);
						}
						scalar_alloc->users.push_back(access_node);
					}
				}

				if (access.access_intermediate)
					access_nodes_to_remove.insert(access.access_intermediate);

				if (access_node->ir_type == NodeType::LOAD || access_node->ir_type == NodeType::STORE)
				{
					Node* access_input = (access_node->ir_type == NodeType::STORE) ?
										 access_node->inputs[1] : access_node->inputs[0];
					if (access_input && access_input->ir_type == NodeType::ACCESS)
						access_nodes_to_remove.insert(access_input);
				}
			}

			for (Node* access_node : access_nodes_to_remove)
			{
				if (access_node->users.empty() && access_node->parent)
					access_node->parent->remove(access_node);
			}
		}

		TypedData make_reduced_struct_t(const AllocationInfo& info, Module& module)
		{
			if (!info.alloc_node || info.alloc_node->value.type() != DataType::STRUCT)
				throw std::runtime_error("make_reduced_struct_t requires struct allocation");

			const auto& original_struct = info.alloc_node->value.get<DataType::STRUCT>();

			/* collect fields that are non-promotable e.g. escaped etc. */
			u8slice<std::tuple<StringTable::StringId, DataType, TypedData>> reduced_fields{};
			std::size_t logical_field_index = 0;

			for (const auto& [name_id, field_type, field_data] : original_struct.fields)
			{
				std::string_view field_name = module.strtable().get(name_id);
				if (field_name.starts_with("__pad"))
				{
					/* include padding fields in reduced struct */
					reduced_fields.emplace_back(name_id, field_type, field_data);
					continue;
				}

				if (info.escaped_fields.contains(logical_field_index))
					reduced_fields.emplace_back(name_id, field_type, field_data);

				logical_field_index++;
			}

			/* create reduced struct type */
			DataTraits<DataType::STRUCT>::value reduced_struct;
			reduced_struct.fields = std::move(reduced_fields);
			reduced_struct.alignment = original_struct.alignment;

			/* generate unique name for reduced struct */
			static std::atomic<std::uint32_t> counter{0};
			std::string reduced_name = "__sroa_reduced_" + std::to_string(counter.fetch_add(1));
			reduced_struct.name = module.intern_str(reduced_name);

			TypedData reduced_type;
			reduced_type.set<decltype(reduced_struct), DataType::STRUCT>(std::move(reduced_struct));

			return reduced_type;
		}

		bool transform_allocation(AllocationInfo& info, Module& module)
		{
			if (info.fully_promotable)
			{
				/* replace the entire struct with scalars */
				make_scalar_allocations(info, module);
				replace_field_accesses(info);

				/* then remove original allocation */
				if (info.alloc_node->parent)
					info.alloc_node->parent->remove(info.alloc_node);

				return true;
			}

			/* partial promotion for structs */
			if (!info.escaped_fields.empty())
			{
				if (info.alloc_node->value.type() == DataType::STRUCT)
				{
					const auto& struct_data = info.alloc_node->value.get<DataType::STRUCT>();
					std::size_t total_logical_fields = 0;
					for (const auto& [name_id, field_type, field_data] : struct_data.fields)
					{
						std::string_view field_name = module.strtable().get(name_id);
						if (!field_name.starts_with("__pad"))
							total_logical_fields++;
					}

					if (info.escaped_fields.size() < total_logical_fields)
					{
						/* some fields can be promoted */
						TypedData reduced_type = make_reduced_struct_t(info, module);

						/* register reduced struct type with module */
						std::string_view reduced_name = module.strtable().get(reduced_type.get<DataType::STRUCT>().name);
						module.add_t(reduced_name.data(), reduced_type);

						info.alloc_node->value = reduced_type;
						info.alloc_node->type_kind = DataType::STRUCT;

						make_scalar_allocations(info, module);
						replace_field_accesses(info);
						return true;
					}
				}
			}

			return false;
		}

		std::vector<AllocationInfo> analyze_promotable_allocs(Region* region, const TypeBasedAliasResult& tbaa)
		{
			std::vector<AllocationInfo> candidates;
			if (!region)
				return candidates;

			/* traverse the region tree and find promotable allocations */
			region->walk_dominated_regions([&](const Region* current_region)
			{
				for (Node* node : current_region->nodes())
				{
					if (is_promotable_allocation(node, tbaa))
					{
						AllocationInfo info;
						info.alloc_node = node;
						info.struct_type = node->type_kind;
						info.fully_promotable = true;

						collect_field_accesses(node, info, tbaa);
						if (analyze_struct_uses(info))
							candidates.push_back(std::move(info));
					}
				}
			});

			return candidates;
		}

		void cleanup_modified_regions(std::vector<Region*>& modified_regions)
		{
			std::ranges::sort(modified_regions);
			modified_regions.erase(std::unique(modified_regions.begin(), modified_regions.end()),
			                      modified_regions.end());
		}
	}

	std::string SROAPass::name() const
	{
		return "scalar-replacement-of-aggregates";
	}

	std::vector<std::string> SROAPass::require() const
	{
		return { "type-based-alias-analysis" };
	}

	std::vector<std::string> SROAPass::invalidates() const
	{
		/* SROA removes allocations, doesn't change aliasing of remaining operations */
		return {};
	}

	std::vector<Region*> SROAPass::run(Module& module, PassManager& pm)
	{
		const auto& tbaa_result = pm.get<TypeBasedAliasResult>();
		std::vector<Region*> all_modified_regions;

		for (const Node* func_node : module.functions())
		{
			if (func_node->ir_type != NodeType::FUNCTION)
				continue;

			const std::string_view func_name = module.strtable().get(func_node->str_id);
			Region* func_region = nullptr;

			for (Region* child : module.root()->children())
			{
				if (child->name() == func_name)
				{
					func_region = child;
					break;
				}
			}

			if (!func_region)
				continue;

			std::vector<Region*> modified_regions = process_function(func_region, tbaa_result);
			all_modified_regions.insert(all_modified_regions.end(),
			                           modified_regions.begin(), modified_regions.end());
		}

		cleanup_modified_regions(all_modified_regions);
		return all_modified_regions;
	}

	std::vector<Region*> SROAPass::process_function(Region* func_region, const TypeBasedAliasResult& tbaa)
	{
		std::vector<Region*> modified_regions;
		std::unordered_set<Region*> affected_regions;

		/* find and analyze promotable allocations then transform each candidate allocation */
		for (std::vector<AllocationInfo> candidates = analyze_promotable_allocs(func_region, tbaa);
		     AllocationInfo& info : candidates)
		{
			if (transform_allocation(info, func_region->module()))
			{
				if (info.alloc_node->parent)
					affected_regions.insert(info.alloc_node->parent);

				for (Node* scalar_alloc : info.scalar_allocs)
				{
					if (scalar_alloc && scalar_alloc->parent)
						affected_regions.insert(scalar_alloc->parent);
				}

				for (const FieldAccess& access : info.field_accesses)
				{
					if (access.access_node && access.access_node->parent)
						affected_regions.insert(access.access_node->parent);
				}
			}
		}

		/* convert `std::set` to `std::vector` */
		modified_regions.reserve(affected_regions.size());
		for (Region* region : affected_regions)
			modified_regions.push_back(region);

		return modified_regions;
	}
}
