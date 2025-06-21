/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <arc/support/allocator.hpp>
#include <arc/support/inference.hpp>
#include <gtest/gtest.h>

class InferenceFixture : public testing::Test
{
protected:
    void SetUp() override
    {
        alloc = std::make_unique<ach::allocator<arc::Node>>();
    }

    void TearDown() override
    {
        /* cleanup all allocated nodes */
        for (arc::Node* node : allocated_nodes)
        {
            alloc->destroy(node);
            alloc->deallocate(node, 1);
        }
        allocated_nodes.clear();
    }

    arc::Node* create_node(arc::DataType type)
    {
        arc::Node* node = alloc->allocate(1);
        alloc->construct(node);
        node->type_kind = type;
        allocated_nodes.push_back(node);
        return node;
    }

    arc::Node* create_vector_node(arc::DataType elem_type)
    {
        arc::Node* node = create_node(arc::DataType::VECTOR);
        arc::DataTraits<arc::DataType::VECTOR>::value vec_data = {};
        vec_data.elem_type = elem_type;
        node->value.set<decltype(vec_data), arc::DataType::VECTOR>(vec_data);
        return node;
    }

    std::unique_ptr<ach::allocator<arc::Node>> alloc;
    std::vector<arc::Node*> allocated_nodes;
};

TEST_F(InferenceFixture, IdenticalPrimitiveTypes)
{
    auto* lhs = create_node(arc::DataType::INT32);
    auto* rhs = create_node(arc::DataType::INT32);

    EXPECT_TRUE(arc::infer_binary_t(lhs, rhs));
    EXPECT_EQ(lhs->type_kind, arc::DataType::INT32);
    EXPECT_EQ(rhs->type_kind, arc::DataType::INT32);
}

TEST_F(InferenceFixture, BoolPromotesToInt32)
{
    auto* lhs = create_node(arc::DataType::BOOL);
    auto* rhs = create_node(arc::DataType::INT16);

    EXPECT_TRUE(arc::infer_binary_t(lhs, rhs));
    EXPECT_EQ(lhs->type_kind, arc::DataType::INT32);
    EXPECT_EQ(rhs->type_kind, arc::DataType::INT32);
}

TEST_F(InferenceFixture, SmallIntegersPromoteToInt32)
{
    auto* lhs = create_node(arc::DataType::INT8);
    auto* rhs = create_node(arc::DataType::UINT16);
    EXPECT_TRUE(arc::infer_binary_t(lhs, rhs));
    EXPECT_EQ(lhs->type_kind, arc::DataType::INT32);
    EXPECT_EQ(rhs->type_kind, arc::DataType::INT32);
}

TEST_F(InferenceFixture, MixedSignednessPromotion)
{
    auto* lhs = create_node(arc::DataType::INT32);
    auto* rhs = create_node(arc::DataType::UINT32);

    EXPECT_TRUE(arc::infer_binary_t(lhs, rhs));
    EXPECT_EQ(lhs->type_kind, arc::DataType::INT64);
    EXPECT_EQ(rhs->type_kind, arc::DataType::INT64);
}

TEST_F(InferenceFixture, Int64UInt64Conflict)
{
    auto* lhs = create_node(arc::DataType::INT64);
    auto* rhs = create_node(arc::DataType::UINT64);

    EXPECT_TRUE(arc::infer_binary_t(lhs, rhs));
    EXPECT_EQ(lhs->type_kind, arc::DataType::UINT64);
    EXPECT_EQ(rhs->type_kind, arc::DataType::UINT64);
}

TEST_F(InferenceFixture, FloatPromotesToDouble)
{
    auto* lhs = create_node(arc::DataType::INT32);
    auto* rhs = create_node(arc::DataType::FLOAT32);

    EXPECT_TRUE(arc::infer_binary_t(lhs, rhs));
    EXPECT_EQ(lhs->type_kind, arc::DataType::FLOAT64);
    EXPECT_EQ(rhs->type_kind, arc::DataType::FLOAT64);
}

TEST_F(InferenceFixture, BothFloat32StaysFloat32)
{
    auto* lhs = create_node(arc::DataType::FLOAT32);
    auto* rhs = create_node(arc::DataType::FLOAT32);

    EXPECT_TRUE(arc::infer_binary_t(lhs, rhs));
    EXPECT_EQ(lhs->type_kind, arc::DataType::FLOAT32);
    EXPECT_EQ(rhs->type_kind, arc::DataType::FLOAT32);
}

