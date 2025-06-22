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

	Node *Builder::load(Node *location)
	{
		if (!location)
			throw std::invalid_argument("load location cannot be null");

		Node *node = create_node(NodeType::LOAD, location->type_kind);
		connect_inputs(node, { location });
		return node;
	}

	Node *Builder::store(Node *value, Node *location)
	{
		if (!value || !location)
			throw std::invalid_argument("store operands cannot be null");

		Node* node = create_node(NodeType::STORE);
		connect_inputs(node, { value, location });
		return node;
	}

	StoreHelper Builder::store(Node *location)
	{
		return StoreHelper(*this, location);
	}

	Node *Builder::ptr_load(Node *pointer)
	{
		if (!pointer)
			throw std::invalid_argument("pointer cannot be null");

		if (pointer->type_kind != DataType::POINTER)
			throw std::invalid_argument("ptr_load requires pointer type");

		const auto& ptr_data = pointer->value.get<DataType::POINTER>();
		auto pointee_type = DataType::VOID;
		if (ptr_data.pointee)
			pointee_type = ptr_data.pointee->type_kind;
		else
			throw std::invalid_argument("pointee node needs to be valid");

		Node* node = create_node(NodeType::PTR_LOAD, pointee_type);
		connect_inputs(node, { pointer });
		return node;
	}

	Node* Builder::ptr_store(Node* value, Node* pointer)
	{
		if (!value || !pointer)
			throw std::invalid_argument("ptr_store operands cannot be null");

		if (pointer->type_kind != DataType::POINTER)
			throw std::invalid_argument("ptr_store requires pointer type");

		const auto& ptr_data = pointer->value.get<DataType::POINTER>();
		if (ptr_data.pointee && value->type_kind != ptr_data.pointee->type_kind)
			throw std::invalid_argument("value type must match pointer pointee type");

		Node* node = create_node(NodeType::PTR_STORE);
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

		/* both operands now have compatible types */
		DataType result_type = lhs->type_kind;

		/* comparison operations always return bool */
		if (op == NodeType::EQ || op == NodeType::NEQ ||
		    op == NodeType::LT || op == NodeType::LTE ||
		    op == NodeType::GT || op == NodeType::GTE)
		{
			result_type = DataType::BOOL;
		}

		Node *node = create_node(op, result_type);
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

		if (function->type_kind != DataType::FUNCTION)
			throw std::invalid_argument("call requires function type");

		/* determine return type from function */
		auto return_type = DataType::VOID; /* will be resolved from function signature */

		Node *node = create_node(NodeType::CALL, return_type);

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

		/* return type will be resolved from function signature */
		/* operand order: [ function, normal_target, except_target, args... ] */
		Node *node = create_node(NodeType::INVOKE, DataType::VOID);
		std::vector inputs = { function, normal_target, except_target };
		inputs.insert(inputs.end(), args.begin(), args.end());
		connect_inputs(node, inputs);
		return node;
	}

	Node* Builder::vector_build(const std::vector<Node*>& elements)
	{
	    if (elements.empty())
	        throw std::invalid_argument("vector_build requires at least one element");

	    DataType elem_type = elements[0]->type_kind;
	    for (const Node* elem : elements)
	    {
	        if (elem->type_kind != elem_type)
	            throw std::invalid_argument("all vector elements must have the same type");
	    }

	    Node* node = create_node(NodeType::VECTOR_BUILD, DataType::VECTOR);

	    DataTraits<DataType::VECTOR>::value vec_data = {};
	    vec_data.elem_type = elem_type;
	    vec_data.lane_count = static_cast<std::uint32_t>(elements.size());
	    node->value.set<decltype(vec_data), DataType::VECTOR>(vec_data);

	    connect_inputs(node, elements);
	    return node;
	}

	Node* Builder::vector_splat(Node* scalar, const std::uint32_t lane_count)
	{
	    if (!scalar)
	        throw std::invalid_argument("scalar cannot be null");

	    if (lane_count == 0)
	        throw std::invalid_argument("lane_count must be greater than 0");

	    Node* node = create_node(NodeType::VECTOR_SPLAT, DataType::VECTOR);
	    DataTraits<DataType::VECTOR>::value vec_data = {};
	    vec_data.elem_type = scalar->type_kind;
	    vec_data.lane_count = lane_count;
	    node->value.set<decltype(vec_data), DataType::VECTOR>(vec_data);

	    connect_inputs(node, {scalar});
	    return node;
	}

	Node* Builder::vector_extract(Node* vector, std::uint32_t index)
	{
	    if (!vector)
	        throw std::invalid_argument("vector cannot be null");

	    if (vector->type_kind != DataType::VECTOR)
	        throw std::invalid_argument("vector_extract requires vector type");

	    const auto& vec_data = vector->value.get<DataType::VECTOR>();

	    if (index >= vec_data.lane_count)
	        throw std::invalid_argument("vector index out of bounds");

	    Node* index_node = lit(index);
	    Node* node = create_node(NodeType::VECTOR_EXTRACT, vec_data.elem_type);
	    connect_inputs(node, { vector, index_node });
	    return node;
	}

	Node *Builder::create_node(NodeType type, DataType result_type)
	{
		if (!current_region)
			throw std::runtime_error("no current region set for node creation");

		ach::allocator<Node> alloc;
		Node *node = alloc.allocate(1);
		std::construct_at(node);
		node->ir_type = type;
		node->type_kind = result_type;
		node->parent = current_region;

		current_region->append(node);
		return node;
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

		Node *address = builder.addr_of(location);
		Node *node = builder.create_node(NodeType::ATOMIC_STORE);
		Node *ordering_node = builder.lit(static_cast<std::uint8_t>(ordering));
		Builder::connect_inputs(node, { value, address, ordering_node });
		return node;
	}

	Node *StoreHelper::through_atomic(Node *pointer, AtomicOrdering ordering)
	{
		if (!pointer)
			throw std::invalid_argument("atomic store pointer cannot be null");

		if (pointer->type_kind != DataType::POINTER)
			throw std::invalid_argument("through_atomic requires pointer type");

		Node *node = builder.create_node(NodeType::ATOMIC_STORE);
		Node *ordering_node = builder.lit(static_cast<std::uint8_t>(ordering));
		Builder::connect_inputs(node, { value, pointer, ordering_node });
		return node;
	}
}
