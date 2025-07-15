/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <print>
#include <string>
#include <unordered_map>
#include <arc/foundation/module.hpp>
#include <arc/foundation/node.hpp>
#include <arc/foundation/region.hpp>
#include <arc/foundation/typed-data.hpp>
#include <arc/support/dump.hpp>

namespace arc
{
	namespace
	{
		thread_local std::unordered_map<Node *, std::uint32_t> node_numbers;
		thread_local std::uint32_t next_node_number = 1;

		std::uint32_t get_node_number(Node *node)
		{
			if (const auto it = node_numbers.find(node);
				it != node_numbers.end())
				return it->second;

			const std::uint32_t num = next_node_number++;
			node_numbers[node] = num;
			return num;
		}

		void reset_numbering()
		{
			node_numbers.clear();
			next_node_number = 1;
		}

		bool is_padding_field(const std::string &field_name)
		{
			return field_name.starts_with("__pad");
		}

		std::size_t get_padding_size(DataType padding_type)
		{
			switch (padding_type)
			{
				case DataType::UINT8:
					return 1;
				case DataType::UINT16:
					return 2;
				case DataType::UINT32:
					return 4;
				case DataType::UINT64:
					return 8;
				default:
					return 0;
			}
		}

		std::string dttstr(DataType type)
		{
			switch (type)
			{
				case DataType::VOID:
					return "void";
				case DataType::BOOL:
					return "bool";
				case DataType::INT8:
					return "i8";
				case DataType::INT16:
					return "i16";
				case DataType::INT32:
					return "i32";
				case DataType::INT64:
					return "i64";
				case DataType::UINT8:
					return "u8";
				case DataType::UINT16:
					return "u16";
				case DataType::UINT32:
					return "u32";
				case DataType::UINT64:
					return "u64";
				case DataType::FLOAT32:
					return "f32";
				case DataType::FLOAT64:
					return "f64";
				case DataType::POINTER:
					return "ptr";
				case DataType::ARRAY:
					return "arr";
				case DataType::STRUCT:
					return "struct";
				case DataType::FUNCTION:
					return "fn";
				case DataType::VECTOR:
					return "vec";
				default:
					return "unknown";
			}
		}

		std::string resolve_type_name(const TypedData &type_data, Module &module)
		{
			if (type_data.type() != DataType::STRUCT)
				return dttstr(type_data.type());

			const auto &struct_data = type_data.get<DataType::STRUCT>();
			auto struct_name = module.strtable().get(struct_data.name);
			if (auto x = std::string(struct_name);
				module.typemap().contains(x))
				return x;

			return std::format("struct {}", struct_name);
		}

		std::string pointer_type_str(const DataTraits<DataType::POINTER>::value &ptr_data, Module &module,
		                             const std::string &context_struct = "")
		{
			if (!ptr_data.pointee)
			{
				if (!context_struct.empty())
					return std::format("ptr<{}>", context_struct);
				return "ptr<unknown>";
			}

			if (ptr_data.pointee->type_kind == DataType::STRUCT && ptr_data.pointee->value.type() == DataType::STRUCT)
			{
				std::string pointee_name = resolve_type_name(ptr_data.pointee->value, module);
				return std::format("ptr<{}>", pointee_name);
			}

			return std::format("ptr<{}>", dttstr(ptr_data.pointee->type_kind));
		}

		/* lookup field type from ACCESS node by examining the struct definition */
		std::string resolve_access_type(Node &node, Module &module)
		{
			if (node.inputs.size() < 2)
				return "unknown";

			Node *container = node.inputs[0];
			Node *index_node = node.inputs[1];

			/* get the field index */
			if (index_node->ir_type != NodeType::LIT || index_node->type_kind != DataType::UINT32)
				return "unknown";

			std::uint32_t field_index = index_node->value.get<DataType::UINT32>();

			/* get the struct type name from the container */
			std::string struct_name;
			if (container->type_kind == DataType::STRUCT && container->value.type() == DataType::STRUCT)
			{
				struct_name = resolve_type_name(container->value, module);
			}
			else
			{
				return "unknown";
			}

			/* look up the struct definition in the module's type registry */
			const auto &typemap = module.typemap();
			auto it = typemap.find(struct_name);
			if (it == typemap.end() || it->second.type() != DataType::STRUCT)
				return "unknown";

			const auto &struct_data = it->second.get<DataType::STRUCT>();

			/* find the field at the given index. we skip padding fields */
			std::size_t actual_field_index = 0;
			for (const auto &[name_id, field_type, field_data]: struct_data.fields)
			{
				auto field_name = module.strtable().get(name_id);
				if (is_padding_field(std::string(field_name)))
					continue;

				if (actual_field_index == field_index)
				{
					if (field_type == DataType::POINTER && field_data.type() == DataType::POINTER)
					{
						const auto &ptr_data = field_data.get<DataType::POINTER>();
						return pointer_type_str(ptr_data, module, struct_name);
					}
					if (field_type == DataType::STRUCT && field_data.type() == DataType::STRUCT)
						return resolve_type_name(field_data, module);

					return dttstr(field_type);
				}
				actual_field_index++;
			}

			return "unknown";
		}

