/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <print>
#include <arc/analysis/call-graph.hpp>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/support/dump.hpp>
#include <arc/transform/inliner.hpp>
#include <gtest/gtest.h>

class InlinerFixture : public testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("inliner_test");
		builder = std::make_unique<arc::Builder>(*module);
		inliner = std::make_unique<arc::Inliner>();
		pass_manager = std::make_unique<arc::PassManager>();
		pass_manager->add<arc::CallGraphAnalysisPass>();
	}

	void TearDown() override
	{
		arc::dump(*module);
		pass_manager.reset();
		inliner.reset();
		builder.reset();
		module.reset();
	}

	std::unique_ptr<arc::Module> module;
	std::unique_ptr<arc::Builder> builder;
	std::unique_ptr<arc::Inliner> inliner;
	std::unique_ptr<arc::PassManager> pass_manager;
};

TEST_F(InlinerFixture, EvaluateSimpleFunction)
{
	arc::Node *add_func = builder->function<arc::DataType::INT32>("add")
			.param<arc::DataType::INT32>("a")
			.param<arc::DataType::INT32>("b")
			.body([](arc::Builder &fb, arc::Node *a, arc::Node *b)
			{
				return fb.ret(fb.add(a, b));
			});

	arc::Node *call_site = nullptr;
	builder->function<arc::DataType::INT32>("main")
			.body([&](arc::Builder &fb)
			{
				call_site = fb.call(add_func, { fb.lit(10), fb.lit(20) });
				return fb.ret(call_site);
			});

	auto decision = inliner->evaluate(call_site, add_func);
	EXPECT_TRUE(decision.should_inline);
	EXPECT_GT(decision.benefit, 2.0f);
	EXPECT_LT(decision.cost, 30);
	std::println("simple function evaluation test passed");
}

TEST_F(InlinerFixture, InlineSimpleFunction)
{
	arc::Node *add_func = builder->function<arc::DataType::INT32>("add")
			.param<arc::DataType::INT32>("a")
			.param<arc::DataType::INT32>("b")
			.body([](arc::Builder &fb, arc::Node *a, arc::Node *b)
			{
				return fb.ret(fb.add(a, b));
			});

	arc::Node *call_site = nullptr;
	builder->function<arc::DataType::INT32>("main")
			.body([&](arc::Builder &fb)
			{
				call_site = fb.call(add_func, { fb.lit(10), fb.lit(20) });
				return fb.ret(call_site);
			});

	std::println("before inlining");
	arc::dump(*module);

	auto result = inliner->inline_call(call_site, add_func, *module);

	std::println("\nafter inlining");
	arc::dump(*module);

	EXPECT_TRUE(result.success);
	EXPECT_NE(result.return_value, nullptr);
	EXPECT_EQ(result.modified.size(), 1);
	std::println("simple function inlining test passed");
}

TEST_F(InlinerFixture, InlineWithConstantArguments)
{
	arc::Node *compute_func = builder->function<arc::DataType::INT32>("compute")
			.param<arc::DataType::INT32>("x")
			.param<arc::DataType::INT32>("y")
			.body([](arc::Builder &fb, arc::Node *x, arc::Node *y)
			{
				auto *mul = fb.mul(x, y);
				auto *add = fb.add(mul, fb.lit(5));
				return fb.ret(add);
			});

	arc::Node *call_site = nullptr;
	builder->function<arc::DataType::INT32>("main")
			.body([&](arc::Builder &fb)
			{
				call_site = fb.call(compute_func, { fb.lit(42), fb.lit(3) });
				return fb.ret(call_site);
			});
	arc::dump(*module);
	auto decision = inliner->evaluate(call_site, compute_func);
	EXPECT_TRUE(decision.should_inline);
	EXPECT_GT(decision.benefit, 7.0f);

	auto result = inliner->inline_call(call_site, compute_func, *module);
	EXPECT_TRUE(result.success);
	std::println("constant arguments inlining test passed");
}

