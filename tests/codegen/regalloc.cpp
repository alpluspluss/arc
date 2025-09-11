/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/codegen/regalloc.hpp>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/region.hpp>
#include <gtest/gtest.h>

struct MockTarget
{
	using register_type = std::uint32_t;

	struct MockInstruction
	{
		enum class Opcode { NOP, ADD, MOV, LOAD, STORE };

		static constexpr std::size_t max_operands()
		{
			return 3;
		}

		static constexpr std::size_t encoding_size()
		{
			return 4;
		}
	};

	using instruction_type = MockInstruction;

	static constexpr arc::TargetArch target_arch()
	{
		return arc::TargetArch::AARCH64;
	}

	[[nodiscard]] std::uint32_t count(arc::RegisterClass cls) const
	{
		switch (cls)
		{
			case arc::RegisterClass::GENERAL_PURPOSE:
				return 16;
			case arc::RegisterClass::VECTOR:
				return 8;
			case arc::RegisterClass::PREDICATE:
				return 0;
		}
		return 0;
	}

	std::vector<register_type> caller_saved(arc::RegisterClass cls) const
	{
		switch (cls)
		{
			case arc::RegisterClass::GENERAL_PURPOSE:
				return { 0, 1, 2, 3, 4, 5, 6, 7 };
			case arc::RegisterClass::VECTOR:
				return { 0, 1, 2, 3 };
			default:
				return {};
		}
	}

	[[nodiscard]] std::vector<register_type> callee_saved(arc::RegisterClass cls) const
	{
		switch (cls)
		{
			case arc::RegisterClass::GENERAL_PURPOSE:
				return { 8, 9, 10, 11, 12, 13, 14, 15 };
			case arc::RegisterClass::VECTOR:
				return { 4, 5, 6, 7 };
			default:
				return {};
		}
	}

	[[nodiscard]] std::uint32_t spill_cost(register_type reg) const
	{
		return reg >= 8 ? 100 : 10;
	}

	[[nodiscard]] bool uses_vector_for_float() const
	{
		return true;
	}
};

class RegisterAllocatorFixture : public ::testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("test_module");
		region = module->create_region("test_region");
		dag = std::make_unique<arc::SelectionDAG<MockTarget::instruction_type> >(region);
		target = std::make_unique<MockTarget>();
		allocator = std::make_unique<arc::RegisterAllocator<MockTarget> >(*target, *dag);
	}

	arc::Budget<MockTarget> create_test_budget()
	{
		arc::Budget<MockTarget> budget;
		auto gp_caller = target->caller_saved(arc::RegisterClass::GENERAL_PURPOSE);
		auto gp_callee = target->callee_saved(arc::RegisterClass::GENERAL_PURPOSE);
		auto vec_caller = target->caller_saved(arc::RegisterClass::VECTOR);
		auto vec_callee = target->callee_saved(arc::RegisterClass::VECTOR);

		for (auto reg: gp_caller)
		{
			budget.available[arc::RegisterClass::GENERAL_PURPOSE].insert(reg);
		}
		for (auto reg: gp_callee)
		{
			budget.available[arc::RegisterClass::GENERAL_PURPOSE].insert(reg);
		}
		for (auto reg: vec_caller)
		{
			budget.available[arc::RegisterClass::VECTOR].insert(reg);
		}
		for (auto reg: vec_callee)
		{
			budget.available[arc::RegisterClass::VECTOR].insert(reg);
		}

		return budget;
	}

	std::unique_ptr<arc::Module> module;
	arc::Region *region = nullptr;
	std::unique_ptr<arc::SelectionDAG<MockTarget::instruction_type> > dag;
	std::unique_ptr<MockTarget> target;
	std::unique_ptr<arc::RegisterAllocator<MockTarget> > allocator;
};

TEST_F(RegisterAllocatorFixture, BasicBudgetCreation)
{
	arc::Budget<MockTarget> budget;
	budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 0, 1, 2, 3 };
	budget.available[arc::RegisterClass::VECTOR] = { 0, 1 };

	EXPECT_EQ(budget.available[arc::RegisterClass::GENERAL_PURPOSE].size(), 4);
	EXPECT_EQ(budget.available[arc::RegisterClass::VECTOR].size(), 2);
}