		std::string cdttstr(Node &node, Module &module) // NOLINT(*-no-recursion)
		{
			switch (node.type_kind)
			{
				case DataType::POINTER:
				{
					/* for ACCESS nodes, use the stored value directly */
					if (node.ir_type == NodeType::ACCESS && node.value.type() == DataType::POINTER)
					{
						const auto &ptr_data = node.value.get<DataType::POINTER>();
						/* if this is a self-reference [pointee = nullptr],
						 * we can try to resolve the type from the context */
						if (!ptr_data.pointee && node.inputs.size() >= 1)
						{
							if (Node* container = node.inputs[0];
								container->type_kind == DataType::STRUCT && container->value.type() == DataType::STRUCT)
							{
								std::string struct_name = resolve_type_name(container->value, module);
								return std::format("ptr<{}>", struct_name);
							}
						}
						return pointer_type_str(ptr_data, module);
					}

					/* for LOAD nodes, it needs to inherit the type from the source node,
					 * so we can resolve it directly from the input */
					if (node.ir_type == NodeType::LOAD && !node.inputs.empty())
					{
						if (Node* source = node.inputs[0];
							source->type_kind == DataType::POINTER)
						{
							return cdttstr(*source, module);
						}
					}

					/* only fallback to registry lookup only if value is not available
					 * this happens rarely and the case shouldn't ever hit "ptr<unknown>" */
					if (node.ir_type == NodeType::ACCESS)
						return resolve_access_type(node, module);

					if (node.value.type() == DataType::POINTER)
					{
						const auto &ptr_data = node.value.get<DataType::POINTER>();
						return pointer_type_str(ptr_data, module);
					}
					return "ptr<unknown>";
				}
				case DataType::ARRAY:
				{
					const auto &arr_data = node.value.get<DataType::ARRAY>();
					return std::format("arr<{} x {}>", dttstr(arr_data.elem_type), arr_data.count);
				}
				case DataType::VECTOR:
				{
					const auto &vec_data = node.value.get<DataType::VECTOR>();
					return std::format("vec<{} x {}>", dttstr(vec_data.elem_type), vec_data.lane_count);
				}
				case DataType::STRUCT:
				{
					if (node.value.type() == DataType::STRUCT)
						return resolve_type_name(node.value, module);
					return "struct";
				}
				case DataType::FUNCTION:
				{
					std::string result = "fn(";
					for (std::size_t i = 0; i < node.inputs.size(); ++i)
					{
						if (i > 0)
							result += ", ";
						result += dttstr(node.inputs[i]->type_kind);
					}

					const auto &fn_data = node.value.get<DataType::FUNCTION>();
					DataType return_type = fn_data.return_type ? fn_data.return_type->type() : DataType::VOID;
					result += std::format(") -> {}", dttstr(return_type));
					return result;
				}
				default:
					return dttstr(node.type_kind);
			}
		}