TEST_F(InlinerFixture, RejectFunctionWithMultipleReturns)
{
	arc::Node *multi_ret_func = builder->function<arc::DataType::INT32>("multi_ret")
			.param<arc::DataType::BOOL>("flag")
			.param<arc::DataType::INT32>("x")
			.body([&](arc::Builder &fb, arc::Node *flag, arc::Node *x)
			{
				auto *then_block = fb.block<arc::DataType::INT32>("then")([&](arc::Builder &bb)
				{
					return bb.ret(bb.lit(10));
				});
				auto *else_block = fb.block<arc::DataType::INT32>("else")([&](arc::Builder &bb)
				{
					return bb.ret(x);
				});
				fb.branch(flag, then_block->parent->entry(), else_block->parent->entry());
				return nullptr;
			});

	arc::Node *call_site = nullptr;
	builder->function<arc::DataType::INT32>("main")
			.body([&](arc::Builder &fb)
			{
				call_site = fb.call(multi_ret_func, { fb.lit(true), fb.lit(5) });
				return fb.ret(call_site);
			});
	arc::dump(*module);
	auto decision = inliner->evaluate(call_site, multi_ret_func);
	EXPECT_FALSE(decision.should_inline);
	EXPECT_EQ(decision.reason, "function not suitable for inlining");
	std::println("multiple returns rejection test passed");
}

TEST_F(InlinerFixture, RejectLargeFunction)
{
	arc::Node *large_func = builder->function<arc::DataType::INT32>("large")
			.param<arc::DataType::INT32>("x")
			.body([](arc::Builder &fb, arc::Node *x)
			{
				auto *result = x;
				for (int i = 0; i < 50; ++i)
				{
					result = fb.add(result, fb.lit(i));
				}
				return fb.ret(result);
			});

	arc::Node *call_site = nullptr;
	builder->function<arc::DataType::INT32>("main")
			.body([&](arc::Builder &fb)
			{
				call_site = fb.call(large_func, { fb.lit(10) });
				return fb.ret(call_site);
			});
	arc::dump(*module);
	auto decision = inliner->evaluate(call_site, large_func);
	EXPECT_FALSE(decision.should_inline);
	EXPECT_GT(decision.cost, 30);
	std::println("large function rejection test passed");
}

TEST_F(InlinerFixture, HandleVoidFunction)
{
	arc::Node *void_func = builder->function<arc::DataType::VOID>("print_value")
			.param<arc::DataType::INT32>("x")
			.body([](arc::Builder &fb, arc::Node *x)
			{
				return fb.ret();
			});

	arc::Node *call_site = nullptr;
	builder->function<arc::DataType::VOID>("main")
			.body([&](arc::Builder &fb)
			{
				call_site = fb.call(void_func, { fb.lit(42) });
				return fb.ret();
			});
	arc::dump(*module);
	auto result = inliner->inline_call(call_site, void_func, *module);
	EXPECT_TRUE(result.success);
	EXPECT_EQ(result.return_value, nullptr);
	std::println("void function inlining test passed");
}

TEST_F(InlinerFixture, RejectRecursiveFunction)
{
	auto factorial_func = builder->opaque_t<arc::DataType::FUNCTION>("factorial");
	factorial_func.function<arc::DataType::INT32>()
			.param<arc::DataType::INT32>("n")
			.body([&](arc::Builder &fb, arc::Node *n)
			{
				auto *zero = fb.lit(0);
				auto *one = fb.lit(1);
				auto *cond = fb.eq(n, zero);

				auto *base_case = fb.block<arc::DataType::INT32>("base")([&](arc::Builder &bb)
				{
					return bb.ret(one);
				});

				auto *recursive_case = fb.block<arc::DataType::INT32>("recursive")([&](arc::Builder &bb)
				{
					auto *n_minus_1 = bb.sub(n, one);
					auto *rec_call = bb.call(factorial_func.node(), { n_minus_1 });
					auto *result = bb.mul(n, rec_call);
					return bb.ret(result);
				});

				fb.branch(cond, base_case->parent->entry(), recursive_case->parent->entry());
				return nullptr;
			});

	arc::dump(*module);
	auto decision = inliner->evaluate(factorial_func.node(), factorial_func.node());
	EXPECT_FALSE(decision.should_inline);
	std::println("recursive function rejection test passed");
}

