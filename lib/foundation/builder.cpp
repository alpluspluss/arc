/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <stdexcept>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/region.hpp>
#include <arc/support/allocator.hpp>
#include <arc/support/inference.hpp>

namespace arc
{
	Builder::Builder(Module &module) : module(module), current_region(module.root()) {}

	void Builder::set_insertion_point(Region *region)
	{
		current_region = region;
	}

	Region *Builder::get_insertion_point() const
	{
		return current_region;
	}

	Node *Builder::alloc(const TypedData &type_def)
	{
		Node *node = create_node(NodeType::ALLOC, type_def.type());
		node->value = type_def;
		return node;
	}

	Node *Builder::load(Node *location)
	{
		if (!location)
			throw std::invalid_argument("load location cannot be null");

		/* determine what type we're loading based on the memory location type.
		 * Arc supports two main loading patterns: direct field access from structs
		 * and indirect access through stored pointers/values */
		DataType result_type = location->type_kind;
		Node *type_source = nullptr;

		/* struct field access uses the field's declared type from the type system.
		 * the ACCESS node already contains complete type information including
		 * pointer metadata, so we don't need to chase use-def chains */
		if (location->ir_type == NodeType::ACCESS)
		{
			result_type = location->type_kind;
			type_source = location;
		}
		else
		{
			/* memory location access requires finding what was stored there.
			 * we trace through the use-def chain to find either the original
			 * allocation type or the most recent store operation */
			for (Node *user: location->users)
			{
				if (user->ir_type == NodeType::ALLOC)
				{
					/* allocation determines the base type of the storage location */
					result_type = user->type_kind;
					break;
				}
				if ((user->ir_type == NodeType::STORE || user->ir_type == NodeType::PTR_STORE) &&
				    user->inputs.size() >= 2 && user->inputs[1] == location)
				{
					/* store operation determines the actual value type and metadata */
					result_type = user->inputs[0]->type_kind;
					type_source = user->inputs[0];
					break;
				}
			}
		}

		Node *node = create_node(NodeType::LOAD, result_type);

		/* preserve complex type metadata for pointer and function types.
		 * this ensures that loading a function pointer retains signature information
		 * and loading a data pointer retains pointee type for alias analysis */
		if (type_source && (result_type == DataType::POINTER || result_type == DataType::FUNCTION))
		{
			if (type_source->value.type() != DataType::VOID)
			{
				node->value = type_source->value;
			}
		}

		connect_inputs(node, { location });
		return node;
	}

	Node *Builder::store(Node *value, Node *location)
	{
		if (!value || !location)
			throw std::invalid_argument("store operands cannot be null");

		Node *node = create_node(NodeType::STORE);
		connect_inputs(node, { value, location });
		return node;
	}

	StoreHelper Builder::store(Node *location)
	{
		return { *this, location };
	}

	Node *Builder::ptr_load(Node *pointer)
	{
		if (!pointer)
			throw std::invalid_argument("pointer cannot be null");

		/* handle ACCESS nodes that represent field/element addresses */
		if (pointer->ir_type == NodeType::ACCESS)
		{
			/* ACCESS nodes from struct field or array index operations represent
			 * addresses that can be loaded from, even though they're not technically
			 * DataType::POINTER. the result type is determined by the ACCESS node's type_kind */
			Node *node = create_node(NodeType::PTR_LOAD, pointer->type_kind);

			/* preserve complex type metadata if present */
			if (pointer->value.type() != DataType::VOID &&
				(pointer->type_kind == DataType::STRUCT || pointer->type_kind == DataType::POINTER ||
				 pointer->type_kind == DataType::ARRAY || pointer->type_kind == DataType::VECTOR))
			{
				node->value = pointer->value;
			}

			connect_inputs(node, { pointer });
			return node;
		}

		if (pointer->type_kind != DataType::POINTER)
			throw std::invalid_argument("ptr_load requires pointer type or ACCESS node");

		const auto &ptr_data = pointer->value.get<DataType::POINTER>();
		auto pointee_type = DataType::VOID;
		if (ptr_data.pointee)
			pointee_type = ptr_data.pointee->type_kind;
		else
			throw std::invalid_argument("pointee node needs to be valid");

		Node *node = create_node(NodeType::PTR_LOAD, pointee_type);
		connect_inputs(node, { pointer });
		return node;
	}