		std::string ntttstr(NodeType type)
		{
			switch (type)
			{
				case NodeType::ENTRY:
					return "entry";
				case NodeType::EXIT:
					return "exit";
				case NodeType::PARAM:
					return "param";
				case NodeType::LIT:
					return "";
				case NodeType::ADD:
					return "add";
				case NodeType::SUB:
					return "sub";
				case NodeType::MUL:
					return "mul";
				case NodeType::DIV:
					return "div";
				case NodeType::MOD:
					return "mod";
				case NodeType::GT:
					return "gt";
				case NodeType::GTE:
					return "gte";
				case NodeType::LT:
					return "lt";
				case NodeType::LTE:
					return "lte";
				case NodeType::EQ:
					return "eq";
				case NodeType::NEQ:
					return "neq";
				case NodeType::BAND:
					return "band";
				case NodeType::BOR:
					return "bor";
				case NodeType::BXOR:
					return "bxor";
				case NodeType::BNOT:
					return "bnot";
				case NodeType::BSHL:
					return "bshl";
				case NodeType::BSHR:
					return "bshr";
				case NodeType::RET:
					return "ret";
				case NodeType::FUNCTION:
					return "fn";
				case NodeType::CALL:
					return "call";
				case NodeType::ALLOC:
					return "alloc";
				case NodeType::LOAD:
					return "load";
				case NodeType::STORE:
					return "store";
				case NodeType::ADDR_OF:
					return "addr_of";
				case NodeType::PTR_LOAD:
					return "ptr_load";
				case NodeType::PTR_STORE:
					return "ptr_store";
				case NodeType::PTR_ADD:
					return "ptr_add";
				case NodeType::CAST:
					return "cast";
				case NodeType::ATOMIC_LOAD:
					return "atomic_load";
				case NodeType::ATOMIC_STORE:
					return "atomic_store";
				case NodeType::ATOMIC_CAS:
					return "atomic_cas";
				case NodeType::JUMP:
					return "jump";
				case NodeType::BRANCH:
					return "branch";
				case NodeType::INVOKE:
					return "invoke";
				case NodeType::VECTOR_BUILD:
					return "vector_build";
				case NodeType::VECTOR_EXTRACT:
					return "vector_extract";
				case NodeType::VECTOR_SPLAT:
					return "vector_splat";
				case NodeType::ACCESS:
					return "access";
				default:
					return "unknown";
			}
		}

		void print_node_traits(Node &node, std::ostream &os)
		{
			if ((node.traits & NodeTraits::EXPORT) != NodeTraits::NONE)
				std::print(os, "export ");
			if ((node.traits & NodeTraits::DRIVER) != NodeTraits::NONE)
				std::print(os, "driver ");
			if ((node.traits & NodeTraits::EXTERN) != NodeTraits::NONE)
				std::print(os, "extern ");
			if ((node.traits & NodeTraits::VOLATILE) != NodeTraits::NONE)
				std::print(os, "volatile ");
		}

		void print_lit_v(Node &node, std::ostream &os)
		{
			switch (node.type_kind)
			{
				case DataType::BOOL:
					std::print(os, "{}", node.value.get<DataType::BOOL>() ? "true" : "false");
					break;
				case DataType::INT8:
					std::print(os, "{}", static_cast<int>(node.value.get<DataType::INT8>()));
					break;
				case DataType::INT16:
					std::print(os, "{}", node.value.get<DataType::INT16>());
					break;
				case DataType::INT32:
					std::print(os, "{}", node.value.get<DataType::INT32>());
					break;
				case DataType::INT64:
					std::print(os, "{}", node.value.get<DataType::INT64>());
					break;
				case DataType::UINT8:
					std::print(os, "{}", static_cast<unsigned>(node.value.get<DataType::UINT8>()));
					break;
				case DataType::UINT16:
					std::print(os, "{}", node.value.get<DataType::UINT16>());
					break;
				case DataType::UINT32:
					std::print(os, "{}", node.value.get<DataType::UINT32>());
					break;
				case DataType::UINT64:
					std::print(os, "{}", node.value.get<DataType::UINT64>());
					break;
				case DataType::FLOAT32:
					std::print(os, "{}", node.value.get<DataType::FLOAT32>());
					break;
				case DataType::FLOAT64:
					std::print(os, "{}", node.value.get<DataType::FLOAT64>());
					break;
				default:
					std::print(os, "?");
					break;
			}
		}

		void print_operands(u8slice<Node *> &inputs, std::ostream &os)
		{
			for (u8slice<Node *>::size_type i = 0; i < inputs.size(); ++i)
			{
				if (i > 0)
					std::print(os, ", ");
				if (inputs[i])
				{
					if (inputs[i]->ir_type == NodeType::LIT && inputs[i]->users.size() == 1)
					{
						std::print(os, "#");
						print_lit_v(*inputs[i], os);
					}
					else
					{
						std::print(os, "%{}", get_node_number(inputs[i]));
					}
				}
				else
				{
					std::print(os, "null");
				}
			}
		}

