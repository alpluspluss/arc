/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <print>
#include <arc/analysis/tbaa.hpp>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/support/dump.hpp>
#include <arc/transform/cse.hpp>
#include <gtest/gtest.h>

class CSEFixture : public testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("simple_cse_test");
		builder = std::make_unique<arc::Builder>(*module);
		pass_manager = std::make_unique<arc::PassManager>();
		pass_manager->add<arc::TypeBasedAliasAnalysisPass>();
		pass_manager->add<arc::CommonSubexpressionEliminationPass>();
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
};

TEST_F(CSEFixture, NoCommonExpressionsNoChanges)
{
	arc::Node* add = nullptr;
	arc::Node* mul = nullptr;

	builder->function<arc::DataType::INT32>("test_function")
			.param<arc::DataType::INT32>("param1")
			.param<arc::DataType::INT32>("param2")
			.body([&](arc::Builder &fb, arc::Node *param1, arc::Node *param2)
			{
				/* different expressions - no CSE opportunities */
				add = fb.add(param1, param2);
				mul = fb.mul(param1, param2);
				return fb.ret(add);
			});

	pass_manager->run(*module);
	EXPECT_NE(add, mul);
	EXPECT_EQ(add->inputs.size(), 2);
	EXPECT_EQ(mul->inputs.size(), 2);
	std::println("no CSE opportunities test passed");
}

TEST_F(CSEFixture, EliminatesIdenticalExpressions)
{
	arc::Node* add1 = nullptr;
	arc::Node* add2 = nullptr;
	arc::Node* mul = nullptr;

	builder->function<arc::DataType::INT32>("test_function")
			.param<arc::DataType::INT32>("param1")
			.param<arc::DataType::INT32>("param2")
			.body([&](arc::Builder &fb, arc::Node *param1, arc::Node *param2)
			{
				add1 = fb.add(param1, param2);
				add2 = fb.add(param1, param2); /* duplicate */
				mul = fb.mul(add1, add2);
				return fb.ret(mul);
			});

	std::println("before CSE");
	arc::dump(*module);

	pass_manager->run(*module);

	std::println("\nafter CSE");
	arc::dump(*module);
	EXPECT_EQ(mul->inputs.size(), 2);
	EXPECT_EQ(mul->inputs[0], mul->inputs[1]);
	EXPECT_EQ(mul->inputs[0], add1);
	std::println("identical expressions elimination test passed");
}

TEST_F(CSEFixture, HandlesCommutativeOperations)
{
	arc::Node* add1 = nullptr;
	arc::Node* add2 = nullptr;
	arc::Node* mul = nullptr;

	builder->function<arc::DataType::INT32>("test_function")
			.param<arc::DataType::INT32>("param1")
			.param<arc::DataType::INT32>("param2")
			.body([&](arc::Builder &fb, arc::Node *param1, arc::Node *param2)
			{
				add1 = fb.add(param1, param2);
				add2 = fb.add(param2, param1);
				mul = fb.mul(add1, add2);
				return fb.ret(mul);
			});

	std::println("before CSE");
	arc::dump(*module);

	pass_manager->run(*module);

	std::println("\nafter CSE");
	arc::dump(*module);
	EXPECT_EQ(mul->inputs.size(), 2);
	EXPECT_EQ(mul->inputs[0], mul->inputs[1]);
	std::println("commutative operations test passed");
}

TEST_F(CSEFixture, IdenticalLiterals)
{
	arc::Node* lit1 = nullptr;
	arc::Node* lit2 = nullptr;
	arc::Node* add = nullptr;

	builder->function<arc::DataType::INT32>("test_function")
			.body([&](arc::Builder &fb)
			{
				/* identical literals */
				lit1 = fb.lit(42);
				lit2 = fb.lit(42); /* duplicate */
				add = fb.add(lit1, lit2);
				return fb.ret(add);
			});

	std::println("before CSE");
	arc::dump(*module);

	pass_manager->run(*module);

	std::println("\nafter CSE");
	arc::dump(*module);

	EXPECT_EQ(add->inputs.size(), 2);
	EXPECT_EQ(add->inputs[0], add->inputs[1]);
	std::println("identical literals test passed");
}