	Node *Builder::ptr_store(Node *value, Node *pointer)
	{
		if (!value || !pointer)
			throw std::invalid_argument("ptr_store operands cannot be null");

		if (pointer->type_kind != DataType::POINTER)
			throw std::invalid_argument("ptr_store requires pointer type");

		const auto &[pointee, addr_space, qual] = pointer->value.get<DataType::POINTER>();
		if (pointee)
		{
			if (value->type_kind != pointee->type_kind)
				throw std::invalid_argument("value type must match pointer pointee type");
		}

		Node *node = create_node(NodeType::PTR_STORE);
		connect_inputs(node, { value, pointer });
		return node;
	}

	Node *Builder::addr_of(Node *variable)
	{
		if (!variable)
			throw std::invalid_argument("variable cannot be null");

		Node *node = create_node(NodeType::ADDR_OF, DataType::POINTER);

		DataTraits<DataType::POINTER>::value ptr_data = {};
		ptr_data.pointee = variable;
		ptr_data.addr_space = 0;
		node->value.set<decltype(ptr_data), DataType::POINTER>(ptr_data);

		connect_inputs(node, { variable });
		return node;
	}

	Node *Builder::ptr_add(Node *base_pointer, Node *offset)
	{
		if (!base_pointer || !offset)
			throw std::invalid_argument("ptr_add operands cannot be null");

		if (base_pointer->type_kind != DataType::POINTER)
			throw std::invalid_argument("ptr_add requires pointer base");

		Node *node = create_node(NodeType::PTR_ADD, DataType::POINTER);

		/* copy pointer type data from base */
		const auto &base_ptr_data = base_pointer->value.get<DataType::POINTER>();
		node->value.set<decltype(base_ptr_data), DataType::POINTER>(base_ptr_data);

		connect_inputs(node, { base_pointer, offset });
		return node;
	}

	Node *Builder::binary_op(const NodeType op, Node *lhs, Node *rhs)
	{
		if (!lhs || !rhs)
			throw std::invalid_argument("binary operation operands cannot be null");

		/* use type inference to determine result type */
		if (!infer_binary_t(lhs, rhs))
			throw std::invalid_argument("incompatible types for binary operation");

		/* comparison operations always return bool;
		 * if the result type is VECTOR, copy the vector data from lhs */
		DataType result_type = lhs->type_kind;
		if (op == NodeType::EQ || op == NodeType::NEQ ||
		    op == NodeType::LT || op == NodeType::LTE ||
		    op == NodeType::GT || op == NodeType::GTE)
		{
			result_type = DataType::BOOL;
		}

		Node *node = create_node(op, result_type);
		if (result_type == DataType::VECTOR && lhs->value.type() == DataType::VECTOR)
			node->value = lhs->value;
		connect_inputs(node, { lhs, rhs });
		return node;
	}

