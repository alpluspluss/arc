/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/support/dump.hpp>
#include <arc/codegen/lowering.hpp>
#include <arc/support/algorithm.hpp>
#include <gtest/gtest.h>

class IRLoweringFixture : public testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("lowering_test");
		builder = std::make_unique<arc::Builder>(*module);
		pass_manager = std::make_unique<arc::PassManager>();
		pass_manager->add<arc::IRLoweringPass>();
	}

	void TearDown() override
	{
		arc::dump(*module);
		pass_manager.reset();
		builder.reset();
		module.reset();
	}

	arc::Node* find_node(arc::Region* region, arc::NodeType type)
	{
		for (arc::Node* node : region->nodes())
		{
			if (node->ir_type == type)
				return node;
		}
		return nullptr;
	}

	std::size_t count_nodes(arc::Region* region, arc::NodeType type)
	{
		std::size_t count = 0;
		for (arc::Node* node : region->nodes())
		{
			if (node->ir_type == type)
				count++;
		}
		return count;
	}

	arc::Region* get_function_region(const std::string& name)
	{
		for (arc::Region* child : module->root()->children())
		{
			if (child->name() == name)
				return child;
		}
		return nullptr;
	}

	arc::TypedData create_struct_type()
	{
		auto struct_builder = builder->struct_type("TestStruct");
		struct_builder.field("field_a", arc::DataType::INT32);
		struct_builder.field("field_b", arc::DataType::FLOAT32);
		struct_builder.field("field_c", arc::DataType::UINT64);
		return struct_builder.build();
	}

	std::unique_ptr<arc::Module> module;
	std::unique_ptr<arc::Builder> builder;
	std::unique_ptr<arc::PassManager> pass_manager;
};

TEST_F(IRLoweringFixture, StructFieldAccessLowering)
{
	auto struct_type = create_struct_type();

	builder->function<arc::DataType::INT32>("test_struct_access")
		.body([&](arc::Builder& fb)
		{
			auto* struct_alloc = fb.alloc(struct_type);
			auto* field_access = fb.struct_field(struct_alloc, "field_a");
			auto* loaded_value = fb.load(field_access);
			return fb.ret(loaded_value);
		});

	pass_manager->run(*module);

	auto* func_region = get_function_region("test_struct_access");
	ASSERT_NE(func_region, nullptr);

	/* ACCESS nodes should be lowered to PTR_ADD */
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::ACCESS), 0);
	EXPECT_GE(count_nodes(func_region, arc::NodeType::PTR_ADD), 1);

	/* verify PTR_ADD has correct structure */
	auto* ptr_add = find_node(func_region, arc::NodeType::PTR_ADD);
	ASSERT_NE(ptr_add, nullptr);
	ASSERT_EQ(ptr_add->inputs.size(), 2);

	/* second operand should be offset literal */
	auto* offset_node = ptr_add->inputs[1];
	EXPECT_EQ(offset_node->ir_type, arc::NodeType::LIT);
	EXPECT_EQ(arc::extract_literal_value(offset_node), 0); /* first field at offset 0 */
}

TEST_F(IRLoweringFixture, StructFieldAccessWithOffset)
{
	auto struct_type = create_struct_type();

	builder->function<arc::DataType::FLOAT32>("test_struct_field_b")
		.body([&](arc::Builder& fb)
		{
			auto* struct_alloc = fb.alloc(struct_type);
			auto* field_access = fb.struct_field(struct_alloc, "field_b");
			auto* loaded_value = fb.load(field_access);
			return fb.ret(loaded_value);
		});

	pass_manager->run(*module);

	auto* func_region = get_function_region("test_struct_field_b");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_nodes(func_region, arc::NodeType::ACCESS), 0);

	auto* ptr_add = find_node(func_region, arc::NodeType::PTR_ADD);
	ASSERT_NE(ptr_add, nullptr);
	ASSERT_EQ(ptr_add->inputs.size(), 2);

	auto* offset_node = ptr_add->inputs[1];
	EXPECT_EQ(offset_node->ir_type, arc::NodeType::LIT);
	/* field_b should be at offset 4 (after 4-byte INT32) */
	EXPECT_EQ(arc::extract_literal_value(offset_node), 4);
}

TEST_F(IRLoweringFixture, ArrayElementAccessConstantIndex)
{
	builder->function<arc::DataType::INT32>("test_array_access")
		.body([&](arc::Builder& fb)
		{
			auto* array_alloc = fb.array_alloc<arc::DataType::INT32, 10>();
			auto* index = fb.lit(static_cast<std::int32_t>(3));
			auto* element_access = fb.array_index(array_alloc, index);
			auto* loaded_value = fb.load(element_access);
			return fb.ret(loaded_value);
		});

	pass_manager->run(*module);

	auto* func_region = get_function_region("test_array_access");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_nodes(func_region, arc::NodeType::ACCESS), 0);

	auto* ptr_add = find_node(func_region, arc::NodeType::PTR_ADD);
	ASSERT_NE(ptr_add, nullptr);
	ASSERT_EQ(ptr_add->inputs.size(), 2);

	auto* offset_node = ptr_add->inputs[1];
	EXPECT_EQ(offset_node->ir_type, arc::NodeType::LIT);
	/* element 3 of INT32 array: 3 * 4 = 12 bytes */
	EXPECT_EQ(arc::extract_literal_value(offset_node), 12);
	EXPECT_EQ(offset_node->type_kind, arc::DataType::INT32); /* preserve index type */
}

