/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/codegen/insn-selector.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/region.hpp>
#include <arc/foundation/builder.hpp>
#include <gtest/gtest.h>

struct MockInstruction
{
    enum class Opcode : std::uint16_t
    {
        NOP,
        ADD_REG,
        ADD_IMM,
        SUB_REG,
        SUB_IMM,
        MUL_REG,
        LOAD,
        STORE,
        MOV_REG,
        MOV_IMM
    };

    static constexpr std::size_t max_operands()
    {
        return 3;
    }

    static constexpr std::size_t encoding_size()
    {
        return 4;
    }
};

template<>
struct arc::TargetTraits<arc::TargetArch::AARCH64>
{
    using instruction_type = MockInstruction;

    static constexpr arc::TargetArch target_arch()
    {
        return arc::TargetArch::AARCH64;
    }
};

class InstructionSelectorFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        module = std::make_unique<arc::Module>("test_module");
        region = module->create_region("test_region");
        dag = std::make_unique<arc::SelectionDAG<MockInstruction> >(region);
        selector = std::make_unique<arc::InstructionSelector<arc::TargetTraits<arc::TargetArch::AARCH64> > >(*dag);
    }

    void TearDown() override {}

    std::unique_ptr<arc::Module> module;
    arc::Region *region = nullptr;
    std::unique_ptr<arc::SelectionDAG<MockInstruction> > dag;
    std::unique_ptr<arc::InstructionSelector<arc::TargetTraits<arc::TargetArch::AARCH64> > > selector;
};

TEST_F(InstructionSelectorFixture, BasicConstruction)
{
    EXPECT_EQ(selector->patterns().size(), 0);
}

TEST_F(InstructionSelectorFixture, PatternRegistration)
{
    auto predicate = [](auto *node)
    {
        return node && node->kind == arc::NodeKind::VALUE;
    };
    auto generator = [](auto *node)
    {
        return node;
    };

    selector->define(predicate, generator, 10, "test_pattern");

    EXPECT_EQ(selector->patterns().size(), 1);
    EXPECT_EQ(selector->patterns()[0].priority, 10);
    EXPECT_EQ(selector->patterns()[0].name, "test_pattern");
}

TEST_F(InstructionSelectorFixture, PatternPriorityOrdering)
{
    auto pred = [](auto *node)
    {
        return true;
    };
    auto gen = [](auto *node)
    {
        return node;
    };

    selector->define(pred, gen, 5, "low_priority");
    selector->define(pred, gen, 15, "high_priority");
    selector->define(pred, gen, 10, "med_priority");

    const auto &patterns = selector->patterns();
    EXPECT_EQ(patterns[0].priority, 15);
    EXPECT_EQ(patterns[1].priority, 10);
    EXPECT_EQ(patterns[2].priority, 5);
}

TEST_F(InstructionSelectorFixture, PatternStructCreation)
{
    arc::InstructionSelector<arc::TargetTraits<arc::TargetArch::AARCH64> >::Pattern pattern {
        .predicate = [](auto *node)
        {
            return node != nullptr;
        },
        .generator = [](auto *node)
        {
            return node;
        },
        .priority = 100,
        .name = "struct_pattern"
    };

    selector->define(std::move(pattern));

    EXPECT_EQ(selector->patterns().size(), 1);
    EXPECT_EQ(selector->patterns()[0].priority, 100);
    EXPECT_EQ(selector->patterns()[0].name, "struct_pattern");
}

TEST_F(InstructionSelectorFixture, NodeSelectionSuccess)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto *lit_node = builder.lit(42);
    dag->build();

    auto *dag_node = dag->find(lit_node);
    ASSERT_NE(dag_node, nullptr);

    bool pattern_matched = false;
    selector->define(
        [](auto *node)
        {
            return node->kind == arc::NodeKind::IMMEDIATE;
        },
        [&pattern_matched](auto *node)
        {
            pattern_matched = true;
            node->state |= arc::SelectionState::SELECTED;
            return node;
        },
        10,
        "immediate_pattern"
    );

    bool result = selector->select(dag_node);
    EXPECT_TRUE(result);
    EXPECT_TRUE(pattern_matched);
    EXPECT_TRUE((dag_node->state & arc::SelectionState::SELECTED) != arc::SelectionState::UNSELECTED);
}

TEST_F(InstructionSelectorFixture, NodeSelectionFailure)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto *lit_node = builder.lit(42);
    dag->build();

    auto *dag_node = dag->find(lit_node);
    ASSERT_NE(dag_node, nullptr);

    selector->define(
        [](auto *node)
        {
            return node->kind == arc::NodeKind::REGISTER;
        },
        [](auto *node)
        {
            return node;
        },
        10,
        "register_pattern"
    );

    bool result = selector->select(dag_node);
    EXPECT_FALSE(result);
    EXPECT_TRUE((dag_node->state & arc::SelectionState::SELECTED) == arc::SelectionState::UNSELECTED);
}