	Node *Builder::add(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::ADD, lhs, rhs);
	}

	Node *Builder::sub(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::SUB, lhs, rhs);
	}

	Node *Builder::mul(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::MUL, lhs, rhs);
	}

	Node *Builder::div(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::DIV, lhs, rhs);
	}

	Node *Builder::mod(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::MOD, lhs, rhs);
	}

	Node *Builder::band(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::BAND, lhs, rhs);
	}

	Node *Builder::bor(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::BOR, lhs, rhs);
	}

	Node *Builder::bxor(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::BXOR, lhs, rhs);
	}

	Node *Builder::bnot(Node *value)
	{
		if (!value)
			throw std::invalid_argument("bnot operand cannot be null");

		Node *node = create_node(NodeType::BNOT, value->type_kind);
		connect_inputs(node, { value });
		return node;
	}

	Node *Builder::bshl(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::BSHL, lhs, rhs);
	}

	Node *Builder::bshr(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::BSHR, lhs, rhs);
	}

	Node *Builder::eq(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::EQ, lhs, rhs);
	}

	Node *Builder::neq(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::NEQ, lhs, rhs);
	}

	Node *Builder::lt(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::LT, lhs, rhs);
	}

	Node *Builder::lte(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::LTE, lhs, rhs);
	}

	Node *Builder::gt(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::GT, lhs, rhs);
	}

	Node *Builder::gte(Node *lhs, Node *rhs)
	{
		return binary_op(NodeType::GTE, lhs, rhs);
	}

	Node *Builder::call(Node *function, const std::vector<Node *> &args)
	{
		if (!function)
			throw std::invalid_argument("function cannot be null");

		DataType return_type = DataType::VOID;
		TypedData *return_type_data = nullptr;

		switch (function->type_kind)
		{
			case DataType::FUNCTION:
			{
				const auto &fn_data = function->value.get<DataType::FUNCTION>();
				if (fn_data.return_type)
				{
					return_type = fn_data.return_type->type();
					return_type_data = fn_data.return_type;
				}
				break;
			}
			case DataType::POINTER:
			{
				const auto &ptr_data = function->value.get<DataType::POINTER>();
				if (!ptr_data.pointee || ptr_data.pointee->type_kind != DataType::FUNCTION)
					throw std::invalid_argument("pointer must point to function type");

				if (ptr_data.pointee->value.type() == DataType::FUNCTION)
				{
					const auto &fn_data = ptr_data.pointee->value.get<DataType::FUNCTION>();
					if (fn_data.return_type)
					{
						return_type = fn_data.return_type->type();
						return_type_data = fn_data.return_type;
					}
				}
				break;
			}
			default:
				throw std::invalid_argument("call target must be function or function pointer");
		}

		Node *node = create_node(NodeType::CALL, return_type);

		/* preserve complex type information for TBAA */
		if (return_type_data &&
		    (return_type == DataType::POINTER || return_type == DataType::STRUCT ||
		     return_type == DataType::ARRAY || return_type == DataType::VECTOR))
		{
			node->value = *return_type_data;
		}

		std::vector<Node *> inputs = { function };
		inputs.insert(inputs.end(), args.begin(), args.end());
		connect_inputs(node, inputs);
		return node;
	}

	Node *Builder::ret(Node *value)
	{
		Node *node = create_node(NodeType::RET);
		if (value)
			connect_inputs(node, { value });
		return node;
	}

	Node *Builder::branch(Node *condition, Node *true_target, Node *false_target)
	{
		if (!condition || !true_target || !false_target)
			throw std::invalid_argument("branch operands cannot be null");

		if (condition->type_kind != DataType::BOOL)
			throw std::invalid_argument("branch condition must be bool type");

		if (true_target->ir_type != NodeType::ENTRY || false_target->ir_type != NodeType::ENTRY)
			throw std::invalid_argument("branch targets must be ENTRY nodes");

		Node *node = create_node(NodeType::BRANCH);
		connect_inputs(node, { condition, true_target, false_target });
		return node;
	}

	Node *Builder::jump(Node *target)
	{
		if (!target)
			throw std::invalid_argument("jump target cannot be null");

		if (target->ir_type != NodeType::ENTRY)
			throw std::invalid_argument("jump target must be ENTRY node");

		Node *node = create_node(NodeType::JUMP);
		connect_inputs(node, { target });
		return node;
	}

	Node *Builder::invoke(Node *function, const std::vector<Node *> &args, Node *normal_target, Node *except_target)
	{
		if (!function || !normal_target || !except_target)
			throw std::invalid_argument("invoke operands cannot be null");

		if (function->type_kind != DataType::FUNCTION)
			throw std::invalid_argument("invoke requires function type");

		if (normal_target->ir_type != NodeType::ENTRY || except_target->ir_type != NodeType::ENTRY)
			throw std::invalid_argument("invoke targets must be ENTRY nodes");

		auto &fn_data = function->value.get<DataType::FUNCTION>();
		DataType return_type = fn_data.return_type ? fn_data.return_type->type() : DataType::VOID;

		Node *node = create_node(NodeType::INVOKE, return_type);
		if (return_type != DataType::VOID && fn_data.return_type)
			node->value = *fn_data.return_type;

		std::vector inputs = { function, normal_target, except_target };
		inputs.insert(inputs.end(), args.begin(), args.end());
		connect_inputs(node, inputs);
		return node;
	}

	Node *Builder::select(Node *condition, Node *true_value, Node *false_value)
	{
		if (!condition || !true_value || !false_value)
			throw std::invalid_argument("select operands cannot be null");

		if (condition->type_kind != DataType::BOOL)
			throw std::invalid_argument("select condition must be boolean type");

		if (true_value->type_kind != false_value->type_kind)
			throw std::invalid_argument("select operands must have matching types");

		Node *node = create_node(NodeType::SELECT, true_value->type_kind);
		connect_inputs(node, { condition, true_value, false_value });
		return node;
	}

	Node *Builder::vector_build(const std::vector<Node *> &elements)
	{
		if (elements.empty())
			throw std::invalid_argument("vector_build requires at least one element");

		DataType elem_type = elements[0]->type_kind;
		for (const Node *elem: elements)
		{
			if (elem->type_kind != elem_type)
				throw std::invalid_argument("all vector elements must have the same type");
		}

		Node *node = create_node(NodeType::VECTOR_BUILD, DataType::VECTOR);

		DataTraits<DataType::VECTOR>::value vec_data = {};
		vec_data.elem_type = elem_type;
		vec_data.lane_count = static_cast<std::uint32_t>(elements.size());
		node->value.set<decltype(vec_data), DataType::VECTOR>(vec_data);

		connect_inputs(node, elements);
		return node;
	}

	Node *Builder::vector_splat(Node *scalar, const std::uint32_t lane_count)
	{
		if (!scalar)
			throw std::invalid_argument("scalar cannot be null");

		if (lane_count == 0)
			throw std::invalid_argument("lane_count must be greater than 0");

		Node *node = create_node(NodeType::VECTOR_SPLAT, DataType::VECTOR);
		DataTraits<DataType::VECTOR>::value vec_data = {};
		vec_data.elem_type = scalar->type_kind;
		vec_data.lane_count = lane_count;
		node->value.set<decltype(vec_data), DataType::VECTOR>(vec_data);

		connect_inputs(node, { scalar });
		return node;
	}

	Node *Builder::vector_extract(Node *vector, std::uint32_t index)
	{
		if (!vector)
			throw std::invalid_argument("vector cannot be null");

		if (vector->type_kind != DataType::VECTOR)
			throw std::invalid_argument("vector_extract requires vector type");

		const auto &vec_data = vector->value.get<DataType::VECTOR>();

		if (index >= vec_data.lane_count)
			throw std::invalid_argument("vector index out of bounds");

		Node *index_node = lit(index);
		Node *node = create_node(NodeType::VECTOR_EXTRACT, vec_data.elem_type);
		connect_inputs(node, { vector, index_node });
		return node;
	}

	Node *Builder::struct_field(Node *struct_obj, const std::string &field_name)
	{
		if (!struct_obj)
			throw std::invalid_argument("struct_field requires non-null struct object");

		const DataTraits<DataType::STRUCT>::value* struct_data = nullptr;

		/* handle ACCESS nodes from previous field access or array indexing FIRST */
		if (struct_obj->ir_type == NodeType::ACCESS && struct_obj->type_kind == DataType::STRUCT)
		{
			if (struct_obj->value.type() != DataType::STRUCT)
			{
				/* try to find struct type from module registry using string id
				 * if it's not found */
				if (struct_obj->str_id != 0)
				{
					const auto& typemap = module.typemap();
					const std::string_view struct_name = module.strtable().get(struct_obj->str_id);
					if (const auto it = typemap.find(std::string(struct_name));
						it != typemap.end() && it->second.type() == DataType::STRUCT)
					{
						/* then update the ACCESS node with proper type info for future operations */
						struct_obj->value = it->second;
						struct_data = &struct_obj->value.get<DataType::STRUCT>();
					}
					else
					{
						throw std::invalid_argument("ACCESS node missing struct type information and cannot find in module registry");
					}
				}
				else
				{
					throw std::invalid_argument("ACCESS node missing struct type information");
				}
			}
			else
			{
				struct_data = &struct_obj->value.get<DataType::STRUCT>();
			}
		}
		/* handle ALLOC nodes that allocated struct types */
		else if (struct_obj->ir_type == NodeType::ALLOC && struct_obj->type_kind == DataType::STRUCT)
		{
			if (struct_obj->value.type() != DataType::STRUCT)
				throw std::invalid_argument("struct allocation missing type information");
			struct_data = &struct_obj->value.get<DataType::STRUCT>();
		}
		/* handle direct struct nodes - allocation or literal struct values */
		else if (struct_obj->type_kind == DataType::STRUCT)
		{
			if (struct_obj->value.type() != DataType::STRUCT)
				throw std::invalid_argument("struct node missing type information");
			struct_data = &struct_obj->value.get<DataType::STRUCT>();
		}
		/* handle pointer-to-struct (from addr_of operations) */
		else if (struct_obj->type_kind == DataType::POINTER)
		{
			const auto &ptr_data = struct_obj->value.get<DataType::POINTER>();
			if (!ptr_data.pointee)
				throw std::invalid_argument("pointer has null pointee");

			if (ptr_data.pointee->type_kind != DataType::STRUCT)
				throw std::invalid_argument("pointer must point to struct type");

			if (ptr_data.pointee->value.type() != DataType::STRUCT)
				throw std::invalid_argument("pointee struct missing type information");
			struct_data = &ptr_data.pointee->value.get<DataType::STRUCT>();
		}
		else
		{
			throw std::invalid_argument("struct_field requires struct type, pointer-to-struct, or struct ACCESS node");
		}

		/* find the field in the struct definition */
		DataType field_type = DataType::VOID;
		std::size_t field_index = 0;
		TypedData field_type_data = {};
		bool found_field = false;

		for (std::uint8_t i = 0; i < struct_data->fields.size(); ++i)
		{
			const auto &[name_id, ftype, fdata] = struct_data->fields[i];
			std::string_view current_field_name = module.strtable().get(name_id);

			/* skip compiler-generated padding fields */
			if (current_field_name.starts_with("__pad"))
				continue;

			if (current_field_name == field_name)
			{
				field_type = ftype;
				field_index = i; /* use actual index including padding for offset calculation */
				field_type_data = fdata;
				found_field = true;
				break;
			}
		}

		if (!found_field)
			throw std::invalid_argument("field not found: " + field_name);

		Node *field_index_node = lit(static_cast<std::uint32_t>(field_index));
		Node *node = create_node(NodeType::ACCESS, field_type);

		/* preserve complete type metadata for complex types to enable nested access */
		if (field_type == DataType::STRUCT || field_type == DataType::POINTER ||
			field_type == DataType::ARRAY || field_type == DataType::VECTOR ||
			field_type == DataType::FUNCTION)
		{
			node->value = field_type_data;
		}

		/* for struct fields, also store the struct name for type registry lookup */
		if (field_type == DataType::STRUCT && field_type_data.type() == DataType::STRUCT)
		{
			const auto& nested_struct_data = field_type_data.get<DataType::STRUCT>();
			node->str_id = nested_struct_data.name;
		}

		connect_inputs(node, { struct_obj, field_index_node });
		return node;
	}

	StructBuilder Builder::struct_type(std::string_view name)
	{
		return { *this, name };
	}

	Node *Builder::array_index(Node *array, Node *index)
	{
		if (!array || !index)
			throw std::invalid_argument("array_index operands cannot be null");

		if (array->type_kind != DataType::ARRAY)
			throw std::invalid_argument("array_index accepts only array type");

		auto &arr_data = array->value.get<DataType::ARRAY>();
		DataType elem_type = arr_data.elem_type;

		Node *node = create_node(NodeType::ACCESS, elem_type);

		/* we preserve struct type data for element access */
		if (elem_type == DataType::STRUCT)
		{
			if (array->ir_type == NodeType::ALLOC && array->str_id != 0)
			{
				const auto& typemap = module.typemap();
				std::string_view struct_name = module.strtable().get(array->str_id);
				auto it = typemap.find(std::string(struct_name));
				if (it != typemap.end() && it->second.type() == DataType::STRUCT)
				{
					node->value = it->second;
					node->str_id = array->str_id;
				}
			}
		}

		connect_inputs(node, { array, index });
		return node;
	}

	Node *Builder::create_node(NodeType type, DataType result_type)
	{
		if (!current_region)
			throw std::runtime_error("no current region set for node creation");

		ach::shared_allocator<Node> alloc;
		Node *node = alloc.allocate(1);
		std::construct_at(node);
		node->ir_type = type;
		node->type_kind = result_type;
		node->parent = current_region;
		current_region->append(node);
		return node;
	}

	Node *Builder::from(const std::vector<Node *> &sources)
	{
		if (sources.empty())
			throw std::invalid_argument("FROM node requires at least one source");

		DataType result_type = sources[0]->type_kind;
		for (const Node *source: sources)
		{
			if (!source)
				throw std::invalid_argument("FROM node cannot have null sources");

			if (source->type_kind != result_type)
			{
				DataType promoted = infer_primitive_types(result_type, source->type_kind);
				if (promoted == DataType::VOID)
					throw std::invalid_argument("FROM node sources have incompatible types");
				result_type = promoted;
			}
		}

		Node *from_node = create_node(NodeType::FROM, result_type);
		connect_inputs(from_node, sources);
		return from_node;
	}

	Node *Builder::array_alloc(const TypedData &struct_type, std::uint32_t count, std::uint32_t n)
	{
		if (struct_type.type() != DataType::STRUCT)
			throw std::invalid_argument("struct_type must be DataType::STRUCT");

		Node* count_lit = lit(n);
		Node* alloc_node = create_node(NodeType::ALLOC, DataType::ARRAY);

		DataTraits<DataType::ARRAY>::value arr_data;
		arr_data.elem_type = DataType::STRUCT;
		arr_data.count = count;
		arr_data.elements = {};
		alloc_node->value.set<decltype(arr_data), DataType::ARRAY>(arr_data);

		alloc_node->str_id = struct_type.get<DataType::STRUCT>().name;

		connect_inputs(alloc_node, { count_lit });
		return alloc_node;
	}

	void Builder::connect_inputs(Node *node, const std::vector<Node *> &inputs)
	{
		if (!node)
			return;

		for (Node *input: inputs)
		{
			if (input)
			{
				node->inputs.push_back(input);
				input->users.push_back(node);
			}
		}
	}

	StructBuilder::StructBuilder(Builder &builder, const std::string_view name) : builder(builder),
		struct_name_id(builder.module.intern_str(name)) {}

	StructBuilder &StructBuilder::field(const std::string_view name, DataType type, const TypedData &type_data)
	{
		fields.emplace_back(builder.module.intern_str(name), type, std::move(type_data));
		return *this;
	}

	StructBuilder &StructBuilder::field_ptr(const std::string_view name, Node *pointee_type, std::uint32_t addr_space)
	{
		/* you typically don't really care what default value actually is as it simply is to declare a definition,
		 * so we can just use an empty TypedData here */
		TypedData ptr_type_data = {};
		DataTraits<DataType::POINTER>::value ptr_data = {};
		ptr_data.pointee = pointee_type; /* can be nullptr for forward/self refs */
		ptr_data.addr_space = addr_space;
		ptr_type_data.set<decltype(ptr_data), DataType::POINTER>(ptr_data);
		fields.emplace_back(builder.module.intern_str(name), DataType::POINTER, std::move(ptr_type_data));
		return *this;
	}

	StructBuilder &StructBuilder::self_ptr(const std::string_view name, const std::uint32_t addr_space)
	{
		/* self-referential pointer field. pointee = nullptr for forward reference */
		TypedData ptr_type_data = {};
		DataTraits<DataType::POINTER>::value ptr_data = {};
		ptr_data.pointee = nullptr;
		ptr_data.addr_space = addr_space;
		ptr_type_data.set<decltype(ptr_data), DataType::POINTER>(ptr_data);
		fields.emplace_back(builder.module.intern_str(name), DataType::POINTER, std::move(ptr_type_data));
		return *this;
	}

	TypedData StructBuilder::build(const std::uint32_t alignment)
	{
		DataTraits<DataType::STRUCT>::value struct_data = {};
		struct_data.alignment = is_packed ? 1 : alignment;
		struct_data.name = struct_name_id;

		u8slice<std::tuple<StringTable::StringId, DataType, TypedData> > final_fields = {};
		std::size_t current_offset = 0;
		for (const auto &[name_id, type, type_data]: fields)
		{
			if (!is_packed)
			{
				const std::size_t field_align = align_t(type);
				if (std::size_t padding_needed = (field_align - (current_offset % field_align)) % field_align;
					padding_needed > 0)
				{
					auto padding_name = builder.module.intern_str("__pad" + std::to_string(final_fields.size()));
					TypedData padding_data;
					set_t(padding_data, padding_t(padding_needed));

					final_fields.emplace_back(padding_name, padding_t(padding_needed), padding_data);
					current_offset += padding_needed;
				}
			}

			final_fields.emplace_back(name_id, type, type_data);
			current_offset += elem_sz(type);
		}

		if (!is_packed)
		{
			if (const std::size_t final_padding =
						(struct_data.alignment - (current_offset % struct_data.alignment)) % struct_data.alignment;
				final_padding > 0)
			{
				auto padding_name = builder.module.intern_str("__pad_final");
				TypedData padding_data;
				set_t(padding_data, padding_t(final_padding));

				final_fields.emplace_back(padding_name, padding_t(final_padding), padding_data);
			}
		}

		struct_data.fields = std::move(final_fields);
		TypedData type_def;
		type_def.set<decltype(struct_data), DataType::STRUCT>(std::move(struct_data));
		builder.module.add_t(builder.module.strtable().get(struct_data.name).data(), type_def);
		return type_def;
	}

	StructBuilder &StructBuilder::packed()
	{
		is_packed = true;
		return *this;
	}

	StoreHelper::StoreHelper(Builder &builder, Node *value) : builder(builder), value(value) {}

	Node *StoreHelper::to(Node *location)
	{
		if (!location)
			throw std::invalid_argument("store location cannot be null");

		Node *node = builder.create_node(NodeType::STORE);
		Builder::connect_inputs(node, { value, location });
		return node;
	}

	Node *StoreHelper::to(Node *location, Node *offset)
	{
		if (!location || !offset)
			throw std::invalid_argument("store location and offset cannot be null");

		/* create pointer arithmetic for the offset */
		Node *address = builder.addr_of(location);
		Node *target_address = builder.ptr_add(address, offset);

		Node *node = builder.create_node(NodeType::PTR_STORE);
		Builder::connect_inputs(node, { value, target_address });
		return node;
	}

	Node *StoreHelper::through(Node *pointer)
	{
		if (!pointer)
			throw std::invalid_argument("store pointer cannot be null");

		if (pointer->type_kind != DataType::POINTER)
			throw std::invalid_argument("through requires pointer type");

		Node *node = builder.create_node(NodeType::PTR_STORE);
		Builder::connect_inputs(node, { value, pointer });
		return node;
	}

	Node *StoreHelper::to_atomic(Node *location, AtomicOrdering ordering)
	{
		if (!location)
			throw std::invalid_argument("atomic store location cannot be null");

		Node *ordering_node = builder.lit(static_cast<std::uint8_t>(ordering));
		Node *address = builder.addr_of(location);
		Node *node = builder.create_node(NodeType::ATOMIC_STORE);
		Builder::connect_inputs(node, { value, address, ordering_node });
		return node;
	}

	Node *StoreHelper::through_atomic(Node *pointer, AtomicOrdering ordering)
	{
		if (!pointer)
			throw std::invalid_argument("atomic store pointer cannot be null");

		if (pointer->type_kind != DataType::POINTER)
			throw std::invalid_argument("through_atomic requires pointer type");

		Node *ordering_node = builder.lit(static_cast<std::uint8_t>(ordering));
		Node *node = builder.create_node(NodeType::ATOMIC_STORE);
		Builder::connect_inputs(node, { value, pointer, ordering_node });
		return node;
	}
}
