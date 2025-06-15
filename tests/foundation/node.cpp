/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/foundation/node.hpp>
#include <gtest/gtest.h>

class NodeFixture : public ::testing::Test
{
protected:
	void SetUp() override {}

	void TearDown() override {}

	arc::Node node;
};

TEST_F(NodeFixture, BasicProperties)
{
	node.ir_type = arc::NodeType::ADD;
	node.str_id = 42;

	EXPECT_EQ(node.ir_type, arc::NodeType::ADD);
	EXPECT_EQ(node.str_id, 42);
}

TEST_F(NodeFixture, NodeRelationships)
{
	arc::Node input1;
	arc::Node input2;
	arc::Node user1;
	arc::Node user2;

	node.inputs.push_back(&input1);
	node.inputs.push_back(&input2);
	node.users.push_back(&user1);
	node.users.push_back(&user2);

	EXPECT_EQ(node.inputs.size(), 2);
	EXPECT_EQ(node.users.size(), 2);
	EXPECT_EQ(node.inputs[0], &input1);
	EXPECT_EQ(node.inputs[1], &input2);
	EXPECT_EQ(node.users[0], &user1);
	EXPECT_EQ(node.users[1], &user2);
}

TEST_F(NodeFixture, NodeTypeOperations)
{
	node.ir_type = arc::NodeType::ADD;
	EXPECT_EQ(node.ir_type, arc::NodeType::ADD);

	node.ir_type = arc::NodeType::CALL;
	EXPECT_EQ(node.ir_type, arc::NodeType::CALL);

	const std::vector cf_nodes = {
		arc::NodeType::ENTRY, arc::NodeType::EXIT
	};

	const std::vector val_nodes = {
		arc::NodeType::PARAM, arc::NodeType::LIT
	};

	const std::vector op_nodes = {
		arc::NodeType::ADD, arc::NodeType::SUB, arc::NodeType::MUL, arc::NodeType::DIV,
		arc::NodeType::GT, arc::NodeType::GTE, arc::NodeType::LT, arc::NodeType::LTE,
		arc::NodeType::EQ, arc::NodeType::NEQ, arc::NodeType::BAND, arc::NodeType::BOR,
		arc::NodeType::BXOR, arc::NodeType::BNOT, arc::NodeType::BSHL, arc::NodeType::BSHR,
		arc::NodeType::RET
	};

	node.ir_type = cf_nodes[0];
	EXPECT_EQ(node.ir_type, arc::NodeType::ENTRY);

	node.ir_type = val_nodes[0];
	EXPECT_EQ(node.ir_type, arc::NodeType::PARAM);

	node.ir_type = op_nodes[0];
	EXPECT_EQ(node.ir_type, arc::NodeType::ADD);
}