TEST_F(RegisterAllocatorFixture, ConstraintsSpillDetection)
{
	arc::Constraints<MockTarget> constraints;
	constraints.min_required[arc::RegisterClass::GENERAL_PURPOSE] = 8;
	constraints.min_required[arc::RegisterClass::VECTOR] = 4;

	std::unordered_map<arc::RegisterClass, std::uint32_t> available;
	available[arc::RegisterClass::GENERAL_PURPOSE] = 4;
	available[arc::RegisterClass::VECTOR] = 2;

	EXPECT_TRUE(constraints.needs_spill(available));

	available[arc::RegisterClass::GENERAL_PURPOSE] = 8;
	available[arc::RegisterClass::VECTOR] = 4;

	EXPECT_FALSE(constraints.needs_spill(available));
}

TEST_F(RegisterAllocatorFixture, SimpleNodeAllocation)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *lit_node = builder.lit(42);
	dag->build();
	dag->linearize();

	auto *dag_node = dag->find(lit_node);
	ASSERT_NE(dag_node, nullptr);

	auto budget = create_test_budget();
	allocator->allocate(region, budget);

	arc::Request<MockTarget> req;
	req.cls = arc::RegisterClass::GENERAL_PURPOSE;

	auto result = allocator->allocate_node(dag_node, req);
	EXPECT_TRUE(result.allocated());
	EXPECT_FALSE(result.spilled);
}

TEST_F(RegisterAllocatorFixture, RegisterHintRespected)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *lit_node = builder.lit(42);
	dag->build();
	dag->linearize();

	auto *dag_node = dag->find(lit_node);
	ASSERT_NE(dag_node, nullptr);

	arc::Budget<MockTarget> budget;
	budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 5, 6, 7, 8 };
	allocator->allocate(region, budget);

	arc::Request<MockTarget> req;
	req.cls = arc::RegisterClass::GENERAL_PURPOSE;
	req.hint = 7;

	auto result = allocator->allocate_node(dag_node, req);
	EXPECT_TRUE(result.allocated());
	EXPECT_EQ(*result.reg, 7);
}

TEST_F(RegisterAllocatorFixture, ForbiddenRegisterAvoided)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *lit_node = builder.lit(42);
	dag->build();
	dag->linearize();

	auto *dag_node = dag->find(lit_node);
	ASSERT_NE(dag_node, nullptr);

	arc::Budget<MockTarget> budget;
	budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 5, 6, 7 };
	allocator->allocate(region, budget);

	arc::Request<MockTarget> req;
	req.cls = arc::RegisterClass::GENERAL_PURPOSE;
	req.forbidden = { 5, 7 };

	auto result = allocator->allocate_node(dag_node, req);
	EXPECT_TRUE(result.allocated());
	EXPECT_EQ(*result.reg, 6);
}

TEST_F(RegisterAllocatorFixture, RegisterRelease)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *lit_node = builder.lit(42);
	dag->build();
	dag->linearize();

	auto *dag_node = dag->find(lit_node);
	ASSERT_NE(dag_node, nullptr);

	arc::Budget<MockTarget> budget;
	budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 5 };
	allocator->allocate(region, budget);

	arc::Request<MockTarget> req;
	req.cls = arc::RegisterClass::GENERAL_PURPOSE;

	auto result = allocator->allocate_node(dag_node, req);
	EXPECT_TRUE(result.allocated());
	EXPECT_EQ(*result.reg, 5);

	EXPECT_FALSE(allocator->available(region, arc::RegisterClass::GENERAL_PURPOSE, 5));

	allocator->release(dag_node);

	EXPECT_TRUE(allocator->available(region, arc::RegisterClass::GENERAL_PURPOSE, 5));
}

TEST_F(RegisterAllocatorFixture, ForceSpill)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *lit_node = builder.lit(42);
	dag->build();
	dag->linearize();

	auto *dag_node = dag->find(lit_node);
	ASSERT_NE(dag_node, nullptr);

	auto result = allocator->force_spill(dag_node);
	EXPECT_FALSE(result.allocated());
	EXPECT_TRUE(result.spilled);
}

