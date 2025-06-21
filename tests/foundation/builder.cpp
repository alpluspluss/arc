/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <gtest/gtest.h>

class BuilderFixture : public testing::Test
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

TEST_F(BuilderFixture, CreateLiterals)
{
	auto *bool_lit = builder->lit(true);
	EXPECT_EQ(bool_lit->type_kind, arc::DataType::BOOL);
	EXPECT_EQ(bool_lit->ir_type, arc::NodeType::LIT);
	EXPECT_EQ(bool_lit->value.get<arc::DataType::BOOL>(), true);

	auto *int_lit = builder->lit(42);
	EXPECT_EQ(int_lit->type_kind, arc::DataType::INT32);
	EXPECT_EQ(int_lit->value.get<arc::DataType::INT32>(), 42);

	auto *float_lit = builder->lit(3.14f);
	EXPECT_EQ(float_lit->type_kind, arc::DataType::FLOAT32);
	EXPECT_EQ(float_lit->value.get<arc::DataType::FLOAT32>(), 3.14f);
}

TEST_F(BuilderFixture, CreateAllocation)
{
	auto *count = builder->lit(10);
	// ReSharper disable once CppDFAMemoryLeak
	auto *alloc_node = builder->alloc<arc::DataType::INT32>(count);

	EXPECT_EQ(alloc_node->type_kind, arc::DataType::INT32);
	EXPECT_EQ(alloc_node->ir_type, arc::NodeType::ALLOC);
	EXPECT_EQ(alloc_node->inputs.size(), 1);
	EXPECT_EQ(alloc_node->inputs[0], count);
}

TEST_F(BuilderFixture, ArithmeticOperations)
{
	auto *lhs = builder->lit(10);
	auto *rhs = builder->lit(20);

	auto *add_node = builder->add(lhs, rhs);
	EXPECT_EQ(add_node->ir_type, arc::NodeType::ADD);
	EXPECT_EQ(add_node->inputs.size(), 2);
	EXPECT_EQ(add_node->inputs[0], lhs);
	EXPECT_EQ(add_node->inputs[1], rhs);
	EXPECT_EQ(add_node->type_kind, arc::DataType::INT32);

	auto *sub_node = builder->sub(lhs, rhs);
	EXPECT_EQ(sub_node->ir_type, arc::NodeType::SUB);

	auto *mul_node = builder->mul(lhs, rhs);
	EXPECT_EQ(mul_node->ir_type, arc::NodeType::MUL);

	auto *div_node = builder->div(lhs, rhs);
	EXPECT_EQ(div_node->ir_type, arc::NodeType::DIV);
}

TEST_F(BuilderFixture, MemoryOperations)
{
	auto *count = builder->lit(1);
	auto *alloc_node = builder->alloc<arc::DataType::INT32>(count);
	auto *value = builder->lit(42);

	auto *store_node = builder->store(value, alloc_node);
	EXPECT_EQ(store_node->ir_type, arc::NodeType::STORE);
	EXPECT_EQ(store_node->inputs[0], value);
	EXPECT_EQ(store_node->inputs[1], alloc_node);

	auto *load_node = builder->load(alloc_node);
	EXPECT_EQ(load_node->ir_type, arc::NodeType::LOAD);
	EXPECT_EQ(load_node->inputs[0], alloc_node);
	EXPECT_EQ(load_node->type_kind, arc::DataType::INT32);
}

TEST_F(BuilderFixture, PointerOperations)
{
	auto *count = builder->lit(1);
	auto *alloc_node = builder->alloc<arc::DataType::INT32>(count);
	auto *addr = builder->addr_of(alloc_node);

	EXPECT_EQ(addr->ir_type, arc::NodeType::ADDR_OF);
	EXPECT_EQ(addr->type_kind, arc::DataType::POINTER);

	auto &ptr_data = addr->value.get<arc::DataType::POINTER>();
	EXPECT_EQ(ptr_data.pointee, alloc_node);
	EXPECT_EQ(ptr_data.addr_space, 0);

	auto *value = builder->lit(123);
	auto *ptr_store = builder->ptr_store(value, addr);
	EXPECT_EQ(ptr_store->ir_type, arc::NodeType::PTR_STORE);
	EXPECT_EQ(ptr_store->inputs[0], value);
	EXPECT_EQ(ptr_store->inputs[1], addr);

	auto *ptr_load = builder->ptr_load(addr);
	EXPECT_EQ(ptr_load->ir_type, arc::NodeType::PTR_LOAD);
	EXPECT_EQ(ptr_load->type_kind, arc::DataType::INT32);
}

