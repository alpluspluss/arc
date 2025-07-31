/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/support/dump.hpp>
#include <arc/transform/constfold.hpp>
#include <gtest/gtest.h>

class ConstFoldFixture : public testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("constfold_test");
		builder = std::make_unique<arc::Builder>(*module);
		pass_manager = std::make_unique<arc::PassManager>();
		pass_manager->add<arc::ConstantFoldingPass>();
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

	std::unique_ptr<arc::Module> module;
	std::unique_ptr<arc::Builder> builder;
	std::unique_ptr<arc::PassManager> pass_manager;
};

TEST_F(ConstFoldFixture, ArithmeticConstantFolding)
{
	builder->function<arc::DataType::INT32>("test_arithmetic")
			.body([&](arc::Builder &fb)
			{
				auto *add = fb.add(fb.lit(10), fb.lit(20));
				[[maybe_unused]] auto *sub = fb.sub(fb.lit(50), fb.lit(15));
				[[maybe_unused]] auto *mul = fb.mul(fb.lit(6), fb.lit(7));
				[[maybe_unused]] auto *div = fb.div(fb.lit(84), fb.lit(2));
				return fb.ret(add);
			});
	pass_manager->run(*module);
	auto *func_region = get_function_region("test_arithmetic");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_nodes(func_region, arc::NodeType::ADD), 0);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::SUB), 0);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::MUL), 0);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::DIV), 0);

	auto *ret = find_return(func_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::LIT);
	EXPECT_EQ(ret_value->type_kind, arc::DataType::INT32);
	EXPECT_EQ(ret_value->value.get<arc::DataType::INT32>(), 30);
}

TEST_F(ConstFoldFixture, ComparisonFolding)
{
	builder->function<arc::DataType::BOOL>("test_comparisons")
			.body([&](arc::Builder &fb)
			{
				auto *eq = fb.eq(fb.lit(42), fb.lit(42));
				[[maybe_unused]] auto *lt = fb.lt(fb.lit(10), fb.lit(20));
				[[maybe_unused]] auto *gte = fb.gte(fb.lit(5), fb.lit(10));
				[[maybe_unused]] auto *bool_eq = fb.eq(fb.lit(true), fb.lit(false));
				return fb.ret(eq);
			});

	pass_manager->run(*module);

	auto *func_region = get_function_region("test_comparisons");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_nodes(func_region, arc::NodeType::EQ), 0);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::LT), 0);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::GTE), 0);

	auto *ret = find_return(func_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::LIT);
	EXPECT_EQ(ret_value->type_kind, arc::DataType::BOOL);
	EXPECT_EQ(ret_value->value.get<arc::DataType::BOOL>(), true);
}

TEST_F(ConstFoldFixture, BitwiseFolding)
{
	builder->function<arc::DataType::UINT32>("test_bitwise")
			.body([&](arc::Builder &fb)
			{
				auto *val1 = fb.lit(static_cast<std::uint32_t>(0xAAAA));
				auto *val2 = fb.lit(static_cast<std::uint32_t>(0x5555));
				auto *shift_amt = fb.lit(static_cast<std::uint32_t>(2));

				auto *band = fb.band(val1, val2);
				[[maybe_unused]] auto *bor = fb.bor(val1, val2);
				[[maybe_unused]] auto *bxor = fb.bxor(val1, val2);
				[[maybe_unused]] auto *bshl = fb.bshl(val1, shift_amt);
				[[maybe_unused]] auto *bnot = fb.bnot(val1);
				return fb.ret(band);
			});

	pass_manager->run(*module);

	auto *func_region = get_function_region("test_bitwise");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_nodes(func_region, arc::NodeType::BAND), 0);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::BOR), 0);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::BXOR), 0);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::BSHL), 0);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::BNOT), 0);

	auto *ret = find_return(func_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::LIT);
	EXPECT_EQ(ret_value->type_kind, arc::DataType::UINT32);
	EXPECT_EQ(ret_value->value.get<arc::DataType::UINT32>(), 0);
}