		void dump_struct_definition(const std::string &name, const DataTraits<DataType::STRUCT>::value &struct_data,
		                            Module &module, std::ostream &os)
		{
			std::print(os, "    {} = struct", name);

			if (struct_data.alignment != 8)
				std::print(os, " alignas({})", struct_data.alignment);

			std::print(os, " {{\n");

			std::size_t pending_padding = 0;

			for (auto [name_id, field_type, field_data] : struct_data.fields)
			{
					auto field_name = module.strtable().get(name_id);
				if (is_padding_field(field_name.data()))
				{
					pending_padding += get_padding_size(field_type);
					continue;
				}

				if (pending_padding > 0)
				{
					std::print(os, "        /* {} bytes padding */\n", pending_padding);
					pending_padding = 0;
				}

				std::print(os, "        {}: ", field_name);

				if (field_type == DataType::POINTER && field_data.type() == DataType::POINTER)
				{
					const auto &ptr_data = field_data.get<DataType::POINTER>();
					std::print(os, "{}", pointer_type_str(ptr_data, module, name));
				}
				else if (field_type == DataType::STRUCT && field_data.type() == DataType::STRUCT)
				{
					std::print(os, "{}", resolve_type_name(field_data, module));
				}
				else
				{
					std::print(os, "{}", dttstr(field_type));
				}

				std::print(os, ",\n");
			}

			if (pending_padding > 0)
				std::print(os, "        /* {} bytes padding */\n", pending_padding);

			std::print(os, "    }};\n");
		}

		void dump_regions(Region &region, Module &module, std::ostream &os) // NOLINT(*-no-recursion)
		{
			std::print(os, "    ${}:\n", region.name());
			for (Node *node: region.nodes())
			{
				if (node->ir_type == NodeType::ENTRY)
					std::print(os, "        entry\n");
				else if (node->ir_type == NodeType::PARAM)
					continue;
				else if (node->ir_type == NodeType::LIT && node->users.size() == 1)
					continue;
				else
				{
					std::print(os, "        ");
					dump(*node, module, os);
					std::print(os, ";\n");
				}
			}
			/* removed extra newline here */
			for (Region *child: region.children())
				dump_regions(*child, module, os);
		}
	}

	void dump(Module &module, std::ostream &os)
	{
		reset_numbering();

		std::print(os, "#! module: {}\n", module.name());
		const auto &typedefs = module.typemap();
		if (!typedefs.empty())
		{
			std::print(os, "section .__def\n");
			for (const auto &[name, typedef_data]: typedefs)
			{
				if (typedef_data.type() == DataType::STRUCT)
				{
					const auto &struct_data = typedef_data.get<DataType::STRUCT>();
					dump_struct_definition(name, struct_data, module, os);
				}
				else
				{
					std::print(os, "    {} = {};\n", name, dttstr(typedef_data.type()));
				}
			}
			std::print(os, "end .__def\n\n");
		}

		if (auto *rodata = module.rodata())
		{
			std::print(os, "section .__rodata\n");
			for (Node *node: rodata->nodes())
			{
				if (node->ir_type != NodeType::ENTRY)
				{
					std::print(os, "    ");
					dump(*node, module, os);
					std::print(os, ";\n");
				}
			}
			std::print(os, "end .__rodata\n\n");
		}

		for (Node *func: module.functions())
		{
			if (func->ir_type == NodeType::FUNCTION)
			{
				print_node_traits(*func, os);
				const auto &fn_data = func->value.get<DataType::FUNCTION>();
				DataType return_type = fn_data.return_type ? fn_data.return_type->type() : DataType::VOID;

				std::vector<Node *> params;
				for (Node *param: func->inputs)
				{
					if (param->ir_type == NodeType::PARAM)
						params.push_back(param);
				}

				std::print(os, "fn @{}(", module.strtable().get(func->str_id));
				for (std::size_t i = 0; i < params.size(); ++i)
				{
					if (i > 0)
						std::print(os, ", ");
					std::print(os, "{} %{}", cdttstr(*params[i], module), get_node_number(params[i]));
				}
				std::print(os, ") -> {}\n{{\n", dttstr(return_type));

				for (Region *region: module.root()->children())
				{
					if (region->name() == module.strtable().get(func->str_id))
					{
						dump_regions(*region, module, os);
						break;
					}
				}

				std::print(os, "}}\n"); /* removed extra newline */
			}
		}
	}

