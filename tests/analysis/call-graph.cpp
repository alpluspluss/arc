/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <print>
#include <arc/analysis/call-graph.hpp>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/support/dump.hpp>
#include <gtest/gtest.h>

class CallGraphFixture : public testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("cga_test_module");
		builder = std::make_unique<arc::Builder>(*module);
		pass_manager = std::make_unique<arc::PassManager>();
		pass_manager->add<arc::CallGraphAnalysisPass>();
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

	const arc::CallGraphResult &run_cga()
	{
		pass_manager->run(*module);
		return pass_manager->get<arc::CallGraphResult>();
	}
};

TEST_F(CallGraphFixture, DirectCallResolution)
{
	arc::Node *call_node = nullptr;

	auto *callee_func = builder->function<arc::DataType::INT32>("callee")
			.body([](arc::Builder &fb)
			{
				return fb.ret(fb.lit(42));
			});

	builder->function<arc::DataType::INT32>("caller")
			.body([&](arc::Builder &fb)
			{
				call_node = fb.call(callee_func);
				return fb.ret(call_node);
			});

	auto &cga = run_cga();

	EXPECT_EQ(cga.callee(call_node), callee_func);

	auto targets = cga.targets(call_node);
	ASSERT_EQ(targets.size(), 1);
	EXPECT_EQ(targets[0], callee_func);

	std::println("direct call resolution: passed");
}

TEST_F(CallGraphFixture, CalleeCallerRelationships)
{
	auto *func_a = builder->function<arc::DataType::VOID>("func_a")
			.body([](arc::Builder &fb)
			{
				return fb.ret();
			});

	auto *func_b = builder->function<arc::DataType::VOID>("func_b")
			.body([](arc::Builder &fb)
			{
				return fb.ret();
			});

	auto *main_func = builder->function<arc::DataType::VOID>("main")
			.body([&](arc::Builder &fb)
			{
				fb.call(func_a);
				fb.call(func_b);
				return fb.ret();
			});

	auto &cga = run_cga();

	auto callees = cga.callees(main_func);
	EXPECT_EQ(callees.size(), 2);
	EXPECT_TRUE(std::find(callees.begin(), callees.end(), func_a) != callees.end());
	EXPECT_TRUE(std::find(callees.begin(), callees.end(), func_b) != callees.end());

	auto callers_a = cga.callers(func_a);
	ASSERT_EQ(callers_a.size(), 1);
	EXPECT_EQ(callers_a[0], main_func);

	auto callers_b = cga.callers(func_b);
	ASSERT_EQ(callers_b.size(), 1);
	EXPECT_EQ(callers_b[0], main_func);

	std::println("callee caller relationships: passed");
}

TEST_F(CallGraphFixture, TransitiveCallCheck)
{
	auto *func_c = builder->function<arc::DataType::VOID>("func_c")
			.body([](arc::Builder &fb)
			{
				return fb.ret();
			});

	auto *func_b = builder->function<arc::DataType::VOID>("func_b")
			.body([&](arc::Builder &fb)
			{
				fb.call(func_c);
				return fb.ret();
			});

	auto *func_a = builder->function<arc::DataType::VOID>("func_a")
			.body([&](arc::Builder &fb)
			{
				fb.call(func_b);
				return fb.ret();
			});

	auto &cga = run_cga();

	EXPECT_TRUE(cga.calls(func_a, func_b));
	EXPECT_TRUE(cga.calls(func_b, func_c));
	EXPECT_TRUE(cga.calls(func_a, func_c));
	EXPECT_FALSE(cga.calls(func_c, func_a));

	std::println("transitive call check: passed");
}