TEST_F(InstructionSelectorFixture, AlreadySelectedNode)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto *lit_node = builder.lit(42);
    dag->build();

    auto *dag_node = dag->find(lit_node);
    ASSERT_NE(dag_node, nullptr);

    dag_node->state |= arc::SelectionState::SELECTED;

    selector->define(
        [](auto *node)
        {
            return true;
        },
        [](auto *node)
        {
            return node;
        },
        10,
        "any_pattern"
    );

    bool result = selector->select(dag_node);
    EXPECT_FALSE(result);
}

TEST_F(InstructionSelectorFixture, ArithmeticPatternMatching)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto *lhs = builder.lit(10);
    auto *rhs = builder.lit(20);
    auto *add_node = builder.add(lhs, rhs);

    dag->build();

    auto *dag_add = dag->find(add_node);
    ASSERT_NE(dag_add, nullptr);

    bool add_pattern_matched = false;
    selector->define(
        [](auto *node)
        {
            return node->source && node->source->ir_type == arc::NodeType::ADD;
        },
        [&add_pattern_matched](auto *node)
        {
            add_pattern_matched = true;
            node->state |= arc::SelectionState::SELECTED;
            return node;
        },
        10,
        "add_pattern"
    );

    bool result = selector->select(dag_add);
    EXPECT_TRUE(result);
    EXPECT_TRUE(add_pattern_matched);
}

TEST_F(InstructionSelectorFixture, InstructionNodeCreation)
{
    auto *insn_node = selector->make_instruction(MockInstruction::Opcode::ADD_REG);

    EXPECT_EQ(insn_node->kind, arc::NodeKind::INSTRUCTION);
    EXPECT_TRUE(insn_node->opcode.has_value());
    EXPECT_EQ(insn_node->opcode.value(), MockInstruction::Opcode::ADD_REG);
}

TEST_F(InstructionSelectorFixture, InstructionWithOperands)
{
    auto *reg1 = selector->make_reg<arc::DataType::INT32>(1);
    auto *reg2 = selector->make_reg<arc::DataType::INT32>(2);

    auto *insn_node = selector->make_instruction(
        MockInstruction::Opcode::ADD_REG,
        { reg1, reg2 }
    );

    EXPECT_EQ(insn_node->operands.size(), 2);
    EXPECT_EQ(insn_node->operands[0], reg1);
    EXPECT_EQ(insn_node->operands[1], reg2);
    EXPECT_EQ(reg1->users.size(), 1);
    EXPECT_EQ(reg2->users.size(), 1);
    EXPECT_EQ(reg1->users[0], insn_node);
    EXPECT_EQ(reg2->users[0], insn_node);
}

TEST_F(InstructionSelectorFixture, OperandCreation)
{
    auto *reg_node = selector->make_reg<arc::DataType::INT64>(5);
    auto *imm_node = selector->make_imm<arc::DataType::INT32>(100);
    auto *mem_node = selector->make_mem<arc::DataType::FLOAT32>(0x2000);

    EXPECT_EQ(reg_node->kind, arc::NodeKind::REGISTER);
    EXPECT_EQ(reg_node->value_t, arc::DataType::INT64);
    EXPECT_EQ(reg_node->operand.value, 5);

    EXPECT_EQ(imm_node->kind, arc::NodeKind::IMMEDIATE);
    EXPECT_EQ(imm_node->value_t, arc::DataType::INT32);
    EXPECT_EQ(imm_node->operand.value, 100);

    EXPECT_EQ(mem_node->kind, arc::NodeKind::MEMORY);
    EXPECT_EQ(mem_node->value_t, arc::DataType::FLOAT32);
    EXPECT_EQ(mem_node->operand.value, 0x2000);
}

TEST_F(InstructionSelectorFixture, SelectAllNodes)
{
    arc::Builder builder(*module);
    builder.set_insertion_point(region);

    auto *lit1 = builder.lit(1);
    auto *lit2 = builder.lit(2);
    [[maybe_unused]] auto *add_result = builder.add(lit1, lit2);

    dag->build();

    int selections_made = 0;
    selector->define(
        [](auto *node)
        {
            return node->kind == arc::NodeKind::VALUE || node->kind == arc::NodeKind::IMMEDIATE;
        },
        [&selections_made](auto *node)
        {
            selections_made++;
            node->state |= arc::SelectionState::SELECTED;
            return node;
        },
        10,
        "universal_pattern"
    );

    auto instructions = selector->select_all();
    EXPECT_EQ(selections_made, 3);
}

TEST_F(InstructionSelectorFixture, FactoryFunction)
{
    auto created_selector = arc::create_selector<arc::TargetArch::AARCH64>(*dag);

    EXPECT_NE(created_selector, nullptr);
    EXPECT_EQ(created_selector->patterns().size(), 0);
}