TEST_F(InferenceFixture, IdenticalVectorTypes)
{
    auto* lhs = create_vector_node(arc::DataType::INT32);
    auto* rhs = create_vector_node(arc::DataType::INT32);

    EXPECT_TRUE(arc::infer_binary_t(lhs, rhs));
    EXPECT_EQ(lhs->type_kind, arc::DataType::VECTOR);
    EXPECT_EQ(rhs->type_kind, arc::DataType::VECTOR);

    auto& lhs_vec = lhs->value.get<arc::DataType::VECTOR>();
    auto& rhs_vec = rhs->value.get<arc::DataType::VECTOR>();
    EXPECT_EQ(lhs_vec.elem_type, arc::DataType::INT32);
    EXPECT_EQ(rhs_vec.elem_type, arc::DataType::INT32);
}

TEST_F(InferenceFixture, VectorElementPromotion)
{
    auto* lhs = create_vector_node(arc::DataType::INT8);
    auto* rhs = create_vector_node(arc::DataType::FLOAT32);

    EXPECT_TRUE(arc::infer_binary_t(lhs, rhs));
    EXPECT_EQ(lhs->type_kind, arc::DataType::VECTOR);
    EXPECT_EQ(rhs->type_kind, arc::DataType::VECTOR);

    auto& lhs_vec = lhs->value.get<arc::DataType::VECTOR>();
    auto& rhs_vec = rhs->value.get<arc::DataType::VECTOR>();
    EXPECT_EQ(lhs_vec.elem_type, arc::DataType::FLOAT64);
    EXPECT_EQ(rhs_vec.elem_type, arc::DataType::FLOAT64);
}

TEST_F(InferenceFixture, VectorScalarMixingFails)
{
    auto* lhs = create_vector_node(arc::DataType::INT32);
    auto* rhs = create_node(arc::DataType::INT32);

    EXPECT_FALSE(arc::infer_binary_t(lhs, rhs));
}

TEST_F(InferenceFixture, IncompatibleTypesFail)
{
    auto* lhs = create_node(arc::DataType::INT32);
    auto* rhs = create_node(arc::DataType::POINTER);
    bool result = infer_binary_t(lhs, rhs);
    EXPECT_FALSE(result);
}

TEST_F(InferenceFixture, VoidTypesFail)
{
    auto* lhs = create_node(arc::DataType::VOID);
    auto* rhs = create_node(arc::DataType::INT32);

    EXPECT_FALSE(arc::infer_binary_t(lhs, rhs));
}

TEST_F(InferenceFixture, NullPointersFail)
{
    EXPECT_FALSE(arc::infer_binary_t(nullptr, nullptr));

    auto* node = create_node(arc::DataType::INT32);
    EXPECT_FALSE(arc::infer_binary_t(node, nullptr));
    EXPECT_FALSE(arc::infer_binary_t(nullptr, node));
}

TEST_F(InferenceFixture, HelperFunctions)
{
    EXPECT_TRUE(arc::is_integer_t(arc::DataType::INT32));
    EXPECT_TRUE(arc::is_integer_t(arc::DataType::UINT64));
    EXPECT_FALSE(arc::is_integer_t(arc::DataType::FLOAT32));

    EXPECT_TRUE(arc::is_float_t(arc::DataType::FLOAT64));
    EXPECT_FALSE(arc::is_float_t(arc::DataType::INT32));

    EXPECT_TRUE(arc::is_signed_integer_t(arc::DataType::INT32));
    EXPECT_FALSE(arc::is_signed_integer_t(arc::DataType::UINT32));

    EXPECT_TRUE(arc::is_unsigned_integer_t(arc::DataType::UINT32));
    EXPECT_FALSE(arc::is_unsigned_integer_t(arc::DataType::INT32));

    EXPECT_EQ(arc::get_integer_rank(arc::DataType::INT64), 3);
    EXPECT_EQ(arc::get_integer_rank(arc::DataType::INT8), 0);
    EXPECT_EQ(arc::get_integer_rank(arc::DataType::FLOAT32), -1);
}