TEST_F(CallGraphFixture, DirectRecursionDetection)
{
	auto recursive_func  = builder->opaque_t<arc::DataType::FUNCTION>("factorial");
	recursive_func.function<arc::DataType::INT32>()
			.param<arc::DataType::INT32>("n")
			.body([&](arc::Builder &fb, arc::Node *n)
			{
				auto *cond = fb.gt(n, fb.lit(1));

				auto then_block = fb.block<arc::DataType::INT32>("then");
				auto else_block = fb.block<arc::DataType::INT32>("else");

				fb.branch(cond, then_block.entry(), else_block.entry());

				auto *then_result = then_block([&](arc::Builder &tb)
				{
					auto *n_minus_1 = tb.sub(n, tb.lit(1));
					auto *recursive_call = tb.call(recursive_func.node(), { n_minus_1 });
					return tb.mul(n, recursive_call);
				});

				auto *else_result = else_block([&](arc::Builder &eb)
				{
					return eb.lit(1);
				});

				auto *result = fb.create_node(arc::NodeType::FROM, arc::DataType::INT32);
				result->inputs.push_back(then_result);
				result->inputs.push_back(else_result);

				return fb.ret(result);
			});

	auto &cga = run_cga();

	EXPECT_TRUE(cga.recursive(recursive_func.node()));
	EXPECT_TRUE(cga.calls(recursive_func.node(), recursive_func.node()));

	std::println("direct recursion detection: passed");
}

TEST_F(CallGraphFixture, IndirectRecursionDetection)
{
	auto func_a = builder->opaque_t<arc::DataType::FUNCTION>("func_a");
	auto func_b = builder->opaque_t<arc::DataType::FUNCTION>("func_b");

	func_a.function<arc::DataType::VOID>()
			.body([&](arc::Builder &fb)
			{
				fb.call(func_b.node());
				return fb.ret();
			});

	func_b.function<arc::DataType::VOID>()
			.body([&](arc::Builder &fb)
			{
				fb.call(func_a.node());
				return fb.ret();
			});

	auto &cga = run_cga();

	EXPECT_TRUE(cga.recursive(func_a.node()));
	EXPECT_TRUE(cga.recursive(func_b.node()));
	EXPECT_TRUE(cga.calls(func_a.node(), func_b.node()));
	EXPECT_TRUE(cga.calls(func_b.node(), func_a.node()));

	std::println("indirect recursion detection: passed");
}

TEST_F(CallGraphFixture, ParameterEscapeViaReturn)
{
	auto *identity_func = builder->function<arc::DataType::INT32>("identity")
			.param<arc::DataType::INT32>("value")
			.body([](arc::Builder &fb, arc::Node *value)
			{
				return fb.ret(value);
			});

	auto &cga = run_cga();

	EXPECT_TRUE(cga.escapes(identity_func, 0));

	std::println("parameter escape via return: passed");
}

TEST_F(CallGraphFixture, ParameterEscapeViaStore)
{
	auto *global_var = builder->alloc<arc::DataType::INT32>(builder->lit(1));

	auto *store_param = builder->function<arc::DataType::VOID>("store_param")
			.param<arc::DataType::INT32>("value")
			.body([&](arc::Builder &fb, arc::Node *value)
			{
				fb.store(value, global_var);
				return fb.ret();
			});

	auto &cga = run_cga();

	EXPECT_TRUE(cga.escapes(store_param, 0));

	std::println("parameter escape via store: passed");
}

TEST_F(CallGraphFixture, ParameterNoEscape)
{
	auto *local_func = builder->function<arc::DataType::INT32>("local_computation")
			.param<arc::DataType::INT32>("a")
			.param<arc::DataType::INT32>("b")
			.body([](arc::Builder &fb, arc::Node *a, arc::Node *b)
			{
				auto *sum = fb.add(a, b);
				auto *product = fb.mul(sum, fb.lit(2));
				return fb.ret(product);
			});

	auto &cga = run_cga();

	EXPECT_FALSE(cga.escapes(local_func, 0));
	EXPECT_FALSE(cga.escapes(local_func, 1));

	std::println("parameter no escape: passed");
}