TEST_F(CSEFixture, DifferentTypesNotEliminated)
{
	arc::Node* add_i32 = nullptr;
	arc::Node* add_i64 = nullptr;

	builder->function<arc::DataType::VOID>("test_function")
			.body([&](arc::Builder &fb)
			{
				auto *param_i32 = fb.lit(static_cast<std::int32_t>(10));
				auto *param_i64 = fb.lit(static_cast<std::int64_t>(10));
				auto *lit_i32 = fb.lit(static_cast<std::int32_t>(20));
				auto *lit_i64 = fb.lit(static_cast<std::int64_t>(20));

				add_i32 = fb.add(param_i32, lit_i32);
				add_i64 = fb.add(param_i64, lit_i64);
				return fb.ret();
			});

	pass_manager->run(*module);
	EXPECT_NE(add_i32, add_i64);
	EXPECT_EQ(add_i32->type_kind, arc::DataType::INT32);
	EXPECT_EQ(add_i64->type_kind, arc::DataType::INT64);
	std::println("different types preservation test passed");
}

TEST_F(CSEFixture, VolatileNodesNotEliminated)
{
	arc::Node* add1 = nullptr;
	arc::Node* add2 = nullptr;
	arc::Node* mul = nullptr;

	builder->function<arc::DataType::INT32>("test_volatile")
			.param<arc::DataType::INT32>("x")
			.body([&](arc::Builder &fb, arc::Node *x)
			{
				add1 = fb.add(x, fb.lit(10));
				add2 = fb.add(x, fb.lit(10));
				add2->traits |= arc::NodeTraits::VOLATILE;
				mul = fb.mul(add1, add2);
				return fb.ret(mul);
			});

	pass_manager->run(*module);
	EXPECT_EQ(mul->inputs.size(), 2);
	EXPECT_NE(mul->inputs[0], mul->inputs[1]);
	EXPECT_EQ(mul->inputs[0], add1);
	EXPECT_EQ(mul->inputs[1], add2);
	std::println("volatile preservation test passed");
}

TEST_F(CSEFixture, ComplexExpressionsEliminated)
{
	arc::Node* add1 = nullptr;
	arc::Node* add2 = nullptr;
	arc::Node* mul1 = nullptr;
	arc::Node* mul2 = nullptr;
	arc::Node* final_add = nullptr;

	builder->function<arc::DataType::INT32>("test_function")
			.param<arc::DataType::INT32>("param1")
			.param<arc::DataType::INT32>("param2")
			.body([&](arc::Builder &fb, arc::Node *param1, arc::Node *param2)
			{
				/* (param1 + param2) * param1 */
				add1 = fb.add(param1, param2);
				mul1 = fb.mul(add1, param1);

				/* (param1 + param2) * param1 - duplicate complex expression */
				add2 = fb.add(param1, param2);
				mul2 = fb.mul(add2, param1);

				/* use both results */
				final_add = fb.add(mul1, mul2);
				return fb.ret(final_add);
			});

	std::println("before CSE");
	arc::dump(*module);

	pass_manager->run(*module);

	std::println("\nafter CSE");
	arc::dump(*module);

	/* verify that complex expression elimination worked */
	EXPECT_EQ(final_add->inputs.size(), 2);
	EXPECT_EQ(final_add->inputs[0], final_add->inputs[1]);
	std::println("complex expressions elimination test passed");
}