TEST_F(RegisterAllocatorFixture, PressureTracking)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *lit1 = builder.lit(10);
	auto *lit2 = builder.lit(20);
	dag->build();
	dag->linearize();

	auto *dag1 = dag->find(lit1);
	auto *dag2 = dag->find(lit2);
	ASSERT_NE(dag1, nullptr);
	ASSERT_NE(dag2, nullptr);

	arc::Budget<MockTarget> budget;
	budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 0, 1, 2, 3 };
	allocator->allocate(region, budget);

	EXPECT_EQ(allocator->pressure(region, arc::RegisterClass::GENERAL_PURPOSE), 0);

	arc::Request<MockTarget> req;
	req.cls = arc::RegisterClass::GENERAL_PURPOSE;

	allocator->allocate_node(dag1, req);
	EXPECT_EQ(allocator->pressure(region, arc::RegisterClass::GENERAL_PURPOSE), 1);

	allocator->allocate_node(dag2, req);
	EXPECT_EQ(allocator->pressure(region, arc::RegisterClass::GENERAL_PURPOSE), 2);
}

TEST_F(RegisterAllocatorFixture, FROMNodeOptimization)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *value1 = builder.lit(10);
	auto *from_node = builder.create_node(arc::NodeType::FROM, arc::DataType::INT32);
	from_node->inputs = { value1 };

	dag->build();
	dag->linearize();

	auto *dag_value1 = dag->find(value1);
	auto *dag_from = dag->find(from_node);
	ASSERT_NE(dag_value1, nullptr);
	ASSERT_NE(dag_from, nullptr);

	arc::Budget<MockTarget> budget;
	budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 5, 6, 7 };
	allocator->allocate(region, budget);

	arc::Request<MockTarget> req;
	req.cls = arc::RegisterClass::GENERAL_PURPOSE;

	auto value1_result = allocator->allocate_node(dag_value1, req);
	EXPECT_TRUE(value1_result.allocated());

	auto from_result = allocator->allocate_node(dag_from, req);
	EXPECT_TRUE(from_result.allocated());
}

TEST_F(RegisterAllocatorFixture, HierarchicalAllocation)
{
	auto *parent_region = module->create_region("parent");

	arc::Budget<MockTarget> total_budget = create_test_budget();
	auto parent_budget = allocator->allocate(parent_region, total_budget);

	EXPECT_GT(parent_budget.available[arc::RegisterClass::GENERAL_PURPOSE].size(), 0);
}

TEST_F(RegisterAllocatorFixture, RegisterClassInference)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *int_node = builder.lit(42);
	auto *float_node = builder.lit(3.14f);
	dag->build();
	dag->linearize();

	auto *dag_int = dag->find(int_node);
	auto *dag_float = dag->find(float_node);

	arc::Budget<MockTarget> budget;
	budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 0, 1 };
	budget.available[arc::RegisterClass::VECTOR] = { 0, 1 };
	allocator->allocate(region, budget);

	arc::Request<MockTarget> int_req;
	int_req.cls = arc::RegisterClass::GENERAL_PURPOSE;

	arc::Request<MockTarget> float_req;
	float_req.cls = arc::RegisterClass::VECTOR;

	auto int_result = allocator->allocate_node(dag_int, int_req);
	auto float_result = allocator->allocate_node(dag_float, float_req);

	EXPECT_TRUE(int_result.allocated());
	EXPECT_TRUE(float_result.allocated());
}

TEST_F(RegisterAllocatorFixture, SpillWhenNoRegistersAvailable)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *lit_node = builder.lit(42);
	dag->build();
	dag->linearize();

	auto *dag_node = dag->find(lit_node);
	ASSERT_NE(dag_node, nullptr);

	arc::Budget<MockTarget> budget = {};
	allocator->allocate(region, budget);

	arc::Request<MockTarget> req;
	req.cls = arc::RegisterClass::GENERAL_PURPOSE;
	req.allow_spill = true;

	auto result = allocator->allocate_node(dag_node, req);
	EXPECT_FALSE(result.allocated());
	EXPECT_TRUE(result.spilled);
}

TEST_F(RegisterAllocatorFixture, NoSpillWhenDisallowed)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *lit_node = builder.lit(42);
	dag->build();
	dag->linearize();

	auto *dag_node = dag->find(lit_node);
	ASSERT_NE(dag_node, nullptr);

	arc::Budget<MockTarget> budget;
	allocator->allocate(region, budget);

	arc::Request<MockTarget> req;
	req.cls = arc::RegisterClass::GENERAL_PURPOSE;
	req.allow_spill = false;

	auto result = allocator->allocate_node(dag_node, req);
	EXPECT_FALSE(result.allocated());
	EXPECT_FALSE(result.spilled);
}

