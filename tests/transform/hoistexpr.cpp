/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <print>
#include <arc/analysis/tbaa.hpp>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/support/dump.hpp>
#include <arc/transform/hoistexpr.hpp>
#include <gtest/gtest.h>

class HoistExprFixture : public testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("hoistexpr_test");
		builder = std::make_unique<arc::Builder>(*module);
		pass_manager = std::make_unique<arc::PassManager>();
		pass_manager->add<arc::TypeBasedAliasAnalysisPass>()
					 .add<arc::HoistExpr>();
	}

	void TearDown() override
	{
		arc::dump(*module);
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
		for (arc::Region *child: region->children())
		{
			count += count_nodes(child, type);
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

	arc::Region *get_block_region(arc::Region *parent, const std::string &name)
	{
		for (arc::Region *child: parent->children())
		{
			if (child->name() == name)
				return child;
		}
		return nullptr;
	}

	std::unique_ptr<arc::Module> module;
	std::unique_ptr<arc::Builder> builder;
	std::unique_ptr<arc::PassManager> pass_manager;
};

TEST_F(HoistExprFixture, SimpleLoopInvariantMul)
{
	builder->function<arc::DataType::INT32>("simple_mul_hoist")
			.param<arc::DataType::INT32>("limit")
			.param<arc::DataType::INT32>("factor")
			.body([&](arc::Builder &fb, arc::Node *limit, arc::Node *factor)
			{
				auto loop = fb.block<arc::DataType::VOID>("loop");
				auto exit = fb.block<arc::DataType::VOID>("exit");

				auto *counter = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(0), counter);
				fb.jump(loop.entry());

				loop([&](arc::Builder &lb)
				{
					auto *invariant = lb.mul(factor, lb.lit(100));
					auto *i = lb.load(counter);
					auto *next = lb.add(i, lb.lit(1));
					lb.store(next, counter);
					auto *cond = lb.lt(next, limit);
					lb.branch(cond, loop.entry(), exit.entry());
					return invariant;
				});

				exit([&](arc::Builder &eb)
				{
					auto *final_count = eb.load(counter);
					return eb.ret(final_count);
				});

				return fb.ret(fb.lit(0));
			});

	auto *func = get_function_region("simple_mul_hoist");
	auto *loop_region = get_block_region(func, "loop");

	std::size_t mul_before = count_nodes(loop_region, arc::NodeType::MUL);
	pass_manager->run(*module);
	std::size_t mul_after = count_nodes(loop_region, arc::NodeType::MUL);

	EXPECT_EQ(mul_before, 1);
	EXPECT_EQ(mul_after, 0);
	std::println("MUL hoisted: {} -> {}", mul_before, mul_after);
}

TEST_F(HoistExprFixture, LoopDependentNotHoisted)
{
	builder->function<arc::DataType::INT32>("dependent_not_hoisted")
			.param<arc::DataType::INT32>("limit")
			.body([&](arc::Builder &fb, arc::Node *limit)
			{
				auto loop = fb.block<arc::DataType::VOID>("loop");
				auto exit = fb.block<arc::DataType::VOID>("exit");

				auto *counter = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(0), counter);
				fb.jump(loop.entry());

				loop([&](arc::Builder &lb)
				{
					auto *i = lb.load(counter);
					auto *dependent = lb.mul(i, lb.lit(2));
					auto *next = lb.add(i, lb.lit(1));
					lb.store(next, counter);
					auto *cond = lb.lt(next, limit);
					lb.branch(cond, loop.entry(), exit.entry());
					return dependent;
				});

				exit([&](arc::Builder &eb)
				{
					auto *final_count = eb.load(counter);
					return eb.ret(final_count);
				});

				return fb.ret(fb.lit(0));
			});

	auto *func = get_function_region("dependent_not_hoisted");
	auto *loop_region = get_block_region(func, "loop");

	std::size_t mul_before = count_nodes(loop_region, arc::NodeType::MUL);
	pass_manager->run(*module);
	std::size_t mul_after = count_nodes(loop_region, arc::NodeType::MUL);

	EXPECT_EQ(mul_before, 1);
	EXPECT_EQ(mul_after, 1);
	std::println("MUL preserved: {} -> {}", mul_before, mul_after);
}

