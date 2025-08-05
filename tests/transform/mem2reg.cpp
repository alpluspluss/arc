/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <queue>
#include <arc/analysis/tbaa.hpp>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/support/dump.hpp>
#include <arc/transform/mem2reg.hpp>
#include <gtest/gtest.h>

class Mem2RegFixture : public testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("mem2reg_test");
		builder = std::make_unique<arc::Builder>(*module);
		pass_manager = std::make_unique<arc::PassManager>();
		pass_manager->add<arc::TypeBasedAliasAnalysisPass>();
		pass_manager->add<arc::Mem2RegPass>();
	}

	void TearDown() override
	{
		arc::dump(*module.get());
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

TEST_F(Mem2RegFixture, SimpleAllocation)
{
	builder->function<arc::DataType::INT32>("test_simple")
			.body([&](arc::Builder &fb)
			{
				auto *ptr = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(42), ptr);
				auto *val = fb.load(ptr);
				return fb.ret(val);
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_simple");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::ALLOC), 0);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::STORE), 0);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::LOAD), 0);

	auto *ret = find_return(func_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::LIT);
	EXPECT_EQ(ret_value->type_kind, arc::DataType::INT32);
	EXPECT_EQ(ret_value->value.get<arc::DataType::INT32>(), 42);
}

TEST_F(Mem2RegFixture, MultipleStoresLoads)
{
	builder->function<arc::DataType::INT32>("test_multiple")
			.body([&](arc::Builder &fb)
			{
				auto *ptr = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(10), ptr);
				auto *val1 = fb.load(ptr);
				fb.store(fb.lit(20), ptr);
				auto *val2 = fb.load(ptr);
				auto *sum = fb.add(val1, val2);
				return fb.ret(sum);
			});
	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_multiple");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::ALLOC), 0);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::STORE), 0);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::LOAD), 0);

	auto *ret = find_return(func_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::ADD);
}

TEST_F(Mem2RegFixture, ConditionalStores)
{
	arc::Node *true_block = nullptr;
	arc::Node *false_block = nullptr;
	arc::Node *merge_block = nullptr;

	builder->function<arc::DataType::INT32>("test_conditional")
			.body([&](arc::Builder &fb)
			{
				auto *ptr = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(0), ptr);

				merge_block = fb.block<arc::DataType::INT32>("merge_block")([&](arc::Builder &bb)
				{
					auto *val = bb.load(ptr);
					return bb.ret(val);
				});

				true_block = fb.block<arc::DataType::VOID>("true_block")([&](arc::Builder &bb)
				{
					bb.store(bb.lit(100), ptr);
					bb.jump(merge_block->parent->entry());
					return bb.ret();
				});

				false_block = fb.block<arc::DataType::VOID>("false_block")([&](arc::Builder &bb)
				{
					bb.store(bb.lit(200), ptr);
					bb.jump(merge_block->parent->entry());
					return bb.ret();
				});

				auto *condition = fb.gt(fb.lit(5), fb.lit(3));
				fb.branch(condition, true_block->parent->entry(), false_block->parent->entry());
				return fb.ret();
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_conditional");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::ALLOC), 0);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::STORE), 0);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::LOAD), 0);

	arc::Region *merge_region = nullptr;
	for (arc::Region *child: func_region->children())
	{
		if (child->name() == "merge_block")
		{
			merge_region = child;
			break;
		}
	}

	ASSERT_NE(merge_region, nullptr);
	EXPECT_GE(count_nodes(merge_region, arc::NodeType::FROM), 1);

	auto *ret = find_return(merge_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());
}

TEST_F(Mem2RegFixture, VolatileAllocationPreserved)
{
	arc::Node *volatile_alloc = nullptr;

	builder->function<arc::DataType::INT32>("test_volatile")
			.body([&](arc::Builder &fb)
			{
				auto *normal_ptr = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(42), normal_ptr);
				auto *normal_val = fb.load(normal_ptr);

				volatile_alloc = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				volatile_alloc->traits |= arc::NodeTraits::VOLATILE;
				fb.store(fb.lit(100), volatile_alloc);
				auto *volatile_val = fb.load(volatile_alloc);

				auto *sum = fb.add(normal_val, volatile_val);
				return fb.ret(sum);
			});
	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_volatile");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::ALLOC), 1);
	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::STORE), 1);
	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::LOAD), 1);

	auto *ret = find_return(func_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::ADD);
}