TEST_F(BuilderFixture, PointerArithmetic)
{
	auto *count = builder->lit(4);
	auto *alloc_node = builder->alloc<arc::DataType::INT32>(count);
	auto *base_ptr = builder->addr_of(alloc_node);
	auto *offset = builder->lit(4);

	auto *new_ptr = builder->ptr_add(base_ptr, offset);
	EXPECT_EQ(new_ptr->ir_type, arc::NodeType::PTR_ADD);
	EXPECT_EQ(new_ptr->type_kind, arc::DataType::POINTER);
	EXPECT_EQ(new_ptr->inputs[0], base_ptr);
	EXPECT_EQ(new_ptr->inputs[1], offset);
}

TEST_F(BuilderFixture, VectorOperations)
{
	auto *elem1 = builder->lit(1.0f);
	auto *elem2 = builder->lit(2.0f);
	auto *elem3 = builder->lit(3.0f);
	auto *elem4 = builder->lit(4.0f);

	auto *vec = builder->vector_build({ elem1, elem2, elem3, elem4 });
	EXPECT_EQ(vec->ir_type, arc::NodeType::VECTOR_BUILD);
	EXPECT_EQ(vec->type_kind, arc::DataType::VECTOR);
	EXPECT_EQ(vec->inputs.size(), 4);

	auto &vec_data = vec->value.get<arc::DataType::VECTOR>();
	EXPECT_EQ(vec_data.elem_type, arc::DataType::FLOAT32);
	EXPECT_EQ(vec_data.lane_count, 4);

	auto *scalar = builder->lit(5.0f);
	auto *splat_vec = builder->vector_splat(scalar, 8);
	EXPECT_EQ(splat_vec->ir_type, arc::NodeType::VECTOR_SPLAT);

	auto &splat_data = splat_vec->value.get<arc::DataType::VECTOR>();
	EXPECT_EQ(splat_data.elem_type, arc::DataType::FLOAT32);
	EXPECT_EQ(splat_data.lane_count, 8);

	auto *extract = builder->vector_extract(vec, 2);
	EXPECT_EQ(extract->ir_type, arc::NodeType::VECTOR_EXTRACT);
	EXPECT_EQ(extract->type_kind, arc::DataType::FLOAT32);
}

TEST_F(BuilderFixture, FunctionBuilder)
{
	auto *func_node = builder->function<arc::DataType::INT32>("test_func")
			.param<arc::DataType::INT32>("x")
			.param<arc::DataType::INT32>("y")
			.exported()
			.driver()
			.body([](arc::Builder &fb, arc::Node *x, arc::Node *y)
			{
				auto *sum = fb.add(x, y);
				return fb.ret(sum);
			});

	EXPECT_EQ(func_node->ir_type, arc::NodeType::FUNCTION);
	EXPECT_TRUE((func_node->traits & arc::NodeTraits::EXPORT) != arc::NodeTraits::NONE);
	EXPECT_TRUE((func_node->traits & arc::NodeTraits::DRIVER) != arc::NodeTraits::NONE);
}

TEST_F(BuilderFixture, FunctionPointerParameter)
{
	auto *pointee = builder->alloc<arc::DataType::INT32>(builder->lit(1));

	auto *func_node = builder->function<arc::DataType::VOID>("ptr_func")
			.param_ptr<arc::DataType::INT32>("ptr", pointee)
			.body([](arc::Builder &fb, arc::Node *ptr)
			{
				auto *value = fb.lit(42);
				fb.ptr_store(value, ptr);
				return fb.ret();
			});

	EXPECT_EQ(func_node->ir_type, arc::NodeType::FUNCTION);
}

TEST_F(BuilderFixture, BlockBuilder)
{
	auto block_result = builder->block<arc::DataType::INT32>("test_block")([&](arc::Builder &fb)
	{
		auto *a = fb.lit(10);
		auto *b = fb.lit(20);
		return fb.add(a, b);
	});

	EXPECT_EQ(block_result->ir_type, arc::NodeType::ADD);
	EXPECT_EQ(block_result->type_kind, arc::DataType::INT32);
}