TEST_F(InlinerFixture, WithCallGraphAnalysis)
{
	arc::Node *pure_func = builder->function<arc::DataType::INT32>("pure_add")
			.param<arc::DataType::INT32>("a")
			.param<arc::DataType::INT32>("b")
			.body([](arc::Builder &fb, arc::Node *a, arc::Node *b)
			{
				return fb.ret(fb.add(a, b));
			});

	arc::Node* c = nullptr;
	auto* main_fn = builder->function<arc::DataType::INT32>("main")
			.body([&](arc::Builder &fb)
			{
				c = fb.call(pure_func, { fb.lit(1), fb.lit(2) });
				return fb.ret(c);
			});

	pass_manager->run(*module);
	const auto &cg = pass_manager->get<arc::CallGraphResult>();
	auto decision = inliner->evaluate(c, pure_func, &cg);
	EXPECT_TRUE(decision.should_inline) << decision.reason;
	arc::dump(*module);
	auto result = inliner->inline_call(c, pure_func, *module, &cg);
	EXPECT_TRUE(result.success);
	std::println("call graph integration test passed");
}

TEST_F(InlinerFixture, InlineMultipleCallSites)
{
	arc::Node *helper_func = builder->function<arc::DataType::INT32>("helper")
			.param<arc::DataType::INT32>("x")
			.body([](arc::Builder &fb, arc::Node *x)
			{
				return fb.ret(fb.mul(x, fb.lit(2)));
			});

	arc::Node *call1 = nullptr;
	arc::Node *call2 = nullptr;
	builder->function<arc::DataType::INT32>("main")
			.body([&](arc::Builder &fb)
			{
				call1 = fb.call(helper_func, { fb.lit(5) });
				call2 = fb.call(helper_func, { fb.lit(10) });
				auto *result = fb.add(call1, call2);
				return fb.ret(result);
			});
	arc::dump(*module);
	auto result1 = inliner->inline_call(call1, helper_func, *module);
	auto result2 = inliner->inline_call(call2, helper_func, *module);

	EXPECT_TRUE(result1.success);
	EXPECT_TRUE(result2.success);
	std::println("multiple call sites test passed");
}

TEST_F(InlinerFixture, InlineWithFloatingPoint)
{
	arc::Node *math_func = builder->function<arc::DataType::FLOAT32>("compute")
			.param<arc::DataType::FLOAT32>("x")
			.param<arc::DataType::FLOAT32>("y")
			.body([](arc::Builder &fb, arc::Node *x, arc::Node *y)
			{
				auto *mul = fb.mul(x, y);
				auto *add = fb.add(mul, fb.lit(3.14f));
				return fb.ret(add);
			});

	arc::Node *call_site = nullptr;
	builder->function<arc::DataType::FLOAT32>("main")
			.body([&](arc::Builder &fb)
			{
				call_site = fb.call(math_func, { fb.lit(2.0f), fb.lit(1.5f) });
				return fb.ret(call_site);
			});
	arc::dump(*module);
	auto result = inliner->inline_call(call_site, math_func, *module);
	EXPECT_TRUE(result.success);
	EXPECT_NE(result.return_value, nullptr);
	std::println("floating point inlining test passed");
}

TEST_F(InlinerFixture, ConfigurableThresholds)
{
	arc::Node *medium_func = builder->function<arc::DataType::INT32>("medium")
			.param<arc::DataType::INT32>("x")
			.body([](arc::Builder &fb, arc::Node *x)
			{
				auto *result = x;
				for (int i = 0; i < 20; ++i)
				{
					result = fb.add(result, fb.lit(i));
				}
				return fb.ret(result);
			});

	arc::Node *call_site = nullptr;
	builder->function<arc::DataType::INT32>("main")
			.body([&](arc::Builder &fb)
			{
				call_site = fb.call(medium_func, { fb.lit(10) });
				return fb.ret(call_site);
			});

	arc::Inliner::Config config;
	config.max_size = 15;
	config.min_benefit = 1.0f;
	inliner->set_config(config);

	auto decision1 = inliner->evaluate(call_site, medium_func);
	EXPECT_FALSE(decision1.should_inline);

	config.max_size = 50;
	inliner->set_config(config);
	arc::dump(*module);
	auto decision2 = inliner->evaluate(call_site, medium_func);
	EXPECT_TRUE(decision2.should_inline);
	std::println("configurable thresholds test passed");
}