TEST_F(ConstFoldFixture, FloatingPointFolding)
{
	builder->function<arc::DataType::FLOAT32>("test_float")
			.body([&](arc::Builder &fb)
			{
				auto *add_f32 = fb.add(fb.lit(3.14f), fb.lit(2.86f));
				[[maybe_unused]] auto *div_f64 = fb.div(fb.lit(10.0), fb.lit(3.0));
				[[maybe_unused]] auto *mod_f64 = fb.mod(fb.lit(7.5), fb.lit(2.5));
				return fb.ret(add_f32);
			});

	pass_manager->run(*module);

	auto *func_region = get_function_region("test_float");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_nodes(func_region, arc::NodeType::ADD), 0);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::DIV), 0);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::MOD), 0);

	auto *ret = find_return(func_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::LIT);
	EXPECT_EQ(ret_value->type_kind, arc::DataType::FLOAT32);
	EXPECT_NEAR(ret_value->value.get<arc::DataType::FLOAT32>(), 6.0f, 0.001f);
}

TEST_F(ConstFoldFixture, DivisionByZeroPreserved)
{
	builder->function<arc::DataType::INT32>("test_division_by_zero")
			.body([&](arc::Builder &fb)
			{
				[[maybe_unused]] auto *div_int = fb.div(fb.lit(42), fb.lit(0));
				[[maybe_unused]] auto *mod_int = fb.mod(fb.lit(42), fb.lit(0));
				[[maybe_unused]] auto *div_float = fb.div(fb.lit(3.14), fb.lit(0.0));
				[[maybe_unused]] auto *valid_div = fb.div(fb.lit(84), fb.lit(2));
				return fb.ret(valid_div);
			});

	pass_manager->run(*module);

	auto *func_region = get_function_region("test_division_by_zero");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_nodes(func_region, arc::NodeType::DIV), 2);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::MOD), 1);

	auto *ret = find_return(func_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::LIT);
	EXPECT_EQ(ret_value->type_kind, arc::DataType::INT32);
	EXPECT_EQ(ret_value->value.get<arc::DataType::INT32>(), 42);
}

TEST_F(ConstFoldFixture, BranchFolding)
{
	arc::Node *true_block = nullptr;
	arc::Node *false_block = nullptr;

	builder->function<arc::DataType::VOID>("test_branch_folding")
			.body([&](arc::Builder &fb)
			{
				true_block = fb.block<arc::DataType::VOID>("true_block")([&](arc::Builder &bb)
				{
					return bb.ret();
				});

				false_block = fb.block<arc::DataType::VOID>("false_block")([&](arc::Builder &bb)
				{
					return bb.ret();
				});

				[[maybe_unused]] auto *branch_true = fb.branch(fb.lit(true), true_block->parent->entry(),
				                                               false_block->parent->entry());
				[[maybe_unused]] auto *branch_false = fb.branch(fb.lit(false), true_block->parent->entry(),
				                                                false_block->parent->entry());
				return fb.ret();
			});

	pass_manager->run(*module);

	auto *func_region = get_function_region("test_branch_folding");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_nodes(func_region, arc::NodeType::BRANCH), 0);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::JUMP), 2);

	std::size_t jumps_to_true = 0;
	std::size_t jumps_to_false = 0;

	for (arc::Node *node: func_region->nodes())
	{
		if (node->ir_type == arc::NodeType::JUMP && !node->inputs.empty())
		{
			if (node->inputs[0] == true_block->parent->entry())
				jumps_to_true++;
			else if (node->inputs[0] == false_block->parent->entry())
				jumps_to_false++;
		}
	}

	EXPECT_EQ(jumps_to_true, 1);
	EXPECT_EQ(jumps_to_false, 1);
}

TEST_F(ConstFoldFixture, CastFolding)
{
	builder->function<arc::DataType::VOID>("test_cast_folding")
			.body([&](arc::Builder &fb)
			{
				[[maybe_unused]] auto *int_to_float = fb.cast<arc::DataType::FLOAT32>(fb.lit(42));
				[[maybe_unused]] auto *float_to_int = fb.cast<arc::DataType::INT32>(fb.lit(3.14f));
				[[maybe_unused]] auto *int_truncate = fb.cast<arc::DataType::INT8>(fb.lit(0x1234));
				[[maybe_unused]] auto *sign_extend = fb.cast<
					arc::DataType::INT32>(fb.lit(static_cast<std::int8_t>(-5)));
				return fb.ret();
			});

	pass_manager->run(*module);

	auto *func_region = get_function_region("test_cast_folding");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_nodes(func_region, arc::NodeType::CAST), 0);

	auto literal_count = count_nodes(func_region, arc::NodeType::LIT);
	EXPECT_GE(literal_count, 4);
}