TEST_F(HoistExprFixture, AggressiveDivisionHoisting)
{
	builder->function<arc::DataType::INT32>("aggressive_div")
			.param<arc::DataType::INT32>("limit")
			.param<arc::DataType::INT32>("divisor")
			.body([&](arc::Builder &fb, arc::Node *limit, arc::Node *divisor)
			{
				auto loop = fb.block<arc::DataType::VOID>("loop");
				auto exit = fb.block<arc::DataType::VOID>("exit");

				auto *counter = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(0), counter);
				fb.jump(loop.entry());

				loop([&](arc::Builder &lb)
				{
					auto *invariant = lb.div(divisor, lb.lit(3));
					auto *i = lb.load(counter);
					auto *next = lb.add(i, lb.lit(1));
					lb.store(next, counter);
					auto *cond = lb.lt(next, limit);
					lb.branch(cond, loop.entry(), exit.entry());
					return invariant;
				});

				exit([&](arc::Builder &eb)
				{
					auto *final_count = eb.load(counter);
					return eb.ret(final_count);
				});

				return fb.ret(fb.lit(0));
			});

	auto *func = get_function_region("aggressive_div");
	auto *loop_region = get_block_region(func, "loop");

	std::size_t div_before = count_nodes(loop_region, arc::NodeType::DIV);
	pass_manager->run(*module);
	std::size_t div_after = count_nodes(loop_region, arc::NodeType::DIV);

	EXPECT_EQ(div_before, 1);
	EXPECT_EQ(div_after, 0);
	std::println("DIV hoisted: {} -> {}", div_before, div_after);
}

TEST_F(HoistExprFixture, MultipleInvariantsHoisted)
{
	builder->function<arc::DataType::INT32>("multiple_invariants")
			.param<arc::DataType::INT32>("limit")
			.param<arc::DataType::INT32>("a")
			.param<arc::DataType::INT32>("b")
			.body([&](arc::Builder &fb, arc::Node *limit, arc::Node *a, arc::Node *b)
			{
				auto loop = fb.block<arc::DataType::VOID>("loop");
				auto exit = fb.block<arc::DataType::VOID>("exit");

				auto *counter = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(0), counter);
				fb.jump(loop.entry());

				loop([&](arc::Builder &lb)
				{
					auto *inv1 = lb.add(a, b);
					auto *inv2 = lb.mul(a, lb.lit(5));
					auto *inv3 = lb.sub(b, lb.lit(10));
					auto *result = lb.add(inv1, lb.add(inv2, inv3));

					auto *i = lb.load(counter);
					auto *next = lb.add(i, lb.lit(1));
					lb.store(next, counter);
					auto *cond = lb.lt(next, limit);
					lb.branch(cond, loop.entry(), exit.entry());
					return result;
				});

				exit([&](arc::Builder &eb)
				{
					auto *final_count = eb.load(counter);
					return eb.ret(final_count);
				});

				return fb.ret(fb.lit(0));
			});

	auto *func = get_function_region("multiple_invariants");
	auto *loop_region = get_block_region(func, "loop");

	std::size_t arith_before = count_nodes(loop_region, arc::NodeType::ADD) +
							   count_nodes(loop_region, arc::NodeType::MUL) +
							   count_nodes(loop_region, arc::NodeType::SUB);
	pass_manager->run(*module);
	std::size_t arith_after = count_nodes(loop_region, arc::NodeType::ADD) +
							  count_nodes(loop_region, arc::NodeType::MUL) +
							  count_nodes(loop_region, arc::NodeType::SUB);

	EXPECT_GT(arith_before, arith_after);
	std::println("Arithmetic ops: {} -> {}", arith_before, arith_after);
}

