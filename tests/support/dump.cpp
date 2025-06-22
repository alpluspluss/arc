/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/support/dump.hpp>
#include <gtest/gtest.h>

class DumpFixture : public testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("test_module");
		builder = std::make_unique<arc::Builder>(*module);
	}

	void TearDown() override
	{
		builder.reset();
		module.reset();
	}

	std::unique_ptr<arc::Module> module;
	std::unique_ptr<arc::Builder> builder;
};

TEST_F(DumpFixture, BasicLiterals)
{
	builder->function<arc::DataType::VOID>("literals")
			.body([](arc::Builder &fb)
			{
				[[maybe_unused]] auto *bool_lit = fb.lit(true);
				[[maybe_unused]] auto *int8_lit = fb.lit(static_cast<std::int8_t>(-42));
				[[maybe_unused]] auto *int16_lit = fb.lit(static_cast<std::int16_t>(1000));
				[[maybe_unused]] auto *int32_lit = fb.lit(42);
				[[maybe_unused]] auto *int64_lit = fb.lit(static_cast<std::int64_t>(9223372036854775807));
				[[maybe_unused]] auto *uint8_lit = fb.lit(static_cast<std::uint8_t>(255));
				[[maybe_unused]] auto *uint16_lit = fb.lit(static_cast<std::uint16_t>(65535));
				[[maybe_unused]] auto *uint32_lit = fb.lit(static_cast<std::uint32_t>(4294967295));
				[[maybe_unused]] auto *uint64_lit = fb.lit(static_cast<std::uint64_t>(18446744073709551615ULL));
				[[maybe_unused]] auto *float32_lit = fb.lit(3.14f);
				[[maybe_unused]] auto *float64_lit = fb.lit(2.718281828459045);

				return fb.ret();
			});

	dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, ArithmeticOperations)
{
	builder->function<arc::DataType::INT32>("arithmetic")
			.param<arc::DataType::INT32>("x")
			.param<arc::DataType::INT32>("y")
			.body([](arc::Builder &fb, arc::Node *x, arc::Node *y)
			{
				auto *sum = fb.add(x, y);
				[[maybe_unused]] auto *diff = fb.sub(x, y);
				[[maybe_unused]] auto *prod = fb.mul(x, y);
				[[maybe_unused]] auto *quot = fb.div(x, y);
				[[maybe_unused]] auto *rem = fb.mod(x, y);

				[[maybe_unused]] auto *band_result = fb.band(x, y);
				[[maybe_unused]] auto *bor_result = fb.bor(x, y);
				[[maybe_unused]] auto *bxor_result = fb.bxor(x, y);
				[[maybe_unused]] auto *bnot_result = fb.bnot(x);
				[[maybe_unused]] auto *bshl_result = fb.bshl(x, fb.lit(2));
				[[maybe_unused]] auto *bshr_result = fb.bshr(x, fb.lit(2));

				return fb.ret(sum);
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, ComparisonOperations)
{
	builder->function<arc::DataType::BOOL>("comparisons")
			.param<arc::DataType::INT32>("a")
			.param<arc::DataType::INT32>("b")
			.body([](arc::Builder &fb, arc::Node *a, arc::Node *b)
			{
				auto *eq_result = fb.eq(a, b);
				[[maybe_unused]] auto *neq_result = fb.neq(a, b);
				[[maybe_unused]] auto *lt_result = fb.lt(a, b);
				[[maybe_unused]] auto *lte_result = fb.lte(a, b);
				[[maybe_unused]] auto *gt_result = fb.gt(a, b);
				[[maybe_unused]] auto *gte_result = fb.gte(a, b);

				return fb.ret(eq_result);
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, MemoryOperations)
{
	builder->function<arc::DataType::VOID>("memory_ops")
			.body([](arc::Builder &fb)
			{
				auto *count = fb.lit(4);
				auto *alloc_node = fb.alloc<arc::DataType::INT32>(count);

				auto *value = fb.lit(42);
				[[maybe_unused]] auto *store_op = fb.store(value, alloc_node);
				[[maybe_unused]] auto *load_op = fb.load(alloc_node);

				auto *addr = fb.addr_of(alloc_node);
				auto *offset = fb.lit(4);
				[[maybe_unused]] auto *ptr_add_result = fb.ptr_add(addr, offset);
				[[maybe_unused]] auto *ptr_store_op = fb.ptr_store(value, addr);
				[[maybe_unused]] auto *ptr_load_op = fb.ptr_load(addr);

				return fb.ret();
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, VectorOperations)
{
	builder->function<arc::DataType::VOID>("vector_ops")
			.body([](arc::Builder &fb)
			{
				auto *elem1 = fb.lit(1.0f);
				auto *elem2 = fb.lit(2.0f);
				auto *elem3 = fb.lit(3.0f);
				auto *elem4 = fb.lit(4.0f);

				auto *vec = fb.vector_build({ elem1, elem2, elem3, elem4 });

				auto *scalar = fb.lit(5.0f);
				[[maybe_unused]] auto *splat_vec = fb.vector_splat(scalar, 8);

				fb.vector_extract(vec, 2);
				return fb.ret();
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, ControlFlow)
{
	builder->function<arc::DataType::INT32>("control_flow")
			.param<arc::DataType::BOOL>("condition")
			.body([](arc::Builder &fb, arc::Node *condition)
			{
				const auto true_block = fb.block<arc::DataType::VOID>("true_path");
				const auto false_block = fb.block<arc::DataType::VOID>("false_path");
				const auto merge_block = fb.block<arc::DataType::VOID>("merge");
				fb.branch(condition, true_block.entry(), false_block.entry());
				fb.jump(merge_block.entry());
				return fb.ret(fb.lit(0));
			});

	dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, FunctionCalls)
{
	auto *helper = builder->function<arc::DataType::INT32>("helper")
			.param<arc::DataType::INT32>("x")
			.param<arc::DataType::INT32>("y")
			.body([](arc::Builder &fb, arc::Node *x, arc::Node *y)
			{
				auto *result = fb.add(x, y);
				return fb.ret(result);
			});

	builder->function<arc::DataType::INT32>("caller")
			.body([helper](arc::Builder &fb)
			{
				auto *arg1 = fb.lit(10);
				auto *arg2 = fb.lit(20);
				auto *call_result = fb.call(helper, { arg1, arg2 });

				return fb.ret(call_result);
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, InvokeOperations)
{
	auto *risky_func = builder->function<arc::DataType::INT32>("risky")
			.param<arc::DataType::INT32>("x")
			.body([](arc::Builder &fb, arc::Node *x)
			{
				return fb.ret(x);
			});

	builder->function<arc::DataType::INT32>("invoke_test")
			.body([risky_func](arc::Builder &fb)
			{
				auto normal_block = fb.block<arc::DataType::VOID>("normal");
				auto except_block = fb.block<arc::DataType::VOID>("exception");

				auto *arg = fb.lit(42);
				auto *invoke_result = fb.invoke(risky_func, { arg }, normal_block.entry(), except_block.entry());

				return fb.ret(invoke_result);
			});

	dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, TypeCasts)
{
	builder->function<arc::DataType::VOID>("casts")
			.body([](arc::Builder &fb)
			{
				auto *int_val = fb.lit(42);
				[[maybe_unused]] auto *float_cast = fb.cast<arc::DataType::FLOAT32>(int_val);
				[[maybe_unused]] auto *int64_cast = fb.cast<arc::DataType::INT64>(int_val);
				[[maybe_unused]] auto *uint32_cast = fb.cast<arc::DataType::UINT32>(int_val);

				return fb.ret();
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, ComplexTypes)
{
	auto *int_alloc = builder->alloc<arc::DataType::INT32>(builder->lit(1));

	builder->function<arc::DataType::VOID>("complex_types")
			.param_ptr<arc::DataType::INT32>("int_ptr", int_alloc)
			.body([](arc::Builder &fb, arc::Node *)
			{
				auto *array_alloc = fb.alloc<arc::DataType::INT32>(fb.lit(10));
				auto *array_addr = fb.addr_of(array_alloc);

				auto *offset = fb.lit(4);
				fb.ptr_add(array_addr, offset);
				return fb.ret();
			});

	dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, NodeTraits)
{
	builder->function<arc::DataType::VOID>("exported_func")
			.exported()
			.body([](arc::Builder &fb)
			{
				return fb.ret();
			});

	builder->function<arc::DataType::INT32>("main")
			.driver()
			.body([](arc::Builder &fb)
			{
				return fb.ret(fb.lit(0));
			});

	builder->function<arc::DataType::VOID>("external_func")
			.imported()
			.body([](arc::Builder &fb)
			{
				return fb.ret();
			});

	builder->function<arc::DataType::VOID>("volatile_func")
			.keep()
			.body([](arc::Builder &fb)
			{
				return fb.ret();
			});

	dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, NestedRegions)
{
	builder->function<arc::DataType::INT32>("nested_regions")
			.param<arc::DataType::BOOL>("condition")
			.body([](arc::Builder &fb, arc::Node *condition)
			{
				auto outer_block = fb.block<arc::DataType::VOID>("outer");
				auto inner_block = fb.block<arc::DataType::VOID>("inner");
				auto end_block = fb.block<arc::DataType::VOID>("end");

				auto *value = fb.lit(42);
				fb.branch(condition, outer_block.entry(), end_block.entry());
				fb.jump(inner_block.entry());
				fb.jump(end_block.entry());
				return fb.ret(value);
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, FluentStoreOperations)
{
	builder->function<arc::DataType::VOID>("fluent_stores")
			.body([](arc::Builder &fb)
			{
				auto *alloc_node = fb.alloc<arc::DataType::INT32>(fb.lit(1));
				auto *value1 = fb.lit(42);
				auto *value2 = fb.lit(84);
				auto *addr = fb.addr_of(alloc_node);

				[[maybe_unused]] auto *store1 = fb.store(value1).to(alloc_node);
				[[maybe_unused]] auto *store2 = fb.store(value2).through(addr);
				[[maybe_unused]] auto *atomic_store1 = fb.store(value1).to_atomic(alloc_node);
				[[maybe_unused]] auto *atomic_store2 = fb.store(value2).through_atomic(
					addr, arc::AtomicOrdering::RELAXED);
				return fb.ret();
			});

	arc::dump(*module);
	std::cout << "\n";
}

TEST_F(DumpFixture, EdgeCases)
{
	builder->function<arc::DataType::VOID>("empty_func")
		.body([](arc::Builder &fb)
		{
			return fb.ret();
		});

	builder->function<arc::DataType::INT32>("many_params")
		.param<arc::DataType::INT32>("a")
		.param<arc::DataType::INT32>("b")
		.param<arc::DataType::INT32>("c")
		.param<arc::DataType::INT32>("d")
		.param<arc::DataType::INT32>("e")
		.body([](arc::Builder &fb, arc::Node* a, arc::Node* b, arc::Node* c, arc::Node* d, arc::Node* e)
		{
			auto* result = fb.add(a, fb.add(b, fb.add(c, fb.add(d, e))));
			return fb.ret(result);
		});

	arc::dump(*module);
	std::cout << "\n";
}
