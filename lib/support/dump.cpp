/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <iostream>
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
			if (auto it = node_numbers.find(node);
				it != node_numbers.end())
				return it->second;

			std::uint32_t num = next_node_number++;
			node_numbers[node] = num;
			return num;
		}

		void reset_numbering()
		{
			node_numbers.clear();
			next_node_number = 1;
		}

		std::string dttstr(const DataType type)
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

		std::string cdttstr(Node &node, Module &module) // NOLINT(*-no-recursion)
		{
			switch (node.type_kind)
			{
				case DataType::POINTER:
				{
					auto &[pointee, addr_space] = node.value.get<DataType::POINTER>();
					if (pointee)
						return "ptr<" + cdttstr(*pointee, module) + ">";
					return "ptr<unknown>";
				}
				case DataType::ARRAY:
				{
					auto &arr_data = node.value.get<DataType::ARRAY>();
					return "arr<" + dttstr(arr_data.elem_type) + " x " +
					       std::to_string(arr_data.count) + ">";
				}
				case DataType::VECTOR:
				{
					auto &vec_data = node.value.get<DataType::VECTOR>();
					return "vec<" + dttstr(vec_data.elem_type) + " x " +
					       std::to_string(vec_data.lane_count) + ">";
				}
				case DataType::STRUCT:
				{
					auto &struct_data = node.value.get<DataType::STRUCT>();
					std::string result = "struct ";
					if (struct_data.name != StringTable::StringId {})
						result += module.strtable().get(struct_data.name);

					result += " {";
					for (u8slice<Node *>::size_type i = 0; i < struct_data.fields.size(); ++i)
					{
						if (i > 0)
							result += ", ";
						auto &[name_id, field_type, field_data] = struct_data.fields[i];
						result += std::string(module.strtable().get(name_id)) + ": " + dttstr(field_type);
					}
					result += "}";
					return result;
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

					auto& fn_data = node.value.get<DataType::FUNCTION>();
					DataType return_type = fn_data.return_type ? fn_data.return_type->type() : DataType::VOID;

					result += ") -> " + dttstr(return_type);
					return result;
				}
				default:
					return dttstr(node.type_kind);
			}
		}

		std::string ntttstr(const NodeType type)
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
					return ""; /* type already tells what is it */
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
				os << "export ";
			if ((node.traits & NodeTraits::DRIVER) != NodeTraits::NONE)
				os << "driver ";
			if ((node.traits & NodeTraits::EXTERN) != NodeTraits::NONE)
				os << "extern ";
			if ((node.traits & NodeTraits::VOLATILE) != NodeTraits::NONE)
				os << "volatile ";
		}

		void print_lit_v(Node &node, std::ostream &os)
		{
			switch (node.type_kind)
			{
				case DataType::BOOL:
					os << (node.value.get<DataType::BOOL>() ? "true" : "false");
					break;
				case DataType::INT8:
					os << static_cast<int>(node.value.get<DataType::INT8>());
					break;
				case DataType::INT16:
					os << node.value.get<DataType::INT16>();
					break;
				case DataType::INT32:
					os << node.value.get<DataType::INT32>();
					break;
				case DataType::INT64:
					os << node.value.get<DataType::INT64>();
					break;
				case DataType::UINT8:
					os << static_cast<unsigned>(node.value.get<DataType::UINT8>());
					break;
				case DataType::UINT16:
					os << node.value.get<DataType::UINT16>();
					break;
				case DataType::UINT32:
					os << node.value.get<DataType::UINT32>();
					break;
				case DataType::UINT64:
					os << node.value.get<DataType::UINT64>();
					break;
				case DataType::FLOAT32:
					os << node.value.get<DataType::FLOAT32>();
					break;
				case DataType::FLOAT64:
					os << node.value.get<DataType::FLOAT64>();
					break;
				default:
					os << "?";
					break;
			}
		}

		void print_operands(u8slice<Node *> &inputs, std::ostream &os)
		{
			for (u8slice<Node *>::size_type i = 0; i < inputs.size(); ++i)
			{
				if (i > 0)
					os << ", ";
				if (inputs[i])
				{
					if (inputs[i]->ir_type == NodeType::LIT && inputs[i]->users.size() == 1)
					{
						os << "#";
						print_lit_v(*inputs[i], os);
					}
					else
					{
						os << "%" << get_node_number(inputs[i]);
					}
				}
				else
				{
					os << "null";
				}
			}
		}

		void dump_regions(Region &region, Module &module, std::ostream &os) // NOLINT(*-no-recursion)
		{
			os << "    $" << region.name() << ":\n";
			for (Node *node: region.nodes())
			{
				if (node->ir_type == NodeType::ENTRY)
					os << "        entry\n";
				else if (node->ir_type == NodeType::PARAM)
					continue;
				else if (node->ir_type == NodeType::LIT && node->users.size() == 1)
					continue;
				else
				{
					os << "        ";
					dump(*node, module, os);
					os << ";\n";
				}
			}
			os << "\n";
			for (Region *child: region.children())
				dump_regions(*child, module, os);
		}
	}

	void dump(Module &module, std::ostream &os)
	{
		reset_numbering();

		os << "#! module: " << module.name() << "\n";
		const auto& typedefs = module.typemap();
		if (!typedefs.empty())
		{
			os << "section .__def\n";
			for (const auto& [name, typedef_data] : typedefs)
			{
				os << "    " << name << " = ";
				if (typedef_data.type() == DataType::STRUCT)
				{
					const auto& struct_data = typedef_data.get<DataType::STRUCT>();
					os << "struct " << name << " {";
					for (std::size_t i = 0; i < struct_data.fields.size(); ++i)
					{
						if (i > 0) os << ", ";
						auto [name_id, field_type, field_data] = struct_data.fields[i];
						os << module.strtable().get(name_id) << ": " << dttstr(field_type);
					}
					os << "}";
				}
				else
				{
					os << dttstr(typedef_data.type());
				}
				os << ";\n";
			}
			os << "end .__def\n\n";
		}

		if (auto *rodata = module.rodata())
		{
			os << "section .__rodata\n";
			for (Node *node: rodata->nodes())
			{
				if (node->ir_type != NodeType::ENTRY)
				{
					os << "    ";
					dump(*node, module, os);
					os << ";\n";
				}
			}
			os << "end .__rodata\n\n";
		}

		for (Node *func: module.functions())
		{
			if (func->ir_type == NodeType::FUNCTION)
			{
				print_node_traits(*func, os);
				auto& fn_data = func->value.get<DataType::FUNCTION>();
				DataType return_type = fn_data.return_type ? fn_data.return_type->type() : DataType::VOID;

				std::vector<Node*> params;
				for (Node* param : func->inputs)
				{
					if (param->ir_type == NodeType::PARAM)
						params.push_back(param);
				}

				os << "fn @" << module.strtable().get(func->str_id) << "(";
				for (std::size_t i = 0; i < params.size(); ++i)
				{
					if (i > 0) os << ", ";
					os << cdttstr(*params[i], module) << " %" << get_node_number(params[i]);
				}
				os << ") -> " << dttstr(return_type) << "\n{\n";
				for (Region *region: module.root()->children())
				{
					if (region->name() == module.strtable().get(func->str_id))
					{
						dump_regions(*region, module, os);
						break;
					}
				}

				os << "}\n\n";
			}
		}
	}

	void dump(Region &region, Module &module, std::ostream &os) // NOLINT(*-no-recursion)
	{
		os << "$" << region.name() << ":\n";
		for (Node *node: region.nodes())
		{
			os << "    ";
			dump(*node, module, os);
			os << ";\n";
		}

		for (Region *child: region.children())
		{
			os << "\n";
			dump(*child, module, os);
		}
	}

	void dump(Node &node, Module &module, std::ostream &os)
	{
		/* special cases that don't use standard format */
		if (node.ir_type == NodeType::ENTRY)
		{
			os << "entry";
			return;
		}

		if (node.ir_type == NodeType::RET)
		{
			os << "ret";
			if (!node.inputs.empty())
			{
				os << " ";
				print_operands(node.inputs, os);
			}
			return;
		}

		if (node.ir_type == NodeType::BRANCH)
		{
			const std::uint32_t num = get_node_number(&node);
			os << "%" << num << " = branch ";
			os << "%" << get_node_number(node.inputs[0]) << " ? ";
			os << "$" << node.inputs[1]->parent->name() << " : ";
			os << "$" << node.inputs[2]->parent->name();
			return;
		}

		if (node.ir_type == NodeType::JUMP)
		{
			const std::uint32_t num = get_node_number(&node);
			os << "%" << num << " = jump $" << node.inputs[0]->parent->name();
			return;
		}

		if (node.ir_type == NodeType::CALL)
		{
			const std::uint32_t num = get_node_number(&node);
			os << std::format("%{} = ", num);
			if (node.type_kind != DataType::VOID)
				os << cdttstr(node, module) << " ";
			os << std::format("call @{}(", module.strtable().get(node.inputs[0]->str_id));
			for (std::size_t i = 1; i < node.inputs.size(); ++i)
			{
				if (i > 1)
					os << ", ";
				Node* arg = node.inputs[i];
				if (arg && arg->ir_type == NodeType::LIT && arg->users.size() == 1)
				{
					os << "#";
					print_lit_v(*arg, os);
				}
				else if (arg)
				{
					os << "%" << get_node_number(arg);
				}
				else
				{
					os << "null";
				}
			}
			os << ")";
			return;
		}

		if (node.ir_type == NodeType::INVOKE)
		{
			const std::uint32_t num = get_node_number(&node);
			os << "%" << num << " = ";
			if (node.type_kind != DataType::VOID)
				os << cdttstr(node, module) << " ";

			os << "invoke @" << module.strtable().get(node.inputs[0]->str_id);
			os << ", $" << node.inputs[1]->parent->name();
			os << ", $" << node.inputs[2]->parent->name();
			for (std::size_t i = 3; i < node.inputs.size(); ++i)
			{
				os << ", ";
				Node* arg = node.inputs[i];
				if (arg && arg->ir_type == NodeType::LIT && arg->users.size() == 1)
				{
					os << "#";
					print_lit_v(*arg, os);
				}
				else if (arg)
				{
					os << "%" << get_node_number(arg);
				}
				else
				{
					os << "null";
				}
			}
			return;
		}

		const std::uint32_t num = get_node_number(&node);
		if (node.ir_type == NodeType::ALLOC)
		{
			os << "%" << num << " = alloc<" << cdttstr(node, module) << ">";
			if (!node.inputs.empty())
			{
				os << " ";
				print_operands(node.inputs, os);
			}
			return;
		}

		os << "%" << num << " = ";

		/* print type for typed operations */
		if (node.type_kind != DataType::VOID)
			os << cdttstr(node, module) << " ";

		os << ntttstr(node.ir_type); /* print operation */
		if (node.ir_type == NodeType::LIT)
		{
			print_lit_v(node, os);
		}
		else if (node.ir_type == NodeType::ALLOC || node.ir_type == NodeType::CAST)
		{
			os << "<" << cdttstr(node, module) << "> ";
			print_operands(node.inputs, os);
		}
		else if (!node.inputs.empty())
		{
			os << " ";
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