TEST_F(HoistExprFixture, VolatilePreserved)
{
	builder->function<arc::DataType::INT32>("volatile_preserved")
			.param<arc::DataType::INT32>("limit")
			.param<arc::DataType::INT32>("value")
			.body([&](arc::Builder &fb, arc::Node *limit, arc::Node *value)
			{
				auto loop = fb.block<arc::DataType::VOID>("loop");
				auto exit = fb.block<arc::DataType::VOID>("exit");

				auto *counter = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(0), counter);
				fb.jump(loop.entry());

				loop([&](arc::Builder &lb)
				{
					auto *volatile_op = lb.mul(value, lb.lit(7));
					volatile_op->traits |= arc::NodeTraits::VOLATILE;

					auto *i = lb.load(counter);
					auto *next = lb.add(i, lb.lit(1));
					lb.store(next, counter);
					auto *cond = lb.lt(next, limit);
					lb.branch(cond, loop.entry(), exit.entry());
					return volatile_op;
				});

				exit([&](arc::Builder &eb)
				{
					auto *final_count = eb.load(counter);
					return eb.ret(final_count);
				});

				return fb.ret(fb.lit(0));
			});

	auto *func = get_function_region("volatile_preserved");
	auto *loop_region = get_block_region(func, "loop");

	std::size_t mul_before = count_nodes(loop_region, arc::NodeType::MUL);
	pass_manager->run(*module);
	std::size_t mul_after = count_nodes(loop_region, arc::NodeType::MUL);

	EXPECT_EQ(mul_before, 1);
	EXPECT_EQ(mul_after, 1);
	std::println("Volatile MUL preserved: {} -> {}", mul_before, mul_after);
}

TEST_F(HoistExprFixture, NestedLoopOuterInvariant)
{
	builder->function<arc::DataType::INT32>("nested_outer_invariant")
			.param<arc::DataType::INT32>("n")
			.param<arc::DataType::INT32>("m")
			.param<arc::DataType::INT32>("constant")
			.body([&](arc::Builder &fb, arc::Node *n, arc::Node *m, arc::Node *constant)
			{
				auto outer = fb.block<arc::DataType::VOID>("outer");
				auto inner = fb.block<arc::DataType::VOID>("inner");
				auto inner_exit = fb.block<arc::DataType::VOID>("inner_exit");
				auto outer_exit = fb.block<arc::DataType::VOID>("outer_exit");

				auto *i_alloc = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				auto *j_alloc = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(0), i_alloc);
				fb.jump(outer.entry());

				outer([&](arc::Builder &ob)
				{
					ob.store(ob.lit(0), j_alloc);
					ob.jump(inner.entry());
					return ob.lit(0);
				});

				inner([&](arc::Builder &ib)
				{
					auto *outer_invariant = ib.mul(constant, ib.lit(42));
					auto *j = ib.load(j_alloc);
					auto *next_j = ib.add(j, ib.lit(1));
					ib.store(next_j, j_alloc);
					auto *inner_cond = ib.lt(next_j, m);
					ib.branch(inner_cond, inner.entry(), inner_exit.entry());
					return outer_invariant;
				});

				inner_exit([&](arc::Builder &ieb)
				{
					auto *i = ieb.load(i_alloc);
					auto *next_i = ieb.add(i, ieb.lit(1));
					ieb.store(next_i, i_alloc);
					auto *outer_cond = ieb.lt(next_i, n);
					ieb.branch(outer_cond, outer.entry(), outer_exit.entry());
					return ieb.lit(0);
				});

				outer_exit([&](arc::Builder &oeb)
				{
					auto *final_i = oeb.load(i_alloc);
					return oeb.ret(final_i);
				});

				return fb.ret(fb.lit(0));
			});

	auto *func = get_function_region("nested_outer_invariant");
	auto *inner_region = get_block_region(func, "inner");

	std::size_t mul_before = count_nodes(inner_region, arc::NodeType::MUL);
	pass_manager->run(*module);
	std::size_t mul_after = count_nodes(inner_region, arc::NodeType::MUL);

	EXPECT_EQ(mul_before, 1);
	EXPECT_EQ(mul_after, 0);
	std::println("Nested loop MUL hoisted: {} -> {}", mul_before, mul_after);
}