TEST_F(RegisterAllocatorFixture, AllocationCaching)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *lit_node = builder.lit(42);
	dag->build();
	dag->linearize();

	auto *dag_node = dag->find(lit_node);
	ASSERT_NE(dag_node, nullptr);

	arc::Budget<MockTarget> budget;
	budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 5 };
	allocator->allocate(region, budget);

	arc::Request<MockTarget> req;
	req.cls = arc::RegisterClass::GENERAL_PURPOSE;

	auto result1 = allocator->allocate_node(dag_node, req);
	auto result2 = allocator->allocate_node(dag_node, req);

	EXPECT_TRUE(result1.allocated());
	EXPECT_TRUE(result2.allocated());
	EXPECT_EQ(*result1.reg, *result2.reg);
}

TEST_F(RegisterAllocatorFixture, ComplexArithmeticChain)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *a = builder.lit(10);
	auto *b = builder.lit(20);
	auto *c = builder.lit(30);
	auto *add1 = builder.add(a, b);
	auto *add2 = builder.add(add1, c);

	dag->build();
	dag->linearize();

	auto budget = create_test_budget();
	auto final_budget = allocator->allocate(region, budget);

	EXPECT_GE(final_budget.allocated[arc::RegisterClass::GENERAL_PURPOSE], 0);
}

TEST_F(RegisterAllocatorFixture, InterferenceTracking)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *val1 = builder.lit(10);
	auto *val2 = builder.lit(20);
	auto *add_result = builder.add(val1, val2);
	auto *mul_result = builder.mul(val1, val2);
	auto *final_use = builder.add(add_result, mul_result);

	dag->build();
	dag->linearize();

	arc::Budget<MockTarget> budget;
	budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 0, 1 };
	allocator->allocate(region, budget);

	auto pressure = allocator->pressure(region, arc::RegisterClass::GENERAL_PURPOSE);
	EXPECT_LE(pressure, 2);

	auto *dag_add = dag->find(add_result);
	auto *dag_mul = dag->find(mul_result);
	auto *dag_final = dag->find(final_use);

	int successful_allocations = 0;
	for (auto* dag_node : {dag_add, dag_mul, dag_final})
	{
		auto result = allocator->get(dag_node);
		if (result.allocated()) successful_allocations++;
	}
	EXPECT_GT(successful_allocations, 0);
}

TEST_F(RegisterAllocatorFixture, OptimalRegisterReuse)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    std::vector<arc::Node*> operations;
    auto *base = builder.lit(1);

    for (int i = 0; i < 5; ++i) {
        auto *val = builder.lit(i + 10);
        auto *op = builder.add(base, val);
        operations.push_back(op);
        base = op;
    }

    dag->build();
    dag->linearize();

    arc::Budget<MockTarget> limited_budget;
    limited_budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 0, 1 };
    allocator->allocate(region, limited_budget);

    auto peak_pressure = allocator->pressure(region, arc::RegisterClass::GENERAL_PURPOSE);
    EXPECT_EQ(peak_pressure, 2);

    auto* final_dag = dag->find(operations.back());
    auto final_result = allocator->get(final_dag);
    EXPECT_TRUE(final_result.allocated());
}

TEST_F(RegisterAllocatorFixture, RegisterClassSeparation)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto *int_val1 = builder.lit(42);
    auto *int_val2 = builder.lit(24);
    auto *float_val1 = builder.lit(3.14f);
    auto *float_val2 = builder.lit(2.71f);

    auto *int_result = builder.add(int_val1, int_val2);
    auto *float_result = builder.add(float_val1, float_val2);

    auto *mixed_use = builder.cast<arc::DataType::FLOAT32>(int_result);
    auto *final_result = builder.add(mixed_use, float_result);

    dag->build();
    dag->linearize();

    arc::Budget<MockTarget> budget;
    budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 0, 1 };
    budget.available[arc::RegisterClass::VECTOR] = { 0, 1 };
    allocator->allocate(region, budget);

    auto gp_pressure = allocator->pressure(region, arc::RegisterClass::GENERAL_PURPOSE);
    auto vec_pressure = allocator->pressure(region, arc::RegisterClass::VECTOR);

    EXPECT_GT(gp_pressure + vec_pressure, 0);

    auto *dag_final = dag->find(final_result);
    auto final_allocation = allocator->get(dag_final);
    EXPECT_TRUE(final_allocation.allocated() || final_allocation.spilled);
}

