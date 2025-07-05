/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/support/dump.hpp>
#include <gtest/gtest.h>

class DumpFixture : public testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("test_module");
		builder = std::make_unique<arc::Builder>(*module);
	}

	void TearDown() override
	{
		builder.reset();
		module.reset();
	}

	std::unique_ptr<arc::Module> module;
	std::unique_ptr<arc::Builder> builder;
};

TEST_F(DumpFixture, BasicLiterals)
{
	builder->function<arc::DataType::VOID>("literals")
			.body([](arc::Builder &fb)
			{
				[[maybe_unused]] auto *bool_lit = fb.lit(true);
				[[maybe_unused]] auto *int8_lit = fb.lit(static_cast<std::int8_t>(-42));
				[[maybe_unused]] auto *int16_lit = fb.lit(static_cast<std::int16_t>(1000));
				[[maybe_unused]] auto *int32_lit = fb.lit(42);
				[[maybe_unused]] auto *int64_lit = fb.lit(static_cast<std::int64_t>(9223372036854775807));
				[[maybe_unused]] auto *uint8_lit = fb.lit(static_cast<std::uint8_t>(255));
				[[maybe_unused]] auto *uint16_lit = fb.lit(static_cast<std::uint16_t>(65535));
				[[maybe_unused]] auto *uint32_lit = fb.lit(static_cast<std::uint32_t>(4294967295));
				[[maybe_unused]] auto *uint64_lit = fb.lit(static_cast<std::uint64_t>(18446744073709551615ULL));
				[[maybe_unused]] auto *float32_lit = fb.lit(3.14f);
				[[maybe_unused]] auto *float64_lit = fb.lit(2.718281828459045);

				return fb.ret();
			});

	dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, ArithmeticOperations)
{
	builder->function<arc::DataType::INT32>("arithmetic")
			.param<arc::DataType::INT32>("x")
			.param<arc::DataType::INT32>("y")
			.body([](arc::Builder &fb, arc::Node *x, arc::Node *y)
			{
				auto *sum = fb.add(x, y);
				[[maybe_unused]] auto *diff = fb.sub(x, y);
				[[maybe_unused]] auto *prod = fb.mul(x, y);
				[[maybe_unused]] auto *quot = fb.div(x, y);
				[[maybe_unused]] auto *rem = fb.mod(x, y);

				[[maybe_unused]] auto *band_result = fb.band(x, y);
				[[maybe_unused]] auto *bor_result = fb.bor(x, y);
				[[maybe_unused]] auto *bxor_result = fb.bxor(x, y);
				[[maybe_unused]] auto *bnot_result = fb.bnot(x);
				[[maybe_unused]] auto *bshl_result = fb.bshl(x, fb.lit(2));
				[[maybe_unused]] auto *bshr_result = fb.bshr(x, fb.lit(2));

				return fb.ret(sum);
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, ComparisonOperations)
{
	builder->function<arc::DataType::BOOL>("comparisons")
			.param<arc::DataType::INT32>("a")
			.param<arc::DataType::INT32>("b")
			.body([](arc::Builder &fb, arc::Node *a, arc::Node *b)
			{
				auto *eq_result = fb.eq(a, b);
				[[maybe_unused]] auto *neq_result = fb.neq(a, b);
				[[maybe_unused]] auto *lt_result = fb.lt(a, b);
				[[maybe_unused]] auto *lte_result = fb.lte(a, b);
				[[maybe_unused]] auto *gt_result = fb.gt(a, b);
				[[maybe_unused]] auto *gte_result = fb.gte(a, b);

				return fb.ret(eq_result);
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, MemoryOperations)
{
	builder->function<arc::DataType::VOID>("memory_ops")
			.body([](arc::Builder &fb)
			{
				auto *count = fb.lit(4);
				auto *alloc_node = fb.alloc<arc::DataType::INT32>(count);

				auto *value = fb.lit(42);
				[[maybe_unused]] auto *store_op = fb.store(value, alloc_node);
				[[maybe_unused]] auto *load_op = fb.load(alloc_node);

				auto *addr = fb.addr_of(alloc_node);
				auto *offset = fb.lit(4);
				[[maybe_unused]] auto *ptr_add_result = fb.ptr_add(addr, offset);
				[[maybe_unused]] auto *ptr_store_op = fb.ptr_store(value, addr);
				[[maybe_unused]] auto *ptr_load_op = fb.ptr_load(addr);

				return fb.ret();
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, VectorOperations)
{
	builder->function<arc::DataType::VOID>("vector_ops")
			.body([](arc::Builder &fb)
			{
				auto *elem1 = fb.lit(1.0f);
				auto *elem2 = fb.lit(2.0f);
				auto *elem3 = fb.lit(3.0f);
				auto *elem4 = fb.lit(4.0f);

				auto *vec = fb.vector_build({ elem1, elem2, elem3, elem4 });

				auto *scalar = fb.lit(5.0f);
				[[maybe_unused]] auto *splat_vec = fb.vector_splat(scalar, 8);

				fb.vector_extract(vec, 2);
				return fb.ret();
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, ControlFlow)
{
	builder->function<arc::DataType::INT32>("control_flow")
			.param<arc::DataType::BOOL>("condition")
			.body([](arc::Builder &fb, arc::Node *condition)
			{
				const auto true_block = fb.block<arc::DataType::VOID>("true_path");
				const auto false_block = fb.block<arc::DataType::VOID>("false_path");
				const auto merge_block = fb.block<arc::DataType::VOID>("merge");
				fb.branch(condition, true_block.entry(), false_block.entry());
				fb.jump(merge_block.entry());
				return fb.ret(fb.lit(0));
			});

	dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, FunctionCalls)
{
	auto *helper = builder->function<arc::DataType::INT32>("helper")
			.param<arc::DataType::INT32>("x")
			.param<arc::DataType::INT32>("y")
			.body([](arc::Builder &fb, arc::Node *x, arc::Node *y)
			{
				auto *result = fb.add(x, y);
				return fb.ret(result);
			});

	builder->function<arc::DataType::INT32>("caller")
			.body([helper](arc::Builder &fb)
			{
				auto *arg1 = fb.lit(10);
				auto *arg2 = fb.lit(20);
				auto *call_result = fb.call(helper, { arg1, arg2 });

				return fb.ret(call_result);
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, InvokeOperations)
{
	auto *risky_func = builder->function<arc::DataType::INT32>("risky")
			.param<arc::DataType::INT32>("x")
			.body([](arc::Builder &fb, arc::Node *x)
			{
				return fb.ret(x);
			});

	builder->function<arc::DataType::INT32>("invoke_test")
			.body([risky_func](arc::Builder &fb)
			{
				auto normal_block = fb.block<arc::DataType::VOID>("normal");
				auto except_block = fb.block<arc::DataType::VOID>("exception");

				auto *arg = fb.lit(42);
				auto *invoke_result = fb.invoke(risky_func, { arg }, normal_block.entry(), except_block.entry());

				return fb.ret(invoke_result);
			});

	dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, TypeCasts)
{
	builder->function<arc::DataType::VOID>("casts")
			.body([](arc::Builder &fb)
			{
				auto *int_val = fb.lit(42);
				[[maybe_unused]] auto *float_cast = fb.cast<arc::DataType::FLOAT32>(int_val);
				[[maybe_unused]] auto *int64_cast = fb.cast<arc::DataType::INT64>(int_val);
				[[maybe_unused]] auto *uint32_cast = fb.cast<arc::DataType::UINT32>(int_val);

				return fb.ret();
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, ComplexTypes)
{
	auto *int_alloc = builder->alloc<arc::DataType::INT32>(builder->lit(1));

	builder->function<arc::DataType::VOID>("complex_types")
			.param_ptr<arc::DataType::INT32>("int_ptr", int_alloc)
			.body([](arc::Builder &fb, arc::Node *)
			{
				auto *array_alloc = fb.alloc<arc::DataType::INT32>(fb.lit(10));
				auto *array_addr = fb.addr_of(array_alloc);

				auto *offset = fb.lit(4);
				fb.ptr_add(array_addr, offset);
				return fb.ret();
			});

	dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, NodeTraits)
{
	builder->function<arc::DataType::VOID>("exported_func")
			.exported()
			.body([](arc::Builder &fb)
			{
				return fb.ret();
			});

	builder->function<arc::DataType::INT32>("main")
			.driver()
			.body([](arc::Builder &fb)
			{
				return fb.ret(fb.lit(0));
			});

	builder->function<arc::DataType::VOID>("external_func")
			.imported()
			.body([](arc::Builder &fb)
			{
				return fb.ret();
			});

	builder->function<arc::DataType::VOID>("volatile_func")
			.keep()
			.body([](arc::Builder &fb)
			{
				return fb.ret();
			});

	dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, NestedRegions)
{
	builder->function<arc::DataType::INT32>("nested_regions")
			.param<arc::DataType::BOOL>("condition")
			.body([](arc::Builder &fb, arc::Node *condition)
			{
				auto outer_block = fb.block<arc::DataType::VOID>("outer");
				auto inner_block = fb.block<arc::DataType::VOID>("inner");
				auto end_block = fb.block<arc::DataType::VOID>("end");

				auto *value = fb.lit(42);
				fb.branch(condition, outer_block.entry(), end_block.entry());
				fb.jump(inner_block.entry());
				fb.jump(end_block.entry());
				return fb.ret(value);
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, FluentStoreOperations)
{
	builder->function<arc::DataType::VOID>("fluent_stores")
			.body([](arc::Builder &fb)
			{
				auto *alloc_node = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				auto *value1 = fb.lit(42);
				auto *value2 = fb.lit(84);
				auto *addr = fb.addr_of(alloc_node);

				[[maybe_unused]] auto *store1 = fb.store(value1).to(alloc_node);
				[[maybe_unused]] auto *store2 = fb.store(value2).through(addr);
				[[maybe_unused]] auto *atomic_store1 = fb.store(value1).to_atomic(alloc_node);
				[[maybe_unused]] auto *atomic_store2 = fb.store(value2).through_atomic(
					addr, arc::AtomicOrdering::RELAXED);
				return fb.ret();
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, EdgeCases)
{
	builder->function<arc::DataType::VOID>("empty_func")
		.body([](arc::Builder &fb)
		{
			return fb.ret();
		});

	builder->function<arc::DataType::INT32>("many_params")
		.param<arc::DataType::INT32>("a")
		.param<arc::DataType::INT32>("b")
		.param<arc::DataType::INT32>("c")
		.param<arc::DataType::INT32>("d")
		.param<arc::DataType::INT32>("e")
		.body([](arc::Builder &fb, arc::Node* a, arc::Node* b, arc::Node* c, arc::Node* d, arc::Node* e)
		{
			auto* result = fb.add(a, fb.add(b, fb.add(c, fb.add(d, e))));
			return fb.ret(result);
		});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, Array)
{
	builder->function<arc::DataType::VOID>("array_test")
		.body([](arc::Builder &fb)
		{
			auto *array_alloc = fb.array_alloc<arc::DataType::INT32, 5>();
			auto *index = fb.lit(2);
			fb.array_index(array_alloc, index);
			return fb.ret();
		});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, StructTypes)
{
	auto person_type = builder->struct_type("Person")
		.field("age", arc::DataType::INT32)
		.field("height", arc::DataType::FLOAT32)
		.field("is_student", arc::DataType::BOOL)
		.build();

	module->add_t("Person", person_type);
	builder->function<arc::DataType::VOID>("struct_test")
		.body([&](arc::Builder &fb)
		{
			auto *person = fb.alloc(person_type);
			auto *age_field = fb.struct_field(person, "age");
			auto *age_value = fb.lit(25);
			fb.store(age_value, age_field);
			return fb.ret();
		});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, RecursiveFunction)
{
	auto opaque_fn = builder->opaque_t<arc::DataType::FUNCTION>("factorial");

	opaque_fn.function<arc::DataType::INT32>()
		.param<arc::DataType::INT32>("n")
		.body([&opaque_fn](arc::Builder &fb, arc::Node *n)
		{
			auto* zero = fb.lit(0);
			auto* one = fb.lit(1);
			auto* is_zero = fb.eq(n, zero);

			const auto true_block = fb.block<arc::DataType::INT32>("base_case");
			const auto false_block = fb.block<arc::DataType::INT32>("recursive_case");

			fb.branch(is_zero, true_block.entry(), false_block.entry());

			auto* n_minus_1 = fb.sub(n, one);
			auto* recursive_call = fb.call(opaque_fn.node(), {n_minus_1});
			auto* result = fb.mul(n, recursive_call);

			return fb.ret(result);
		});
	arc::dump(*module);
}

TEST_F(DumpFixture, BasicStructDump)
{
    auto point_type = builder->struct_type("Point")
        .field("x", arc::DataType::FLOAT32)
        .field("y", arc::DataType::FLOAT32)
        .build();

    module->add_t("Point", point_type);

    builder->function<arc::DataType::VOID>("struct_basic")
        .body([&](arc::Builder &fb)
        {
            auto *point = fb.alloc(point_type);
            auto *x_field = fb.struct_field(point, "x");
            auto *y_field = fb.struct_field(point, "y");

            auto *x_val = fb.lit(3.14f);
            auto *y_val = fb.lit(2.71f);

            fb.store(x_val, x_field);
            fb.store(y_val, y_field);

            return fb.ret();
        });

    arc::dump(*module);
    std::cout << "\n";
}

TEST_F(DumpFixture, SelfReferentialStructDump)
{
    auto node_type = builder->struct_type("ListNode")
        .field("data", arc::DataType::INT32)
        .self_ptr("next")
        .build();

    module->add_t("ListNode", node_type);

    builder->function<arc::DataType::VOID>("linked_list")
        .body([&](arc::Builder &fb)
        {
            auto *node1 = fb.alloc(node_type);
            auto *node2 = fb.alloc(node_type);

            auto *data1 = fb.struct_field(node1, "data");
            auto *data2 = fb.struct_field(node2, "data");
            fb.store(fb.lit(42), data1);
            fb.store(fb.lit(84), data2);

            auto *next1 = fb.struct_field(node1, "next");
            auto *node2_addr = fb.addr_of(node2);
            fb.ptr_store(node2_addr, next1);

            return fb.ret();
        });

    arc::dump(*module);
    std::cout << "\n";
}

TEST_F(DumpFixture, ComplexRecursiveStructDump)
{
    auto tree_type = builder->struct_type("TreeNode")
        .field("value", arc::DataType::INT32)
        .self_ptr("left")
        .self_ptr("right")
        .self_ptr("parent")
        .field("depth", arc::DataType::UINT32)
        .build();

    module->add_t("TreeNode", tree_type);

    builder->function<arc::DataType::VOID>("binary_tree")
        .body([&](arc::Builder &fb)
        {
            auto *root = fb.alloc(tree_type);
            auto *left_child = fb.alloc(tree_type);
            auto *right_child = fb.alloc(tree_type);

            auto *root_value = fb.struct_field(root, "value");
            auto *left_value = fb.struct_field(left_child, "value");
            auto *right_value = fb.struct_field(right_child, "value");

            fb.store(fb.lit(10), root_value);
            fb.store(fb.lit(5), left_value);
            fb.store(fb.lit(15), right_value);

            auto *root_left = fb.struct_field(root, "left");
            auto *root_right = fb.struct_field(root, "right");
            auto *left_parent = fb.struct_field(left_child, "parent");
            auto *right_parent = fb.struct_field(right_child, "parent");

            fb.ptr_store(fb.addr_of(left_child), root_left);
            fb.ptr_store(fb.addr_of(right_child), root_right);
            fb.ptr_store(fb.addr_of(root), left_parent);
            fb.ptr_store(fb.addr_of(root), right_parent);

            return fb.ret();
        });

    arc::dump(*module);
    std::cout << "\n";
}

TEST_F(DumpFixture, PackedStructDump)
{
    auto packed_type = builder->struct_type("PackedData")
        .field("flag", arc::DataType::BOOL)
        .field("large_val", arc::DataType::INT64)
        .field("small_val", arc::DataType::INT8)
        .packed()
        .build();

    module->add_t("PackedData", packed_type);

    builder->function<arc::DataType::VOID>("packed_struct")
        .body([&](arc::Builder &fb)
        {
            auto *packed = fb.alloc(packed_type);
            auto *flag_field = fb.struct_field(packed, "flag");
            auto *large_field = fb.struct_field(packed, "large_val");
            auto *small_field = fb.struct_field(packed, "small_val");

            fb.store(fb.lit(true), flag_field);
            fb.store(fb.lit(static_cast<std::int64_t>(9223372036854775807)), large_field);
            fb.store(fb.lit(static_cast<std::int8_t>(127)), small_field);

            return fb.ret();
        });

    arc::dump(*module);
    std::cout << "\n";
}

TEST_F(DumpFixture, NestedStructDump)
{
    /* define inner struct */
    auto address_type = builder->struct_type("Address")
        .field("street_num", arc::DataType::UINT32)
        .field("zip_code", arc::DataType::UINT32)
        .build();

    /* define outer struct with nested struct */
    arc::TypedData address_field_type;
    address_field_type.set<arc::DataTraits<arc::DataType::STRUCT>::value, arc::DataType::STRUCT>(
        address_type.get<arc::DataType::STRUCT>());

    auto person_type = builder->struct_type("Person")
        .field("age", arc::DataType::INT32)
        .field("address", arc::DataType::STRUCT, address_field_type)
        .self_ptr("spouse")
        .build();

    module->add_t("Address", address_type);
    module->add_t("Person", person_type);

    builder->function<arc::DataType::VOID>("nested_struct")
        .body([&](arc::Builder &fb)
        {
            auto *person = fb.alloc(person_type);
            auto *age_field = fb.struct_field(person, "age");
            auto *addr_field = fb.struct_field(person, "address");
            auto *street_field = fb.struct_field(addr_field, "street_num");
            auto *zip_field = fb.struct_field(addr_field, "zip_code");

            fb.store(fb.lit(30), age_field);
            fb.store(fb.lit(static_cast<std::uint32_t>(123)), street_field);
            fb.store(fb.lit(static_cast<std::uint32_t>(12345)), zip_field);

            return fb.ret();
        });

    arc::dump(*module);
    std::cout << "\n";
}

TEST_F(DumpFixture, StructWithPointersDump)
{
	auto *int_alloc = builder->alloc<arc::DataType::INT32>(builder->lit(1));

	auto wrapper_type = builder->struct_type("Wrapper")
		.field("id", arc::DataType::UINT64)
		.field_ptr("data_ptr", int_alloc)
		.field_ptr("nullable_ptr", nullptr)
		.self_ptr("next", 1)
		.build();

	module->add_t("Wrapper", wrapper_type);

	builder->function<arc::DataType::VOID>("pointer_struct")
		.body([&](arc::Builder &fb)
		{
			auto *wrapper = fb.alloc(wrapper_type);
			auto *id_field = fb.struct_field(wrapper, "id");
			auto *data_ptr_field = fb.struct_field(wrapper, "data_ptr");
			auto *next_field = fb.struct_field(wrapper, "next");

			fb.store(fb.lit(static_cast<std::uint64_t>(12345)), id_field);

			auto *loaded_ptr = fb.load(data_ptr_field);
			fb.ptr_load(loaded_ptr);

			auto *next_ptr = fb.load(next_field);
			auto *wrapper_addr = fb.addr_of(wrapper);
			fb.ptr_store(wrapper_addr, next_ptr);
			return fb.ret();
		});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, MultipleStructTypesDump)
{
    auto node_type = builder->struct_type("Node")
        .field("id", arc::DataType::UINT32)
        .self_ptr("next")
        .build();

    auto list_type = builder->struct_type("List")
        .field("count", arc::DataType::UINT32)
        .field_ptr("head", nullptr) /* forward ref to Node */
        .field_ptr("tail", nullptr) /* forward ref to Node */
        .build();

    auto manager_type = builder->struct_type("Manager")
        .field("active_lists", arc::DataType::UINT32)
        .field_ptr("primary_list", nullptr) /* forward ref to List */
        .build();

    module->add_t("Node", node_type);
    module->add_t("List", list_type);
    module->add_t("Manager", manager_type);

    builder->function<arc::DataType::VOID>("multiple_structs")
        .body([&](arc::Builder &fb)
        {
            auto *manager = fb.alloc(manager_type);
            auto *list = fb.alloc(list_type);
            auto *node = fb.alloc(node_type);

            auto *mgr_count = fb.struct_field(manager, "active_lists");
            auto *mgr_primary = fb.struct_field(manager, "primary_list");
            auto *list_count = fb.struct_field(list, "count");
            auto *list_head = fb.struct_field(list, "head");
            auto *node_id = fb.struct_field(node, "id");

            fb.store(fb.lit(static_cast<std::uint32_t>(1)), mgr_count);
            fb.ptr_store(fb.addr_of(list), mgr_primary);
            fb.store(fb.lit(static_cast<std::uint32_t>(1)), list_count);
            fb.ptr_store(fb.addr_of(node), list_head);
            fb.store(fb.lit(static_cast<std::uint32_t>(100)), node_id);

            return fb.ret();
        });

    arc::dump(*module);
    std::cout << "\n";
}