TEST_F(HoistExprFixture, NoLoopNoHoisting)
{
	builder->function<arc::DataType::INT32>("no_loop")
			.param<arc::DataType::INT32>("a")
			.param<arc::DataType::INT32>("b")
			.body([&](arc::Builder &fb, arc::Node *a, arc::Node *b)
			{
				auto *result1 = fb.add(a, b);
				auto *result2 = fb.mul(result1, fb.lit(2));
				auto *result3 = fb.sub(result2, fb.lit(5));
				return fb.ret(result3);
			});

	auto *func = get_function_region("no_loop");

	std::size_t ops_before = count_nodes(func, arc::NodeType::ADD) +
							 count_nodes(func, arc::NodeType::MUL) +
							 count_nodes(func, arc::NodeType::SUB);
	pass_manager->run(*module);
	std::size_t ops_after = count_nodes(func, arc::NodeType::ADD) +
							count_nodes(func, arc::NodeType::MUL) +
							count_nodes(func, arc::NodeType::SUB);

	EXPECT_EQ(ops_before, ops_after);
	std::println("Non-loop operations unchanged: {}", ops_after);
}

TEST_F(HoistExprFixture, ComplexInvariantChain)
{
	builder->function<arc::DataType::INT32>("complex_chain")
			.param<arc::DataType::INT32>("limit")
			.param<arc::DataType::INT32>("x")
			.param<arc::DataType::INT32>("y")
			.body([&](arc::Builder &fb, arc::Node *limit, arc::Node *x, arc::Node *y)
			{
				auto loop = fb.block<arc::DataType::VOID>("loop");
				auto exit = fb.block<arc::DataType::VOID>("exit");

				auto *counter = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(0), counter);
				fb.jump(loop.entry());

				loop([&](arc::Builder &lb)
				{
					auto *step1 = lb.add(x, y);
					auto *step2 = lb.mul(step1, lb.lit(3));
					auto *step3 = lb.sub(step2, lb.lit(7));
					auto *step4 = lb.div(step3, lb.lit(2));
					auto *final = lb.band(step4, lb.lit(0xFF));

					auto *i = lb.load(counter);
					auto *next = lb.add(i, lb.lit(1));
					lb.store(next, counter);
					auto *cond = lb.lt(next, limit);
					lb.branch(cond, loop.entry(), exit.entry());
					return final;
				});

				exit([&](arc::Builder &eb)
				{
					auto *final_count = eb.load(counter);
					return eb.ret(final_count);
				});

				return fb.ret(fb.lit(0));
			});

	auto *func = get_function_region("complex_chain");
	auto *loop_region = get_block_region(func, "loop");

	std::size_t ops_before = count_nodes(loop_region, arc::NodeType::ADD) +
							 count_nodes(loop_region, arc::NodeType::MUL) +
							 count_nodes(loop_region, arc::NodeType::SUB) +
							 count_nodes(loop_region, arc::NodeType::DIV) +
							 count_nodes(loop_region, arc::NodeType::BAND);
	pass_manager->run(*module);
	std::size_t ops_after = count_nodes(loop_region, arc::NodeType::ADD) +
							count_nodes(loop_region, arc::NodeType::MUL) +
							count_nodes(loop_region, arc::NodeType::SUB) +
							count_nodes(loop_region, arc::NodeType::DIV) +
							count_nodes(loop_region, arc::NodeType::BAND);

	EXPECT_GT(ops_before, ops_after);
	std::println("Complex chain hoisted: {} -> {}", ops_before, ops_after);
}
