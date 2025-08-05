/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <queue>
#include <arc/analysis/tbaa.hpp>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/support/dump.hpp>
#include <arc/transform/dse.hpp>
#include <gtest/gtest.h>

class DSEFixture : public testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("dse_test");
		builder = std::make_unique<arc::Builder>(*module);
		pass_manager = std::make_unique<arc::PassManager>();
		pass_manager->add<arc::TypeBasedAliasAnalysisPass>();
		pass_manager->add<arc::DeadStoreEliminationPass>();
	}

	void TearDown() override
	{
		arc::dump(*module.get());
		pass_manager.reset();
		builder.reset();
		module.reset();
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

TEST_F(DSEFixture, OverwrittenStore)
{
	builder->function<arc::DataType::INT32>("test_overwritten")
			.body([&](arc::Builder &fb)
			{
				auto *ptr = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(42), ptr);
				fb.store(fb.lit(99), ptr);
				auto *val = fb.load(ptr);
				return fb.ret(val);
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_overwritten");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::STORE), 1);
}

TEST_F(DSEFixture, MultipleOverwrites)
{
	builder->function<arc::DataType::INT32>("test_multiple")
			.body([&](arc::Builder &fb)
			{
				auto *ptr = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(10), ptr);
				fb.store(fb.lit(20), ptr);
				fb.store(fb.lit(30), ptr);
				auto *val = fb.load(ptr);
				return fb.ret(val);
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_multiple");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::STORE), 1);
}

TEST_F(DSEFixture, VolatileStorePreserved)
{
	arc::Node *volatile_store = nullptr;

	builder->function<arc::DataType::INT32>("test_volatile")
			.body([&](arc::Builder &fb)
			{
				auto *ptr = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				volatile_store = fb.store(fb.lit(42), ptr);
				volatile_store->traits |= arc::NodeTraits::VOLATILE;
				fb.store(fb.lit(99), ptr);
				auto *val = fb.load(ptr);
				return fb.ret(val);
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_volatile");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::STORE), 2);
}

TEST_F(DSEFixture, DifferentAllocationsPreserved)
{
	builder->function<arc::DataType::INT32>("test_different_allocs")
			.body([&](arc::Builder &fb)
			{
				auto *ptr1 = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				auto *ptr2 = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(42), ptr1);
				fb.store(fb.lit(99), ptr2);
				auto *val1 = fb.load(ptr1);
				auto *val2 = fb.load(ptr2);
				auto *sum = fb.add(val1, val2);
				return fb.ret(sum);
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_different_allocs");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::STORE), 2);
}

TEST_F(DSEFixture, ConditionalStoresPreserved)
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

	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::STORE), 3);
}

TEST_F(DSEFixture, CrossRegionSameBlock)
{
	arc::Node *inner_block = nullptr;

	builder->function<arc::DataType::INT32>("test_cross_region_same")
			.body([&](arc::Builder &fb)
			{
				auto *ptr = fb.alloc<arc::DataType::INT32>(fb.lit(1));

				inner_block = fb.block<arc::DataType::INT32>("inner_block")([&](arc::Builder &bb)
				{
					bb.store(bb.lit(42), ptr);
					bb.store(bb.lit(99), ptr);
					auto *val = bb.load(ptr);
					return bb.ret(val);
				});

				fb.jump(inner_block->parent->entry());
				return fb.ret();
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_cross_region_same");
	ASSERT_NE(func_region, nullptr);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::STORE), 1);
}

TEST_F(DSEFixture, CrossRegionNoInterfere)
{
	arc::Node *branch1 = nullptr;
	arc::Node *branch2 = nullptr;

	builder->function<arc::DataType::INT32>("test_cross_region_no_interfere")
			.body([&](arc::Builder &fb)
			{
				auto *ptr1 = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				auto *ptr2 = fb.alloc<arc::DataType::INT32>(fb.lit(1));

				branch1 = fb.block<arc::DataType::VOID>("branch1")([&](arc::Builder &bb)
				{
					bb.store(bb.lit(42), ptr1);
					return bb.ret();
				});

				branch2 = fb.block<arc::DataType::INT32>("branch2")([&](arc::Builder &bb)
				{
					bb.store(bb.lit(99), ptr2);
					auto *val1 = bb.load(ptr1);
					auto *val2 = bb.load(ptr2);
					auto *sum = bb.add(val1, val2);
					return bb.ret(sum);
				});

				fb.jump(branch1->parent->entry());
				branch1->parent->append(fb.jump(branch2->parent->entry()));
				return fb.ret();
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_cross_region_no_interfere");
	ASSERT_NE(func_region, nullptr);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::STORE), 2);
}

TEST_F(DSEFixture, LocalCallNoInterfere)
{
	auto *external = builder->function<arc::DataType::VOID>("ext")
		.body([&](arc::Builder &fb)
		{
			return fb.ret();
		});

	builder->function<arc::DataType::INT32>("test_local_call")
			.body([&](arc::Builder &fb)
			{
				auto *ptr = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(42), ptr);
				fb.call(external);
				fb.store(fb.lit(99), ptr);
				auto *val = fb.load(ptr);
				return fb.ret(val);
			});

	dump(*module);
	pass_manager->run(*module);

	auto *func_region = get_function_region("test_local_call");
	ASSERT_NE(func_region, nullptr);
	EXPECT_EQ(count_all_nodes(func_region, arc::NodeType::STORE), 1);
}