TEST_F(BuilderFixture, FluentStoreOperations)
{
	auto *count = builder->lit(4);
	auto *array = builder->alloc<arc::DataType::INT32>(count);
	auto *value1 = builder->lit(42);
	auto *value2 = builder->lit(84);
	auto *offset = builder->lit(4);

	auto *store1 = builder->store(value1).to(array);
	EXPECT_EQ(store1->ir_type, arc::NodeType::STORE);

	auto *store2 = builder->store(value2).to(array, offset);
	EXPECT_EQ(store2->ir_type, arc::NodeType::PTR_STORE);

	auto *ptr = builder->addr_of(array);
	auto *store3 = builder->store(value1).through(ptr);
	EXPECT_EQ(store3->ir_type, arc::NodeType::PTR_STORE);
}

TEST_F(BuilderFixture, TypeInference)
{
	auto *int8_val = builder->lit(static_cast<std::int8_t>(10));
	auto *uint16_val = builder->lit(static_cast<std::uint16_t>(20));

	auto *result = builder->add(int8_val, uint16_val);
	EXPECT_EQ(result->type_kind, arc::DataType::INT32);
	EXPECT_EQ(int8_val->type_kind, arc::DataType::INT32);
	EXPECT_EQ(uint16_val->type_kind, arc::DataType::INT32);
}

TEST_F(BuilderFixture, ErrorHandling)
{
	EXPECT_THROW(builder->load(nullptr), std::invalid_argument);
	EXPECT_THROW(builder->ptr_load(nullptr), std::invalid_argument);
	EXPECT_THROW(builder->add(nullptr, nullptr), std::invalid_argument);

	auto *int_val = builder->lit(42);
	EXPECT_THROW(builder->ptr_load(int_val), std::invalid_argument);

	EXPECT_THROW(builder->vector_build({}), std::invalid_argument);
	EXPECT_THROW(builder->vector_splat(nullptr, 4), std::invalid_argument);
	EXPECT_THROW(builder->vector_splat(int_val, 0), std::invalid_argument);
}

TEST_F(BuilderFixture, AtomicOperations)
{
	auto *count = builder->lit(1);
	auto *alloc_node = builder->alloc<arc::DataType::INT32>(count);
	auto *addr = builder->addr_of(alloc_node);
	auto *value = builder->lit(42);

	auto *atomic_store1 = builder->store(value).to_atomic(alloc_node);
	EXPECT_EQ(atomic_store1->ir_type, arc::NodeType::ATOMIC_STORE);

	auto *atomic_store2 = builder->store(value).through_atomic(addr);
	EXPECT_EQ(atomic_store2->ir_type, arc::NodeType::ATOMIC_STORE);
}

TEST_F(BuilderFixture, InvokeOperation)
{
	auto func_builder = builder->function<arc::DataType::INT32>("risky_func");
	auto *func_node = func_builder.body([&](arc::Builder &fb)
	{
		return fb.ret(fb.lit(42));
	});

	auto normal_block = builder->block<arc::DataType::VOID>("normal");
	auto except_block = builder->block<arc::DataType::VOID>("except");

	auto *arg1 = builder->lit(10);
	auto *arg2 = builder->lit(20);
	auto *invoke_node = builder->invoke(func_node, { arg1, arg2 },
	                                    normal_block.entry(),
	                                    except_block.entry());

	EXPECT_EQ(invoke_node->ir_type, arc::NodeType::INVOKE);
	EXPECT_EQ(invoke_node->inputs[0], func_node);
	EXPECT_EQ(invoke_node->inputs[1], normal_block.entry());
	EXPECT_EQ(invoke_node->inputs[2], except_block.entry());
	EXPECT_EQ(invoke_node->inputs[3], arg1);
	EXPECT_EQ(invoke_node->inputs[4], arg2);
}

TEST_F(BuilderFixture, TypeCasts)
{
	auto *int_val = builder->lit(42);
	auto *float_cast = builder->cast<arc::DataType::FLOAT32>(int_val);

	EXPECT_EQ(float_cast->ir_type, arc::NodeType::CAST);
	EXPECT_EQ(float_cast->type_kind, arc::DataType::FLOAT32);
	EXPECT_EQ(float_cast->inputs[0], int_val);
}