TEST_F(CallGraphFixture, PureFunctionDetection)
{
	auto *pure_math = builder->function<arc::DataType::INT32>("pure_math")
			.param<arc::DataType::INT32>("x")
			.param<arc::DataType::INT32>("y")
			.body([](arc::Builder &fb, arc::Node *x, arc::Node *y)
			{
				auto *sum = fb.add(x, y);
				auto *square = fb.mul(sum, sum);
				return fb.ret(square);
			});

	auto *global_var = builder->alloc<arc::DataType::INT32>(builder->lit(1));

	auto *impure_func = builder->function<arc::DataType::VOID>("impure_func")
			.param<arc::DataType::INT32>("value")
			.body([&](arc::Builder &fb, arc::Node *value)
			{
				fb.store(value, global_var);
				return fb.ret();
			});

	auto &cga = run_cga();

	EXPECT_TRUE(cga.pure(pure_math));
	EXPECT_FALSE(cga.pure(impure_func));

	std::println("pure function detection: passed");
}

/* FIXME: broken test; need to make `Builder::call` accept function pointers */
TEST_F(CallGraphFixture, IndirectCallThroughPointer)
{
	auto *target_func = builder->function<arc::DataType::INT32>("target")
			.body([](arc::Builder &fb)
			{
				return fb.ret(fb.lit(100));
			});

	arc::Node *real_call_node = nullptr;
	arc::Node *indirect_call = builder->function<arc::DataType::INT32>("caller")
			.body([&](arc::Builder &fb)
			{
				auto *func_ptr_storage = fb.alloc<arc::DataType::FUNCTION>(fb.lit(1));
				auto *func_addr = fb.addr_of(target_func);
				fb.store(func_addr, func_ptr_storage);
				auto *func_ptr = fb.load(func_ptr_storage);
				real_call_node = fb.call(func_ptr);
				return fb.ret(real_call_node);
			});

	auto &cga = run_cga();
	EXPECT_EQ(cga.callee(indirect_call), nullptr);
	std::println("DEBUG: Expected target_func pointer: {}", static_cast<void*>(target_func));
	std::println("DEBUG: Test calling targets() with call_site: {}", static_cast<void*>(indirect_call));
	auto targets = cga.targets(real_call_node);
	std::println("DEBUG: Found {} targets", targets.size());
	for (auto* t : targets)
	{
		std::println("DEBUG: Target pointer: {}", static_cast<void*>(t));
	}

	EXPECT_TRUE(std::ranges::find(targets, target_func) != targets.end());

	std::println("indirect call through pointer: passed");
}

TEST_F(CallGraphFixture, CallSiteTracking)
{
	arc::Node *call1 = nullptr;
	arc::Node *call2 = nullptr;

	auto *helper = builder->function<arc::DataType::VOID>("helper")
			.body([](arc::Builder &fb)
			{
				return fb.ret();
			});

	auto *caller_func = builder->function<arc::DataType::VOID>("caller")
			.body([&](arc::Builder &fb)
			{
				call1 = fb.call(helper);
				call2 = fb.call(helper);
				return fb.ret();
			});

	auto &cga = run_cga();

	auto call_sites = cga.call_sites(caller_func);
	EXPECT_EQ(call_sites.size(), 2);
	EXPECT_TRUE(std::find(call_sites.begin(), call_sites.end(), call1) != call_sites.end());
	EXPECT_TRUE(std::find(call_sites.begin(), call_sites.end(), call2) != call_sites.end());

	EXPECT_EQ(cga.containing_fn(call1), caller_func);
	EXPECT_EQ(cga.containing_fn(call2), caller_func);

	std::println("call site tracking: passed");
}

TEST_F(CallGraphFixture, ExternFunctionConservativeAnalysis)
{
	auto extern_func = builder->function<arc::DataType::VOID>("external_function")
			.param<arc::DataType::INT32>("param")
			.imported() /* note: make this actually return an incomplete node */
			.body([&](arc::Builder& fb, arc::Node *)
			{
				return fb.ret();
			});

	auto &cga = run_cga();

	EXPECT_TRUE(cga.escapes(extern_func, 0));
	EXPECT_FALSE(cga.pure(extern_func));

	std::println("extern function conservative analysis: passed");
}
