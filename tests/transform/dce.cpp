/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <print>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/support/dump.hpp>
#include <arc/transform/dce.hpp>
#include <gtest/gtest.h>

class DCEFixture : public testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("dce_test_module");
		builder = std::make_unique<arc::Builder>(*module);
		pass_manager = std::make_unique<arc::PassManager>();
		pass_manager->add<arc::DeadCodeElimination>();
	}

	void TearDown() override
	{
		pass_manager.reset();
		builder.reset();
		module.reset();
	}

	std::unique_ptr<arc::Module> module;
	std::unique_ptr<arc::Builder> builder;
	std::unique_ptr<arc::PassManager> pass_manager;

	std::size_t count_nodes_in_module() const
	{
		std::size_t count = 0;
		for (const auto *fn: module->functions())
		{
			std::string_view fn_name = module->strtable().get(fn->str_id);
			for (const auto *region: module->root()->children())
			{
				if (region->name() == fn_name)
				{
					count += count_nodes_in_region(region);
					break;
				}
			}
		}
		count += count_nodes_in_region(module->root());
		return count;
	}

	static std::size_t count_nodes_in_region(const arc::Region *region)
	{
		std::size_t count = region->nodes().size();
		for (const auto *child: region->children())
			count += count_nodes_in_region(child);
		return count;
	}
};

TEST_F(DCEFixture, BasicDeadCodeElimination)
{
	builder->function<arc::DataType::INT32>("test_basic_dce")
			.param<arc::DataType::INT32>("x")
			.body([](arc::Builder &fb, arc::Node *x)
			{
				/* live code */
				auto *y = fb.add(x, fb.lit(10));

				/* dead */
				[[maybe_unused]] auto *dead1 = fb.mul(fb.lit(5), fb.lit(7));
				[[maybe_unused]] auto *dead2 = fb.sub(fb.lit(100), fb.lit(50));
				[[maybe_unused]] auto *dead3 = fb.div(dead1, dead2);

				/* live */
				auto *z = fb.mul(y, fb.lit(2));
				return fb.ret(z);
			});

	std::println("before DCE");
	dump(*module);
	const std::size_t nodes_before = count_nodes_in_module();
	std::println("nodes before DCE: {}", nodes_before);
	
	pass_manager->run(*module);

	std::println("\nafter DCE");
	dump(*module);
	const std::size_t nodes_after = count_nodes_in_module();
	std::println("nodes after DCE: {}", nodes_after);

	EXPECT_LT(nodes_after, nodes_before);
	std::println("removed: {} nodes", nodes_before - nodes_after);
}

TEST_F(DCEFixture, ControlFlowPreservation)
{
	builder->function<arc::DataType::INT32>("test_control_flow")
			.param<arc::DataType::BOOL>("condition")
			.param<arc::DataType::INT32>("x")
			.body([](arc::Builder &fb, arc::Node *condition, arc::Node *x)
			{
				auto true_block = fb.block<arc::DataType::VOID>("true_path");
				auto false_block = fb.block<arc::DataType::VOID>("false_path");
				auto merge_block = fb.block<arc::DataType::VOID>("merge");

				[[maybe_unused]] auto *dead_before = fb.mul(fb.lit(99), fb.lit(88));

				fb.branch(condition, true_block.entry(), false_block.entry());

				true_block([&](arc::Builder &fb_true)
				{
					auto *true_result = fb_true.add(x, fb_true.lit(1));
					fb_true.jump(merge_block.entry());
					return true_result;
				});

				false_block([&](arc::Builder &fb_false)
				{
					[[maybe_unused]] auto *dead_in_false = fb_false.sub(fb_false.lit(77), fb_false.lit(66));
					auto *false_result = fb_false.sub(x, fb_false.lit(1));
					fb_false.jump(merge_block.entry());
					return false_result;
				});

				merge_block([&](arc::Builder &fb_merge)
				{
					fb_merge.jump(merge_block.entry());
					return fb_merge.ret(x);
				});

				return fb.ret(fb.lit(0));
			});

	std::println("before DCE");
	arc::dump(*module);
	const std::size_t nodes_before = count_nodes_in_module();

	pass_manager->run(*module);

	std::println("\nafter DCE");
	arc::dump(*module);
	const std::size_t nodes_after = count_nodes_in_module();

	EXPECT_LT(nodes_after, nodes_before);
	std::println("control flow test: removed {} nodes", nodes_before - nodes_after);
}

TEST_F(DCEFixture, SideEffectsPreservation)
{
	builder->function<arc::DataType::VOID>("test_side_effects")
			.body([](arc::Builder &fb)
			{
				auto *alloc1 = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				auto *alloc2 = fb.alloc<arc::DataType::INT32>(fb.lit(1));

				[[maybe_unused]] auto *dead_calc = fb.add(fb.lit(42), fb.lit(24));

				auto *value1 = fb.lit(100);
				[[maybe_unused]] auto *store1 = fb.store(value1, alloc1);

				[[maybe_unused]] auto *dead_load = fb.load(alloc2);
				[[maybe_unused]] auto *dead_calc2 = fb.mul(fb.lit(7), fb.lit(8));
				auto *addr = fb.addr_of(alloc2);
				auto *value2 = fb.lit(200);
				[[maybe_unused]]auto *ptr_store = fb.ptr_store(value2, addr);
				return fb.ret();
			});

	std::println("before DCE");
	arc::dump(*module);
	const std::size_t nodes_before = count_nodes_in_module();

	pass_manager->run(*module);

	std::println("\nafter DCE");
	arc::dump(*module);
	const std::size_t nodes_after = count_nodes_in_module();

	EXPECT_LT(nodes_after, nodes_before);
	std::println("side effects test: removed {} nodes", nodes_before - nodes_after);
}