	void dump(Region &region, Module &module, std::ostream &os) // NOLINT(*-no-recursion)
	{
		std::print(os, "${}:\n", region.name());
		for (Node *node: region.nodes())
		{
			std::print(os, "    ");
			dump(*node, module, os);
			std::print(os, ";\n");
		}

		for (Region *child: region.children())
		{
			std::print(os, "\n");
			dump(*child, module, os);
		}
	}

	void dump(Node &node, Module &module, std::ostream &os)
	{
		if (node.ir_type == NodeType::ENTRY)
		{
			std::print(os, "entry");
			return;
		}

		if (node.ir_type == NodeType::RET)
		{
			std::print(os, "ret");
			if (!node.inputs.empty())
			{
				std::print(os, " ");
				print_operands(node.inputs, os);
			}
			return;
		}

		if (node.ir_type == NodeType::BRANCH)
		{
			const std::uint32_t num = get_node_number(&node);
			std::print(os, "%{} = branch %{} ? ${} : ${}",
			           num, get_node_number(node.inputs[0]),
			           node.inputs[1]->parent->name(),
			           node.inputs[2]->parent->name());
			return;
		}

		if (node.ir_type == NodeType::JUMP)
		{
			const std::uint32_t num = get_node_number(&node);
			std::print(os, "%{} = jump ${}", num, node.inputs[0]->parent->name());
			return;
		}

		if (node.ir_type == NodeType::CALL)
		{
			const std::uint32_t num = get_node_number(&node);
			std::print(os, "%{} = ", num);
			if (node.type_kind != DataType::VOID)
				std::print(os, "{} ", cdttstr(node, module));
			std::print(os, "call @{}(", module.strtable().get(node.inputs[0]->str_id));
			for (std::size_t i = 1; i < node.inputs.size(); ++i)
			{
				if (i > 1)
					std::print(os, ", ");
				Node *arg = node.inputs[i];
				if (arg && arg->ir_type == NodeType::LIT && arg->users.size() == 1)
				{
					std::print(os, "#");
					print_lit_v(*arg, os);
				}
				else if (arg)
				{
					std::print(os, "%{}", get_node_number(arg));
				}
				else
				{
					std::print(os, "null");
				}
			}
			std::print(os, ")");
			return;
		}

		if (node.ir_type == NodeType::INVOKE)
		{
			const std::uint32_t num = get_node_number(&node);
			std::print(os, "%{} = ", num);
			if (node.type_kind != DataType::VOID)
				std::print(os, "{} ", cdttstr(node, module));

			std::print(os, "invoke @{}, ${}, ${}",
			           module.strtable().get(node.inputs[0]->str_id),
			           node.inputs[1]->parent->name(),
			           node.inputs[2]->parent->name());
			for (std::size_t i = 3; i < node.inputs.size(); ++i)
			{
				std::print(os, ", ");
				Node *arg = node.inputs[i];
				if (arg && arg->ir_type == NodeType::LIT && arg->users.size() == 1)
				{
					std::print(os, "#");
					print_lit_v(*arg, os);
				}
				else if (arg)
				{
					std::print(os, "%{}", get_node_number(arg));
				}
				else
				{
					std::print(os, "null");
				}
			}
			return;
		}

		const std::uint32_t num = get_node_number(&node);
		if (node.ir_type == NodeType::ALLOC)
		{
			std::print(os, "%{} = alloc<{}>", num, cdttstr(node, module));
			if (!node.inputs.empty())
			{
				std::print(os, " ");
				print_operands(node.inputs, os);
			}
			return;
		}

		std::print(os, "%{} = ", num);
		if (node.type_kind != DataType::VOID)
			std::print(os, "{} ", cdttstr(node, module));

		std::print(os, "{}", ntttstr(node.ir_type));
		if (node.ir_type == NodeType::LIT)
		{
			print_lit_v(node, os);
		}
		else if (node.ir_type == NodeType::ALLOC || node.ir_type == NodeType::CAST)
		{
			std::print(os, "<{}> ", cdttstr(node, module));
			print_operands(node.inputs, os);
		}
		else if (!node.inputs.empty())
		{
			std::print(os, " ");
			print_operands(node.inputs, os);
		}
	}

	void dump_dbg(Module &module)
	{
		dump(module, std::cerr);
		std::cerr << std::endl;
	}

	void dump_dbg(Region &region, Module &module)
	{
		dump(region, module, std::cerr);
		std::cerr << std::endl;
	}

	void dump_dbg(Node &node, Module &module)
	{
		dump(node, module, std::cerr);
		std::cerr << std::endl;
	}
}
