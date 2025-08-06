/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <queue>
#include <arc/analysis/tbaa.hpp>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/support/dump.hpp>
#include <arc/transform/sroa.hpp>
#include <gtest/gtest.h>

class SROAFixture : public testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("sroa_test");
		builder = std::make_unique<arc::Builder>(*module);
		pass_manager = std::make_unique<arc::PassManager>();
		pass_manager->add<arc::TypeBasedAliasAnalysisPass>();
		pass_manager->add<arc::SROAPass>();
	}

	void TearDown() override
	{
		arc::dump(*module);
		pass_manager.reset();
		builder.reset();
		module.reset();
	}

	arc::Node *find_return(arc::Region *region)
	{
		for (arc::Node *node: region->nodes())
		{
			if (node->ir_type == arc::NodeType::RET)
				return node;
		}
		return nullptr;
	}

	std::size_t count_nodes(arc::Region *region, arc::NodeType type)
	{
		std::size_t count = 0;
		for (arc::Node *node: region->nodes())
		{
			if (node->ir_type == type)
				count++;
		}
		return count;
	}

	arc::Node *find_node(arc::Region *region, arc::NodeType type)
	{
		for (arc::Node *node: region->nodes())
		{
			if (node->ir_type == type)
				return node;
		}
		return nullptr;
	}

	arc::Region *get_function_region(const std::string &name)
	{
		for (arc::Region *child: module->root()->children())
		{
			if (child->name() == name)
				return child;
		}
		return nullptr;
	}

	std::size_t count_all_nodes(arc::Region *region, arc::NodeType type)
	{
		std::size_t count = 0;
		std::queue<arc::Region *> worklist;
		worklist.push(region);

		while (!worklist.empty())
		{
			arc::Region *current = worklist.front();
			worklist.pop();

			count += count_nodes(current, type);

			for (arc::Region *child: current->children())
				worklist.push(child);
		}

		return count;
	}

	std::unique_ptr<arc::Module> module;
	std::unique_ptr<arc::Builder> builder;
	std::unique_ptr<arc::PassManager> pass_manager;
};

TEST_F(SROAFixture, SimpleStructPromotion)
{
	auto simple_struct = builder->struct_type("SimpleStruct")
			.field("x", arc::DataType::INT32)
			.field("y", arc::DataType::FLOAT32)
			.build();

	builder->function<arc::DataType::FLOAT32>("test_simple")
			.body([&](arc::Builder &fb)
			{
				auto *struct_alloc = fb.alloc(simple_struct);
				auto *x_field = fb.struct_field(struct_alloc, "x");
				auto *y_field = fb.struct_field(struct_alloc, "y");

				fb.store(fb.lit(42), x_field);
				fb.store(fb.lit(3.14f), y_field);

				auto *x_val = fb.load(x_field);
				auto *y_val = fb.load(y_field);

				auto *x_float = fb.cast<arc::DataType::FLOAT32>(x_val);
				auto *sum = fb.add(x_float, y_val);

				return fb.ret(sum);
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_simple");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::ACCESS), 0);
	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::ALLOC), 2);

	auto *ret = find_return(func_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::ADD);
}

TEST_F(SROAFixture, VolatileStructPreserved)
{
	auto test_struct = builder->struct_type("TestStruct")
			.field("a", arc::DataType::INT32)
			.field("b", arc::DataType::INT32)
			.build();

	arc::Node *volatile_alloc = nullptr;

	builder->function<arc::DataType::INT32>("test_volatile")
			.body([&](arc::Builder &fb)
			{
				volatile_alloc = fb.alloc(test_struct);
				volatile_alloc->traits |= arc::NodeTraits::VOLATILE;

				auto *a_field = fb.struct_field(volatile_alloc, "a");
				fb.store(fb.lit(100), a_field);
				auto *a_val = fb.load(a_field);

				return fb.ret(a_val);
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_volatile");
	ASSERT_NE(func_region, nullptr);

	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::ACCESS), 1);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::ALLOC), 1);
}

TEST_F(SROAFixture, AddressTakenPreserved)
{
	auto addr_struct = builder->struct_type("AddrStruct")
			.field("value", arc::DataType::INT32)
			.build();

	builder->function<arc::DataType::INT32>("test_addr_taken")
			.body([&](arc::Builder &fb)
			{
				auto *struct_alloc = fb.alloc(addr_struct);
				auto *struct_addr = fb.addr_of(struct_alloc);

				auto *value_field = fb.struct_field(struct_alloc, "value");
				fb.store(fb.lit(42), value_field);
				auto *value = fb.load(value_field);

				return fb.ret(value);
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_addr_taken");
	ASSERT_NE(func_region, nullptr);

	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::ACCESS), 1);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::ALLOC), 1);
	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::ADDR_OF), 1);
}