TEST_F(Mem2RegFixture, MultipleAllocations)
{
	builder->function<arc::DataType::INT32>("test_multiple_allocs")
			.body([&](arc::Builder &fb)
			{
				auto *ptr1 = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				auto *ptr2 = fb.alloc<arc::DataType::INT32>(fb.lit(1));

				fb.store(fb.lit(10), ptr1);
				fb.store(fb.lit(20), ptr2);

				auto *val1 = fb.load(ptr1);
				auto *val2 = fb.load(ptr2);

				auto *sum = fb.add(val1, val2);
				return fb.ret(sum);
			});
	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_multiple_allocs");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::ALLOC), 0);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::STORE), 0);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::LOAD), 0);

	auto *ret = find_return(func_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::ADD);
}

TEST_F(Mem2RegFixture, PointerOperationsPreserved)
{
	builder->function<arc::DataType::INT32>("test_ptr_ops")
			.body([&](arc::Builder &fb)
			{
				auto *ptr = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				auto *addr = fb.addr_of(ptr);
				fb.ptr_store(fb.lit(42), addr);
				auto *val = fb.ptr_load(addr);
				return fb.ret(val);
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_ptr_ops");
	ASSERT_NE(func_region, nullptr);

	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::ALLOC), 1);
	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::PTR_STORE), 1);
	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::PTR_LOAD), 1);
	EXPECT_GE(count_all_nodes(func_region, arc::NodeType::ADDR_OF), 1);
}

TEST_F(Mem2RegFixture, NestedRegions)
{
	arc::Node *inner_block = nullptr;

	builder->function<arc::DataType::INT32>("test_nested")
			.body([&](arc::Builder &fb)
			{
				auto *ptr = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(10), ptr);

				inner_block = fb.block<arc::DataType::INT32>("inner_block")([&](arc::Builder &bb)
				{
					bb.store(bb.lit(20), ptr);
					auto *val = bb.load(ptr);
					return bb.ret(val);
				});

				fb.jump(inner_block->parent->entry());
				return fb.ret();
			});
	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_nested");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::ALLOC), 0);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::STORE), 0);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::LOAD), 0);

	arc::Region *inner_region = nullptr;
	for (arc::Region *child: func_region->children())
	{
		if (child->name() == "inner_block")
		{
			inner_region = child;
			break;
		}
	}

	ASSERT_NE(inner_region, nullptr);

	auto *ret = find_return(inner_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::LIT);
	EXPECT_EQ(ret_value->type_kind, arc::DataType::INT32);
	EXPECT_EQ(ret_value->value.get<arc::DataType::INT32>(), 20);
}

TEST_F(Mem2RegFixture, DifferentDataTypes)
{
	builder->function<arc::DataType::FLOAT32>("test_types")
			.body([&](arc::Builder &fb)
			{
				auto *int_ptr = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				auto *float_ptr = fb.alloc<arc::DataType::FLOAT32>(fb.lit(1));
				auto *bool_ptr = fb.alloc<arc::DataType::BOOL>(fb.lit(1));

				fb.store(fb.lit(42), int_ptr);
				fb.store(fb.lit(3.14f), float_ptr);
				fb.store(fb.lit(true), bool_ptr);

				auto *int_val = fb.load(int_ptr);
				auto *float_val = fb.load(float_ptr);
				auto *bool_val = fb.load(bool_ptr);

				[[maybe_unused]] auto *converted = fb.cast<arc::DataType::FLOAT32>(int_val);
				[[maybe_unused]] auto *condition = fb.eq(bool_val, fb.lit(true));

				return fb.ret(float_val);
			});
	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_types");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::ALLOC), 0);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::STORE), 0);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::LOAD), 0);

	auto *ret = find_return(func_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::LIT);
	EXPECT_EQ(ret_value->type_kind, arc::DataType::FLOAT32);
	EXPECT_NEAR(ret_value->value.get<arc::DataType::FLOAT32>(), 3.14f, 0.001f);
}