TEST_F(RegisterAllocatorFixture, ForceSpillScenario)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto *val1 = builder.lit(1);
    auto *val2 = builder.lit(2);
    auto *val3 = builder.lit(3);
    auto *val4 = builder.lit(4);

    auto *op1 = builder.add(val1, val2);
    auto *op2 = builder.add(val2, val3);
    auto *op3 = builder.add(val3, val4);

    auto *combine1 = builder.add(op1, op2);
    auto *final_result = builder.add(combine1, op3);

    dag->build();
    dag->linearize();

    arc::Budget<MockTarget> tiny_budget;
    tiny_budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 0 };
    allocator->allocate(region, tiny_budget);

    auto pressure = allocator->pressure(region, arc::RegisterClass::GENERAL_PURPOSE);
    EXPECT_LE(pressure, 1);

    auto *dag_final = dag->find(final_result);
    auto final_allocation = allocator->get(dag_final);
    EXPECT_TRUE(final_allocation.allocated() || final_allocation.spilled);
}

TEST_F(RegisterAllocatorFixture, CallerSavedPreference)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *lit_node = builder.lit(42);
	dag->build();
	dag->linearize();

	auto *dag_node = dag->find(lit_node);
	ASSERT_NE(dag_node, nullptr);

	arc::Budget<MockTarget> budget;
	auto caller_regs = target->caller_saved(arc::RegisterClass::GENERAL_PURPOSE);
	for (auto reg: caller_regs)
	{
		budget.available[arc::RegisterClass::GENERAL_PURPOSE].insert(reg);
	}
	allocator->allocate(region, budget);

	arc::Request<MockTarget> req;
	req.cls = arc::RegisterClass::GENERAL_PURPOSE;

	auto result = allocator->allocate_node(dag_node, req);
	EXPECT_TRUE(result.allocated());

	auto allocated_reg = *result.reg;
	auto is_caller_saved = std::find(caller_regs.begin(), caller_regs.end(), allocated_reg) != caller_regs.end();
	EXPECT_TRUE(is_caller_saved);
}

TEST_F(RegisterAllocatorFixture, LiveRangeRegisterReuse)
{
	arc::Builder builder(*module);
	builder.set_insertion_point(region);

	auto *val1 = builder.lit(10);
	auto *val2 = builder.lit(20);
	auto *comp1 = builder.add(val1, val2);
	auto *comp2 = builder.mul(val1, val2);

	auto *use_comp1 = builder.add(comp1, builder.lit(1));
	auto *use_comp2 = builder.add(comp2, builder.lit(2));

	auto *final_result = builder.add(use_comp1, use_comp2);

	dag->build();
	dag->linearize();

	arc::Budget<MockTarget> limited_budget;
	limited_budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 0, 1 };
	allocator->allocate(region, limited_budget);

	auto final_pressure = allocator->pressure(region, arc::RegisterClass::GENERAL_PURPOSE);
	EXPECT_LE(final_pressure, 2);

	auto *dag_final = dag->find(final_result);
	auto final_allocation = allocator->get(dag_final);
	EXPECT_TRUE(final_allocation.allocated());
}

TEST_F(RegisterAllocatorFixture, RegionHierarchyBudgetDistribution)
{
    auto *parent_region = module->create_region("parent");
    auto *child1 = module->create_region("child1", parent_region);
    auto *child2 = module->create_region("child2", parent_region);

    arc::Budget<MockTarget> total_budget;
    total_budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 0, 1, 2, 3, 4, 5, 6, 7 };

    auto parent_allocation = allocator->allocate(parent_region, total_budget);
    auto child1_allocation = allocator->allocate(child1, parent_allocation);
    auto child2_allocation = allocator->allocate(child2, parent_allocation);

    EXPECT_GT(parent_allocation.available[arc::RegisterClass::GENERAL_PURPOSE].size(), 0);
    EXPECT_LE(child1_allocation.available[arc::RegisterClass::GENERAL_PURPOSE].size(),
              parent_allocation.available[arc::RegisterClass::GENERAL_PURPOSE].size());
}

