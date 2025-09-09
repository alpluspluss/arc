/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/codegen/selection-dag.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/region.hpp>
#include <arc/foundation/builder.hpp>
#include <gtest/gtest.h>

struct MockInstruction
{
    enum class Opcode : std::uint8_t
    {
        NOP,
        ADD,
        SUB,
        LOAD,
        STORE
    };

    static constexpr std::size_t max_operands() { return 3; }
    static constexpr std::size_t encoding_size() { return 4; }
};

class SelectionDAGFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        module = std::make_unique<arc::Module>("test_module");
        region = module->create_region("test_region");
        dag = std::make_unique<arc::SelectionDAG<MockInstruction>>(region);
    }

    void TearDown() override {}

    std::unique_ptr<arc::Module> module;
    arc::Region* region = nullptr;
    std::unique_ptr<arc::SelectionDAG<MockInstruction>> dag;
};

TEST_F(SelectionDAGFixture, BasicConstruction)
{
    EXPECT_EQ(dag->nodes().size(), 0);
    EXPECT_EQ(dag->entries().size(), 0);
    EXPECT_EQ(dag->chain_roots().size(), 0);
}

TEST_F(SelectionDAGFixture, EmptyRegionBuild)
{
    dag->build();

    EXPECT_EQ(dag->entries().size(), 1);
    EXPECT_EQ(dag->chain_roots().size(), 1);
    EXPECT_EQ(dag->nodes().size(), 0);
}

TEST_F(SelectionDAGFixture, NodeCreation)
{
    auto* imm_node = dag->make_imm<arc::DataType::INT32>(42);
    auto* reg_node = dag->make_reg<arc::DataType::INT64>(1);
    auto* mem_node = dag->make_mem<arc::DataType::FLOAT32>(0x1000);

    EXPECT_EQ(imm_node->kind, arc::NodeKind::IMMEDIATE);
    EXPECT_EQ(imm_node->value_t, arc::DataType::INT32);
    EXPECT_EQ(imm_node->operand.value, 42);

    EXPECT_EQ(reg_node->kind, arc::NodeKind::REGISTER);
    EXPECT_EQ(reg_node->value_t, arc::DataType::INT64);
    EXPECT_EQ(reg_node->operand.value, 1);

    EXPECT_EQ(mem_node->kind, arc::NodeKind::MEMORY);
    EXPECT_EQ(mem_node->value_t, arc::DataType::FLOAT32);
    EXPECT_EQ(mem_node->operand.value, 0x1000);
}

TEST_F(SelectionDAGFixture, LiteralNodeConversion)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto* lit_node = builder.lit(123);

    dag->build();

    auto* dag_node = dag->find(lit_node);
    ASSERT_NE(dag_node, nullptr);
    EXPECT_EQ(dag_node->kind, arc::NodeKind::IMMEDIATE);
    EXPECT_EQ(dag_node->source, lit_node);
    EXPECT_EQ(dag_node->value_t, arc::DataType::INT32);
}

TEST_F(SelectionDAGFixture, ArithmeticNodeConversion)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto* lhs = builder.lit(10);
    auto* rhs = builder.lit(20);
    auto* add_node = builder.add(lhs, rhs);

    dag->build();

    auto* dag_add = dag->find(add_node);
    ASSERT_NE(dag_add, nullptr);
    EXPECT_EQ(dag_add->kind, arc::NodeKind::VALUE);
    EXPECT_EQ(dag_add->source, add_node);
    EXPECT_EQ(dag_add->value_t, add_node->type_kind);

    EXPECT_EQ(dag_add->operands.size(), 2);

    auto* dag_lhs = dag->find(lhs);
    auto* dag_rhs = dag->find(rhs);
    ASSERT_NE(dag_lhs, nullptr);
    ASSERT_NE(dag_rhs, nullptr);

    EXPECT_TRUE(std::find(dag_add->operands.begin(), dag_add->operands.end(), dag_lhs) != dag_add->operands.end());
    EXPECT_TRUE(std::find(dag_add->operands.begin(), dag_add->operands.end(), dag_rhs) != dag_add->operands.end());
}

TEST_F(SelectionDAGFixture, ChainDependencies)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto* alloc_node = builder.alloc<arc::DataType::INT32>(builder.lit(1));
    auto* load_node = builder.load(alloc_node);

    dag->build();

    auto* dag_load = dag->find(load_node);
    ASSERT_NE(dag_load, nullptr);
    EXPECT_EQ(dag_load->kind, arc::NodeKind::VALUE);

    bool has_chain_dependency = false;
    for (auto* operand : dag_load->operands)
    {
        if (operand->kind == arc::NodeKind::ENTRY)
        {
            has_chain_dependency = true;
            break;
        }
    }
    EXPECT_TRUE(has_chain_dependency);
}