TEST_F(CSEFixture, FloatingPointLiterals)
{
	arc::Node* lit1 = nullptr;
	arc::Node* lit2 = nullptr;
	arc::Node* lit3 = nullptr;
	arc::Node* add1 = nullptr;
	arc::Node* add2 = nullptr;

	builder->function<arc::DataType::VOID>("test_function")
			.body([&](arc::Builder &fb)
			{
				lit1 = fb.lit(3.14159);
				lit2 = fb.lit(3.14159); /* duplicate */
				lit3 = fb.lit(2.71828); /* different value */

				add1 = fb.add(lit1, lit3);
				add2 = fb.add(lit2, lit3); /* should use lit1 instead of lit2 */
				return fb.ret();
			});

	std::println("before CSE");
	arc::dump(*module);

	pass_manager->run(*module);

	std::println("\nafter CSE");
	arc::dump(*module);

	/* verify floating point literal CSE worked */
	EXPECT_EQ(add1->inputs[0], add2->inputs[0]); /* both should use lit1 */
	EXPECT_EQ(add1->inputs[1], add2->inputs[1]); /* both should use lit3 */
	std::println("floating point literals test passed");
}

TEST_F(CSEFixture, BooleanLiterals)
{
	arc::Node* true1 = nullptr;
	arc::Node* true2 = nullptr;
	arc::Node* false1 = nullptr;
	arc::Node* and1 = nullptr;
	arc::Node* and2 = nullptr;

	builder->function<arc::DataType::VOID>("test_function")
			.body([&](arc::Builder &fb)
			{
				true1 = fb.lit(true);
				true2 = fb.lit(true); /* duplicate */
				false1 = fb.lit(false);

				and1 = fb.band(true1, false1);
				and2 = fb.band(true2, false1); /* should use true1 instead of true2 */
				return fb.ret();
			});

	std::println("before CSE");
	arc::dump(*module);

	pass_manager->run(*module);

	std::println("\nafter CSE");
	arc::dump(*module);

	/* verify boolean literal CSE worked */
	EXPECT_EQ(and1->inputs[0], and2->inputs[0]); /* both should use true1 */
	EXPECT_EQ(and1->inputs[1], and2->inputs[1]); /* both should use false1 */
	std::println("boolean literals test passed");
}

TEST_F(CSEFixture, MemoryOperationsWithTBAA)
{
	arc::Node* alloc = nullptr;
	arc::Node* load1 = nullptr;
	arc::Node* load2 = nullptr;
	arc::Node* add = nullptr;

	builder->function<arc::DataType::INT32>("test_memory")
			.body([&](arc::Builder &fb)
			{
				alloc = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				fb.store(fb.lit(100), alloc);

				load1 = fb.load(alloc);
				load2 = fb.load(alloc);

				add = fb.add(load1, load2);
				return fb.ret(add);
			});

	std::println("before CSE");
	arc::dump(*module);

	pass_manager->run(*module);

	std::println("\nafter CSE");
	arc::dump(*module);

	EXPECT_EQ(add->inputs.size(), 2);
	EXPECT_EQ(add->inputs[0], add->inputs[1]);
	EXPECT_EQ(add->inputs[0], load1);
	std::println("memory operations with TBAA test passed");
}

TEST_F(CSEFixture, VectorOperationsCSE)
{
	arc::Node* vec1 = nullptr;
	arc::Node* vec2 = nullptr;
	arc::Node* add = nullptr;

	builder->function<arc::DataType::VOID>("test_vectors")
			.body([&](arc::Builder &fb)
			{
				auto *a = fb.lit(1.0f);
				auto *b = fb.lit(2.0f);
				auto *c = fb.lit(3.0f);
				auto *d = fb.lit(4.0f);

				vec1 = fb.vector_build({ a, b, c, d });
				vec2 = fb.vector_build({ a, b, c, d });

				add = fb.add(vec1, vec2);
				return fb.ret();
			});

	std::println("before CSE");
	arc::dump(*module);

	pass_manager->run(*module);

	std::println("\nafter CSE");
	arc::dump(*module);

	EXPECT_EQ(add->inputs.size(), 2);
	EXPECT_EQ(add->inputs[0], add->inputs[1]);
	EXPECT_EQ(add->inputs[0], vec1);
	std::println("vector operations CSE test passed");
}