TEST_F(ConstFoldFixture, VolatileNodesPreserved)
{
	arc::Node *volatile_add = nullptr;

	builder->function<arc::DataType::INT32>("test_volatile")
			.body([&](arc::Builder &fb)
			{
				auto *normal_add = fb.add(fb.lit(10), fb.lit(20));

				volatile_add = fb.add(fb.lit(30), fb.lit(40));
				volatile_add->traits |= arc::NodeTraits::VOLATILE;

				return fb.ret(normal_add);
			});

	pass_manager->run(*module);

	auto *func_region = get_function_region("test_volatile");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_nodes(func_region, arc::NodeType::ADD), 1);

	auto *ret = find_return(func_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::LIT);
	EXPECT_EQ(ret_value->value.get<arc::DataType::INT32>(), 30);
}

TEST_F(ConstFoldFixture, NestedConstantPropagation)
{
	builder->function<arc::DataType::INT32>("test_nested")
			.body([&](arc::Builder &fb)
			{
				auto *inner_add = fb.add(fb.lit(5), fb.lit(10));
				auto *outer_mul = fb.mul(inner_add, fb.lit(3));
				auto *final_sub = fb.sub(outer_mul, fb.lit(2));

				return fb.ret(final_sub);
			});

	pass_manager->run(*module);

	auto *func_region = get_function_region("test_nested");
	ASSERT_NE(func_region, nullptr);

	EXPECT_EQ(count_nodes(func_region, arc::NodeType::ADD), 0);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::MUL), 0);
	EXPECT_EQ(count_nodes(func_region, arc::NodeType::SUB), 0);

	auto *ret = find_return(func_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::LIT);
	EXPECT_EQ(ret_value->type_kind, arc::DataType::INT32);
	EXPECT_EQ(ret_value->value.get<arc::DataType::INT32>(), 43);
}

TEST_F(ConstFoldFixture, FromFolding)
{
	arc::Node *true_block = nullptr;
	arc::Node *false_block = nullptr;
	arc::Node *merge_block = nullptr;

	builder->function<arc::DataType::INT32>("test_from_folding")
			.body([&](arc::Builder &fb)
			{
				true_block = fb.block<arc::DataType::VOID>("true_block")([&](arc::Builder &bb)
				{
					return bb.ret();
				});

				false_block = fb.block<arc::DataType::VOID>("false_block")([&](arc::Builder &bb)
				{
					return bb.ret();
				});

				merge_block = fb.block<arc::DataType::INT32>("merge_block")([&](arc::Builder &bb)
				{
					auto *identical_from = bb.from({ bb.lit(42), bb.lit(42), bb.lit(42) });
					[[maybe_unused]] auto *different_from = bb.from({ bb.lit(10), bb.lit(20) });
					return bb.ret(identical_from);
				});

				fb.branch(fb.lit(true), true_block->parent->entry(), false_block->parent->entry());
				fb.jump(merge_block->parent->entry());
				return fb.ret();
			});

	arc::dump(*module.get());
	pass_manager->run(*module);

	arc::Region *merge_region = nullptr;
	for (arc::Region *child: module->root()->children())
	{
		if (child->name() == "test_from_folding")
		{
			for (arc::Region *grandchild: child->children())
			{
				if (grandchild->name() == "merge_block")
				{
					merge_region = grandchild;
					break;
				}
			}
			break;
		}
	}

	ASSERT_NE(merge_region, nullptr);
	EXPECT_EQ(count_nodes(merge_region, arc::NodeType::FROM), 1);

	auto *ret = find_return(merge_region);
	ASSERT_NE(ret, nullptr);
	ASSERT_FALSE(ret->inputs.empty());

	auto *ret_value = ret->inputs[0];
	EXPECT_EQ(ret_value->ir_type, arc::NodeType::LIT);
	EXPECT_EQ(ret_value->type_kind, arc::DataType::INT32);
	EXPECT_EQ(ret_value->value.get<arc::DataType::INT32>(), 42);
}