TEST_F(IRLoweringFixture, ArrayElementAccessDynamicIndex)
{
	builder->function<arc::DataType::VOID>("test_dynamic_array_access")
		.param<arc::DataType::INT32>("index")
		.body([&](arc::Builder& fb, arc::Node* index_param)
		{
			auto* array_alloc = fb.array_alloc<arc::DataType::UINT64, 5>();
			auto* element_access = fb.array_index(array_alloc, index_param);
			auto* loaded_value = fb.load(element_access);
			return fb.ret();
		});

	pass_manager->run(*module);

	auto* func_region = get_function_region("test_dynamic_array_access");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_nodes(func_region, arc::NodeType::ACCESS), 0);
	EXPECT_GE(count_nodes(func_region, arc::NodeType::PTR_ADD), 1);
}

TEST_F(IRLoweringFixture, NestedStructAccess)
{
    auto inner_struct = builder->struct_type("InnerStruct");
    inner_struct.field("inner_field", arc::DataType::INT16);
    auto inner_type = inner_struct.build();

    auto outer_struct = builder->struct_type("OuterStruct");
    outer_struct.field("padding", arc::DataType::UINT32);
    outer_struct.field("inner", arc::DataType::STRUCT, inner_type);
    auto outer_type = outer_struct.build();

    builder->function<arc::DataType::INT16>("test_nested_access")
       .body([&](arc::Builder& fb)
       {
          auto* outer_alloc = fb.alloc(outer_type);
          auto* inner_access = fb.struct_field(outer_alloc, "inner");
          auto* field_access = fb.struct_field(inner_access, "inner_field");
          auto* loaded_value = fb.load(field_access);
          return fb.ret(loaded_value);
       });

    pass_manager->run(*module);

    auto* func_region = get_function_region("test_nested_access");
    ASSERT_NE(func_region, nullptr);

    EXPECT_EQ(count_nodes(func_region, arc::NodeType::ACCESS), 0);
    EXPECT_GE(count_nodes(func_region, arc::NodeType::PTR_ADD), 2);
}

TEST_F(IRLoweringFixture, MixedStructAndArrayAccess)
{
    auto struct_type = create_struct_type();

    builder->function<arc::DataType::VOID>("test_mixed_access")
       .param<arc::DataType::INT32>("array_index")
       .body([&](arc::Builder& fb, arc::Node* array_index)
       {
          auto* struct_array = fb.array_alloc(struct_type, 3);
          auto* struct_access = fb.array_index(struct_array, array_index);
          auto* field_access = fb.struct_field(struct_access, "field_c");
          [[maybe_unused]] auto* loaded_value = fb.load(field_access);
          return fb.ret();
       });

    pass_manager->run(*module);

    auto* func_region = get_function_region("test_mixed_access");
    ASSERT_NE(func_region, nullptr);

    EXPECT_EQ(count_nodes(func_region, arc::NodeType::ACCESS), 0);
    EXPECT_GE(count_nodes(func_region, arc::NodeType::PTR_ADD), 2);
}

TEST_F(IRLoweringFixture, AddressOfPreservation)
{
    auto struct_type = create_struct_type();

    builder->function<arc::DataType::VOID>("test_addr_of")
       .body([&](arc::Builder& fb)
       {
          auto* struct_alloc = fb.alloc(struct_type);
          auto* struct_addr = fb.addr_of(struct_alloc);
          auto* field_access = fb.struct_field(struct_addr, "field_a");
          [[maybe_unused]] auto* loaded_value = fb.ptr_load(field_access);
          return fb.ret();
       });

    pass_manager->run(*module);

    auto* func_region = get_function_region("test_addr_of");
    ASSERT_NE(func_region, nullptr);

    EXPECT_EQ(count_nodes(func_region, arc::NodeType::ACCESS), 0);
    EXPECT_EQ(count_nodes(func_region, arc::NodeType::ADDR_OF), 1);
    EXPECT_GE(count_nodes(func_region, arc::NodeType::PTR_ADD), 1);
}

TEST_F(IRLoweringFixture, TypePreservationInLowering)
{
	builder->function<arc::DataType::VOID>("test_type_preservation")
		.body([&](arc::Builder& fb)
		{
			auto* array_u8 = fb.array_alloc<arc::DataType::UINT8, 100>();
			auto* index_u8 = fb.lit(static_cast<std::uint8_t>(10));
			auto* access_u8 = fb.array_index(array_u8, index_u8);

			auto* array_i64 = fb.array_alloc<arc::DataType::INT64, 50>();
			auto* index_i64 = fb.lit(static_cast<std::int64_t>(5));
			auto* access_i64 = fb.array_index(array_i64, index_i64);

			return fb.ret();
		});

	pass_manager->run(*module);

	auto* func_region = get_function_region("test_type_preservation");
	ASSERT_NE(func_region, nullptr);

	/* check that literal types are preserved */
	std::size_t u8_literals = 0;
	std::size_t i64_literals = 0;

	for (arc::Node* node : func_region->nodes())
	{
		if (node->ir_type == arc::NodeType::LIT)
		{
			if (node->type_kind == arc::DataType::UINT8)
				u8_literals++;
			else if (node->type_kind == arc::DataType::INT64)
				i64_literals++;
		}
	}

	EXPECT_GE(u8_literals, 1);
	EXPECT_GE(i64_literals, 1);
}