TEST_F(SROAFixture, PartialPromotion)
{
	auto mixed_struct = builder->struct_type("MixedStruct")
			.field("normal_field", arc::DataType::INT32)
			.field("escaped_field", arc::DataType::FLOAT32)
			.field("another_normal", arc::DataType::BOOL)
			.build();

	builder->function<arc::DataType::INT32>("test_partial")
			.body([&](arc::Builder &fb)
			{
				auto *struct_alloc = fb.alloc(mixed_struct);

				auto *normal_field = fb.struct_field(struct_alloc, "normal_field");
				auto *escaped_field = fb.struct_field(struct_alloc, "escaped_field");
				auto *another_normal = fb.struct_field(struct_alloc, "another_normal");

				fb.store(fb.lit(42), normal_field);
				fb.store(fb.lit(3.14f), escaped_field);
				fb.store(fb.lit(true), another_normal);

				auto *escaped_addr = fb.addr_of(escaped_field);
				auto *normal_val = fb.load(normal_field);
				auto *another_val = fb.load(another_normal);

				auto *normal_cast = fb.cast<arc::DataType::INT32>(another_val);
				auto *result = fb.add(normal_val, normal_cast);

				return fb.ret(result);
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_partial");
	ASSERT_NE(func_region, nullptr);

	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::ALLOC), 2);
	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::ACCESS), 1);
}

TEST_F(SROAFixture, NestedStructAccess)
{
	auto point_struct = builder->struct_type("Point")
			.field("x", arc::DataType::FLOAT32)
			.field("y", arc::DataType::FLOAT32)
			.build();

	builder->function<arc::DataType::FLOAT32>("test_nested")
			.body([&](arc::Builder &fb)
			{
				auto *point_alloc = fb.alloc(point_struct);

				arc::Node *inner_block = fb.block<arc::DataType::FLOAT32>("inner_block")([&](arc::Builder &bb)
				{
					auto *x_field = bb.struct_field(point_alloc, "x");
					auto *y_field = bb.struct_field(point_alloc, "y");

					auto *x_val = bb.load(x_field);
					auto *y_val = bb.load(y_field);
					auto *sum = bb.add(x_val, y_val);

					return bb.ret(sum);
				});

				auto *x_field = fb.struct_field(point_alloc, "x");
				auto *y_field = fb.struct_field(point_alloc, "y");

				fb.store(fb.lit(1.0f), x_field);
				fb.store(fb.lit(2.0f), y_field);

				fb.jump(inner_block->parent->entry());
				return fb.ret();
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_nested");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::ACCESS), 0);
	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::ALLOC), 2);
}

TEST_F(SROAFixture, MultipleStructTypes)
{
	auto struct_a = builder->struct_type("StructA")
			.field("field1", arc::DataType::INT32)
			.field("field2", arc::DataType::INT32)
			.build();

	auto struct_b = builder->struct_type("StructB")
			.field("value", arc::DataType::FLOAT32)
			.build();

	builder->function<arc::DataType::FLOAT32>("test_multiple_types")
			.body([&](arc::Builder &fb)
			{
				auto *alloc_a = fb.alloc(struct_a);
				auto *alloc_b = fb.alloc(struct_b);

				auto *a_field1 = fb.struct_field(alloc_a, "field1");
				auto *a_field2 = fb.struct_field(alloc_a, "field2");
				auto *b_value = fb.struct_field(alloc_b, "value");

				fb.store(fb.lit(10), a_field1);
				fb.store(fb.lit(20), a_field2);
				fb.store(fb.lit(5.5f), b_value);

				auto *val1 = fb.load(a_field1);
				auto *val2 = fb.load(a_field2);
				auto *val_b = fb.load(b_value);

				auto *sum_a = fb.add(val1, val2);
				auto *sum_a_float = fb.cast<arc::DataType::FLOAT32>(sum_a);
				auto *final_sum = fb.add(sum_a_float, val_b);

				return fb.ret(final_sum);
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_multiple_types");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::ACCESS), 0);
	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::ALLOC), 3);
}

TEST_F(SROAFixture, EmptyStruct)
{
	auto empty_struct = builder->struct_type("EmptyStruct")
			.build();

	builder->function<arc::DataType::INT32>("test_empty")
			.body([&](arc::Builder &fb)
			{
				auto *empty_alloc = fb.alloc(empty_struct);
				return fb.ret(fb.lit(42));
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_empty");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::ACCESS), 0);
}

TEST_F(SROAFixture, SingleFieldStruct)
{
	auto single_struct = builder->struct_type("SingleStruct")
			.field("only_field", arc::DataType::INT64)
			.build();

	builder->function<arc::DataType::INT64>("test_single")
			.body([&](arc::Builder &fb)
			{
				auto *single_alloc = fb.alloc(single_struct);
				auto *field = fb.struct_field(single_alloc, "only_field");

				fb.store(fb.lit(static_cast<std::int64_t>(12345)), field);
				auto *value = fb.load(field);
				return fb.ret(value);
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_single");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::ACCESS), 0);
	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::ALLOC), 1);

	auto *ret = find_return(func_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	std::println("{:d}", static_cast<std::uint16_t>(ret_value->ir_type));
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::LOAD);
	EXPECT_EQ(ret_value->type_kind, arc::DataType::INT64);
}