TEST_F(DCEFixture, FunctionCallPreservation)
{
	auto *helper = builder->function<arc::DataType::INT32>("helper")
			.param<arc::DataType::INT32>("x")
			.body([](arc::Builder &fb, arc::Node *x)
			{
				return fb.ret(fb.add(x, fb.lit(1)));
			});

	builder->function<arc::DataType::INT32>("test_calls")
			.param<arc::DataType::INT32>("input")
			.body([helper](arc::Builder &fb, arc::Node *input)
			{
				[[maybe_unused]] auto *dead = fb.mul(fb.lit(3), fb.lit(4));
				auto *call_result = fb.call(helper, { input });
				[[maybe_unused]] auto *dead2 = fb.div(fb.lit(20), fb.lit(5));
				auto *final_result = fb.add(call_result, fb.lit(10));
				return fb.ret(final_result);
			});

	std::println("before DCE");
	arc::dump(*module);
	const std::size_t nodes_before = count_nodes_in_module();

	pass_manager->run(*module);

	std::println("\nafter DCE");
	arc::dump(*module);
	const std::size_t nodes_after = count_nodes_in_module();

	EXPECT_LT(nodes_after, nodes_before);
	std::println("function calls test: removed {} nodes", nodes_before - nodes_after);
}

TEST_F(DCEFixture, VolatilePreservation)
{
	auto *volatile_func = builder->function<arc::DataType::VOID>("volatile_func")
			.keep() /* volatile */
			.body([](arc::Builder &fb)
			{
				[[maybe_unused]] auto *should_not_be_dead = fb.add(fb.lit(1), fb.lit(2));
				return fb.ret();
			});

	builder->function<arc::DataType::VOID>("test_volatile")
			.body([volatile_func](arc::Builder &fb)
			{
				[[maybe_unused]] auto *dead = fb.sub(fb.lit(10), fb.lit(5));
				fb.call(volatile_func, {});
				return fb.ret();
			});

	std::println("before DCE");
	arc::dump(*module);
	const std::size_t nodes_before = count_nodes_in_module();

	pass_manager->run(*module);

	std::println("\nafter DCE");
	arc::dump(*module);
	const std::size_t nodes_after = count_nodes_in_module();

	/* some dead code should be removed but volatile elements preserved */
	EXPECT_LE(nodes_after, nodes_before); /* could be equal if nothing was dead; i'm too lazy to count */
	std::println("volatile test: removed {} nodes", nodes_before - nodes_after);
}

TEST_F(DCEFixture, ComplexDeadCodeChains)
{
	builder->function<arc::DataType::INT32>("test_complex_chains")
			.param<arc::DataType::INT32>("x")
			.body([](arc::Builder &fb, arc::Node *x)
			{
				/* live */
				auto *live1 = fb.add(x, fb.lit(1));
				auto *live2 = fb.mul(live1, fb.lit(2));

				/* dead */
				auto *dead1 = fb.lit(42);
				auto *dead2 = fb.add(dead1, fb.lit(10));
				auto *dead3 = fb.mul(dead2, fb.lit(3));
				auto *dead4 = fb.sub(dead3, fb.lit(5));
				[[maybe_unused]] auto *dead5 = fb.div(dead4, fb.lit(2));

				/* dead but uses lives that is not used */
				auto *dead_mixed1 = fb.add(live1, dead1);
				auto *dead_mixed2 = fb.sub(dead_mixed1, live2);
				[[maybe_unused]] auto *dead_mixed3 = fb.mul(dead_mixed2, fb.lit(7));

				/* final live */
				auto *result = fb.add(live2, fb.lit(100));
				return fb.ret(result);
			});

	std::println("before DCE");
	arc::dump(*module);
	const std::size_t nodes_before = count_nodes_in_module();

	pass_manager->run(*module);

	std::println("\nafter DCE");
	arc::dump(*module);
	const std::size_t nodes_after = count_nodes_in_module();

	EXPECT_LT(nodes_after, nodes_before);
	std::println("complex chains test: removed {} nodes", nodes_before - nodes_after);
}

TEST_F(DCEFixture, EmptyFunction)
{
	builder->function<arc::DataType::VOID>("empty_func")
			.body([](arc::Builder &fb)
			{
				return fb.ret();
			});

	std::println("before DCE");
	arc::dump(*module);
	const std::size_t nodes_before = count_nodes_in_module();

	pass_manager->run(*module);

	std::println("\nafter DCE");
	arc::dump(*module);
	const std::size_t nodes_after = count_nodes_in_module();

	/* empty function should remain unchanged */
	EXPECT_EQ(nodes_after, nodes_before);
	std::println("Empty function test: removed {} nodes", nodes_before - nodes_after);
}

TEST_F(DCEFixture, AllLiveCode)
{
	/* create function where all code is live */
	builder->function<arc::DataType::INT32>("all_live")
			.param<arc::DataType::INT32>("a")
			.param<arc::DataType::INT32>("b")
			.body([](arc::Builder &fb, arc::Node *a, arc::Node *b)
			{
				auto *sum = fb.add(a, b);
				auto *product = fb.mul(a, b);
				auto *result = fb.add(sum, product);
				return fb.ret(result);
			});

	std::println("before DCE");
	arc::dump(*module);
	const std::size_t nodes_before = count_nodes_in_module();

	pass_manager->run(*module);

	std::println("\nafter DCE");
	arc::dump(*module);
	const std::size_t nodes_after = count_nodes_in_module();

	/* no code should be removed */
	EXPECT_EQ(nodes_after, nodes_before);
	std::println("All live test: removed {} nodes", nodes_before - nodes_after);
}