TEST_F(SelectionDAGFixture, TopologicalSort)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto* a = builder.lit(1);
    auto* b = builder.lit(2);
    auto* c = builder.add(a, b);
    auto* d = builder.mul(c, a);

    dag->build();

    auto sorted = dag->sort();

    auto find_index = [&](arc::Node* ir_node) -> std::size_t
    {
        auto* dag_node = dag->find(ir_node);
        auto it = std::ranges::find(sorted, dag_node);
        return it != sorted.end() ? static_cast<std::size_t>(std::distance(sorted.begin(), it)) : sorted.size();
    };

    std::size_t idx_a = find_index(a);
    std::size_t idx_b = find_index(b);
    std::size_t idx_c = find_index(c);
    std::size_t idx_d = find_index(d);

    EXPECT_LT(idx_a, idx_c);
    EXPECT_LT(idx_b, idx_c);
    EXPECT_LT(idx_c, idx_d);
    EXPECT_LT(idx_a, idx_d);
}

TEST_F(SelectionDAGFixture, SelectionStateTracking)
{
    auto* node = dag->make_node<arc::NodeKind::VALUE>();

    EXPECT_EQ(node->state, arc::SelectionState::UNSELECTED);

    node->state |= arc::SelectionState::SELECTED;
    EXPECT_TRUE((node->state & arc::SelectionState::SELECTED) != arc::SelectionState::UNSELECTED);

    node->state |= arc::SelectionState::SCHEDULED;
    EXPECT_TRUE((node->state & arc::SelectionState::SCHEDULED) != arc::SelectionState::UNSELECTED);
    EXPECT_TRUE((node->state & arc::SelectionState::SELECTED) != arc::SelectionState::UNSELECTED);
}

TEST_F(SelectionDAGFixture, VectorOperations)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto* elem1 = builder.lit(1);
    auto* elem2 = builder.lit(2);
    auto* vector_build = builder.vector_build({elem1, elem2});
    auto* vector_extract = builder.vector_extract(vector_build, 0);

    dag->build();

    auto* dag_build = dag->find(vector_build);
    auto* dag_extract = dag->find(vector_extract);

    ASSERT_NE(dag_build, nullptr);
    ASSERT_NE(dag_extract, nullptr);

    EXPECT_EQ(dag_build->value_t, arc::DataType::VECTOR);
    EXPECT_EQ(dag_extract->operands.size(), 2);
}

TEST_F(SelectionDAGFixture, FunctionNodeRejection)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto* func_node = builder.create_node(arc::NodeType::FUNCTION, arc::DataType::FUNCTION);
    region->append(func_node);

    EXPECT_THROW(dag->build(), std::runtime_error);
}

TEST_F(SelectionDAGFixture, MemoryOperationChaining)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto* alloc_node = builder.alloc<arc::DataType::INT32>(builder.lit(1));
    auto* value = builder.lit(42);
    auto* store_node = builder.store(value, alloc_node);

    dag->build();

    auto* dag_store = dag->find(store_node);
    ASSERT_NE(dag_store, nullptr);
    EXPECT_EQ(dag_store->kind, arc::NodeKind::CHAIN);

    bool has_chain_input = false;
    for (auto* operand : dag_store->operands)
    {
        if (operand->kind == arc::NodeKind::ENTRY)
        {
            has_chain_input = true;
            break;
        }
    }
    EXPECT_TRUE(has_chain_input);
}

TEST_F(SelectionDAGFixture, ValueIDAssignment)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto* node1 = builder.lit(1);
    auto* node2 = builder.lit(2);
    auto* node3 = builder.add(node1, node2);

    dag->build();

    auto* dag1 = dag->find(node1);
    auto* dag2 = dag->find(node2);
    auto* dag3 = dag->find(node3);

    ASSERT_NE(dag1, nullptr);
    ASSERT_NE(dag2, nullptr);
    ASSERT_NE(dag3, nullptr);

    EXPECT_NE(dag1->value_id, 0);
    EXPECT_NE(dag2->value_id, 0);
    EXPECT_NE(dag3->value_id, 0);

    EXPECT_NE(dag1->value_id, dag2->value_id);
    EXPECT_NE(dag2->value_id, dag3->value_id);
    EXPECT_NE(dag1->value_id, dag3->value_id);
}