TEST_F(RegisterAllocatorFixture, ComplexControlFlowMerge)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto *val1 = builder.lit(10);
    auto *val2 = builder.lit(20);
    auto *branch1_result = builder.add(val1, builder.lit(5));
    auto *branch2_result = builder.mul(val2, builder.lit(2));

    auto *from_node = builder.create_node(arc::NodeType::FROM, arc::DataType::INT32);
    from_node->inputs = { branch1_result, branch2_result };

    auto *final_result = builder.add(from_node, builder.lit(100));

    dag->build();
    dag->linearize();

    auto *dag_from = dag->find(from_node);
    auto *dag_final = dag->find(final_result);

    arc::Budget<MockTarget> budget;
    budget.available[arc::RegisterClass::GENERAL_PURPOSE] = { 0, 1, 2, 3 };
    allocator->allocate(region, budget);

    arc::Request<MockTarget> req;
    req.cls = arc::RegisterClass::GENERAL_PURPOSE;

    auto from_allocation = allocator->allocate_node(dag_from, req);
    auto final_allocation = allocator->allocate_node(dag_final, req);

    EXPECT_TRUE(from_allocation.allocated());
    EXPECT_TRUE(final_allocation.allocated());

    EXPECT_GE(allocator->pressure(region, arc::RegisterClass::GENERAL_PURPOSE), 1);
}

TEST_F(RegisterAllocatorFixture, ConstraintAnalysisAccuracy)
{
    arc::Constraints<MockTarget> constraints;

    constraints.min_required[arc::RegisterClass::GENERAL_PURPOSE] = 3;
    constraints.max_simultaneous[arc::RegisterClass::GENERAL_PURPOSE] = 5;
    constraints.complexity[arc::RegisterClass::GENERAL_PURPOSE] = 10.5f;

    std::unordered_map<arc::RegisterClass, std::uint32_t> insufficient;
    insufficient[arc::RegisterClass::GENERAL_PURPOSE] = 2;
    EXPECT_TRUE(constraints.needs_spill(insufficient));

    std::unordered_map<arc::RegisterClass, std::uint32_t> sufficient;
    sufficient[arc::RegisterClass::GENERAL_PURPOSE] = 5;
    EXPECT_FALSE(constraints.needs_spill(sufficient));
}

TEST_F(RegisterAllocatorFixture, CrossRegionValueFlow)
{
    auto *parent = module->create_region("parent");
    auto *branch1 = module->create_region("branch1", parent);
    auto *branch2 = module->create_region("branch2", parent);
    auto *merge = module->create_region("merge", parent);

    arc::Builder builder(*module);

    builder.set_insertion_point(branch1);
    auto *branch1_val = builder.lit(42);
    auto *branch1_result = builder.add(branch1_val, builder.lit(10));

    builder.set_insertion_point(branch2);
    auto *branch2_val = builder.lit(24);
    auto *branch2_result = builder.mul(branch2_val, builder.lit(2));

    builder.set_insertion_point(merge);
    auto *from_node = builder.create_node(arc::NodeType::FROM, arc::DataType::INT32);
    from_node->inputs = { branch1_result, branch2_result };
    auto *final_result = builder.add(from_node, builder.lit(100));

    auto branch1_dag = std::make_unique<arc::SelectionDAG<MockTarget::instruction_type>>(branch1);
    auto branch2_dag = std::make_unique<arc::SelectionDAG<MockTarget::instruction_type>>(branch2);
    auto merge_dag = std::make_unique<arc::SelectionDAG<MockTarget::instruction_type>>(merge);

    branch1_dag->build();
	branch1_dag->linearize();
    branch2_dag->build();
	branch2_dag->linearize();
    merge_dag->build();
	merge_dag->linearize();

    arc::RegisterAllocator<MockTarget> allocator(*target, *merge_dag);
    auto budget = create_test_budget();

    auto parent_budget = allocator.allocate(parent, budget);
    auto branch1_budget = allocator.allocate(branch1, parent_budget);
    auto branch2_budget = allocator.allocate(branch2, parent_budget);
    auto merge_budget = allocator.allocate(merge, parent_budget);

    auto *dag_from = merge_dag->find(from_node);
    ASSERT_NE(dag_from, nullptr);

    arc::Request<MockTarget> req;
    req.cls = arc::RegisterClass::GENERAL_PURPOSE;

    auto from_result = allocator.allocate_node(dag_from, req);
    EXPECT_TRUE(from_result.allocated());

    auto total_pressure = allocator.pressure(branch1, arc::RegisterClass::GENERAL_PURPOSE) +
                         allocator.pressure(branch2, arc::RegisterClass::GENERAL_PURPOSE) +
                         allocator.pressure(merge, arc::RegisterClass::GENERAL_PURPOSE);

    EXPECT_LE(total_pressure, 13);
	std::print("total pressure: {}\n", total_pressure);
}

