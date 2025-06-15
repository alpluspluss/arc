/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/foundation/typed-data.hpp>
#include <gtest/gtest.h>

class TypedDataFixture : public ::testing::Test
{
protected:
    void SetUp() override {}

    void TearDown() override {}

    arc::TypedData data;
};

TEST_F(TypedDataFixture, DefaultConstructor)
{
    EXPECT_EQ(data.type(), arc::DataType::VOID);
}

TEST_F(TypedDataFixture, SetTrivialData)
{
    data.set<int, arc::DataType::INT32>(42);
    EXPECT_EQ(data.type(), arc::DataType::INT32);
    EXPECT_EQ(data.get<arc::DataType::INT32>(), 42);
}

TEST_F(TypedDataFixture, SetBoolData)
{
    data.set<bool, arc::DataType::BOOL>(true);
    EXPECT_EQ(data.type(), arc::DataType::BOOL);
    EXPECT_TRUE(data.get<arc::DataType::BOOL>());

    data.set<bool, arc::DataType::BOOL>(false);
    EXPECT_FALSE(data.get<arc::DataType::BOOL>());
}

TEST_F(TypedDataFixture, SetFloatData)
{
    data.set<float, arc::DataType::FLOAT32>(3.14f);
    EXPECT_EQ(data.type(), arc::DataType::FLOAT32);
    EXPECT_FLOAT_EQ(data.get<arc::DataType::FLOAT32>(), 3.14f);
}

TEST_F(TypedDataFixture, SetStringData)
{
    arc::StringTable::StringId str_id = 123;
    data.set<arc::StringTable::StringId, arc::DataType::STRING>(str_id);
    EXPECT_EQ(data.type(), arc::DataType::STRING);
    EXPECT_EQ(data.get<arc::DataType::STRING>(), str_id);
}

TEST_F(TypedDataFixture, SetPointerData)
{
    arc::Node* dummy_node = reinterpret_cast<arc::Node*>(0x12345678);

    arc::DataTraits<arc::DataType::POINTER>::value ptr_data;
    ptr_data.pointee = dummy_node;
    ptr_data.addr_space = 1;

    data.set<decltype(ptr_data), arc::DataType::POINTER>(ptr_data);
    EXPECT_EQ(data.type(), arc::DataType::POINTER);

    auto& retrieved = data.get<arc::DataType::POINTER>();
    EXPECT_EQ(retrieved.pointee, dummy_node);
    EXPECT_EQ(retrieved.addr_space, 1);
}

TEST_F(TypedDataFixture, SetVectorData)
{
    arc::DataTraits<arc::DataType::VECTOR>::value vec_data;
    vec_data.elem_type = arc::DataType::FLOAT32;

    data.set<decltype(vec_data), arc::DataType::VECTOR>(vec_data);
    EXPECT_EQ(data.type(), arc::DataType::VECTOR);

    auto& retrieved = data.get<arc::DataType::VECTOR>();
    EXPECT_EQ(retrieved.elem_type, arc::DataType::FLOAT32);
}

TEST_F(TypedDataFixture, SetArrayData)
{
    arc::Node* elem1 = reinterpret_cast<arc::Node*>(0x1000);
    arc::Node* elem2 = reinterpret_cast<arc::Node*>(0x2000);
    arc::Node* elem3 = reinterpret_cast<arc::Node*>(0x3000);

    arc::DataTraits<arc::DataType::ARRAY>::value arr_data;
    arr_data.elements = arc::u16slice<arc::Node*>{elem1, elem2, elem3};
    arr_data.elem_type = arc::DataType::INT32;

    data.set<decltype(arr_data), arc::DataType::ARRAY>(std::move(arr_data));
    EXPECT_EQ(data.type(), arc::DataType::ARRAY);

    auto& retrieved = data.get<arc::DataType::ARRAY>();
    EXPECT_EQ(retrieved.elements.size(), 3);
    EXPECT_EQ(retrieved.elements[0], elem1);
    EXPECT_EQ(retrieved.elements[1], elem2);
    EXPECT_EQ(retrieved.elements[2], elem3);
    EXPECT_EQ(retrieved.elem_type, arc::DataType::INT32);
}

TEST_F(TypedDataFixture, SetStructData)
{
    arc::DataTraits<arc::DataType::STRUCT>::value struct_data;
    struct_data.fields = arc::u8slice<std::pair<arc::StringTable::StringId, arc::DataType>>{
        {1, arc::DataType::INT32},
        {2, arc::DataType::FLOAT32}
    };
    struct_data.alignment = 8;

    data.set<decltype(struct_data), arc::DataType::STRUCT>(std::move(struct_data));
    EXPECT_EQ(data.type(), arc::DataType::STRUCT);

    auto& retrieved = data.get<arc::DataType::STRUCT>();
    EXPECT_EQ(retrieved.fields.size(), 2);
    EXPECT_EQ(retrieved.fields[0].first, 1);
    EXPECT_EQ(retrieved.fields[0].second, arc::DataType::INT32);
    EXPECT_EQ(retrieved.fields[1].first, 2);
    EXPECT_EQ(retrieved.fields[1].second, arc::DataType::FLOAT32);
    EXPECT_EQ(retrieved.alignment, 8);
}

TEST_F(TypedDataFixture, TypeCheck)
{
    data.set<int, arc::DataType::INT32>(42);
    EXPECT_TRUE(data.is_type<int>());
    EXPECT_FALSE(data.is_type<float>());
    EXPECT_FALSE(data.is_type<bool>());
}

TEST_F(TypedDataFixture, TypeMismatchThrows)
{
    data.set<int, arc::DataType::INT32>(42);
    EXPECT_THROW(data.get<arc::DataType::FLOAT32>(), std::bad_cast);
    EXPECT_THROW(data.get<arc::DataType::BOOL>(), std::bad_cast);
}

TEST_F(TypedDataFixture, CopyConstructor)
{
    data.set<int, arc::DataType::INT32>(42);

    arc::TypedData copied(data);
    EXPECT_EQ(copied.type(), arc::DataType::INT32);
    EXPECT_EQ(copied.get<arc::DataType::INT32>(), 42);
    EXPECT_EQ(data.get<arc::DataType::INT32>(), 42);
}

TEST_F(TypedDataFixture, MoveConstructor)
{
    arc::Node* elem1 = reinterpret_cast<arc::Node*>(0x1000);
    arc::Node* elem2 = reinterpret_cast<arc::Node*>(0x2000);

    arc::DataTraits<arc::DataType::ARRAY>::value arr_data;
    arr_data.elements = arc::u16slice<arc::Node*>{elem1, elem2};
    arr_data.elem_type = arc::DataType::INT32;

    data.set<decltype(arr_data), arc::DataType::ARRAY>(std::move(arr_data));

    arc::TypedData moved(std::move(data));
    EXPECT_EQ(moved.type(), arc::DataType::ARRAY);

    auto& moved_array = moved.get<arc::DataType::ARRAY>();
    EXPECT_EQ(moved_array.elements.size(), 2);
    EXPECT_EQ(moved_array.elements[0], elem1);
    EXPECT_EQ(moved_array.elements[1], elem2);

    /* original should be in moved-from state as VOID */
    EXPECT_EQ(data.type(), arc::DataType::VOID);
}

TEST_F(TypedDataFixture, CopyAssignment)
{
    data.set<int, arc::DataType::INT32>(42);

    arc::TypedData other = data;

    EXPECT_EQ(other.type(), arc::DataType::INT32);
    EXPECT_EQ(other.get<arc::DataType::INT32>(), 42);
    EXPECT_EQ(data.get<arc::DataType::INT32>(), 42);
}

TEST_F(TypedDataFixture, MoveAssignment)
{
    arc::DataTraits<arc::DataType::POINTER>::value ptr_data;
    ptr_data.pointee = reinterpret_cast<arc::Node*>(0x12345678);
    ptr_data.addr_space = 2;

    data.set<decltype(ptr_data), arc::DataType::POINTER>(ptr_data);

    arc::TypedData other = std::move(data);

    EXPECT_EQ(other.type(), arc::DataType::POINTER);
    auto& other_ptr = other.get<arc::DataType::POINTER>();
    EXPECT_EQ(other_ptr.pointee, reinterpret_cast<arc::Node*>(0x12345678));
    EXPECT_EQ(other_ptr.addr_space, 2);

    EXPECT_EQ(data.type(), arc::DataType::VOID);
}

TEST_F(TypedDataFixture, OverwriteData)
{
    data.set<int, arc::DataType::INT32>(42);
    EXPECT_EQ(data.get<arc::DataType::INT32>(), 42);

    data.set<float, arc::DataType::FLOAT32>(3.14f);
    EXPECT_EQ(data.type(), arc::DataType::FLOAT32);
    EXPECT_FLOAT_EQ(data.get<arc::DataType::FLOAT32>(), 3.14f);

    EXPECT_THROW(data.get<arc::DataType::INT32>(), std::bad_cast);
}

TEST_F(TypedDataFixture, VoidType)
{
    EXPECT_EQ(data.type(), arc::DataType::VOID);

    auto& void_val = data.get<arc::DataType::VOID>();

    auto& void_val2 = data.get<arc::DataType::VOID>();
    EXPECT_TRUE(void_val == void_val2);
}

TEST_F(TypedDataFixture, FunctionType)
{
    arc::DataTraits<arc::DataType::FUNCTION>::value func_data;

    data.set<decltype(func_data), arc::DataType::FUNCTION>(func_data);
    EXPECT_EQ(data.type(), arc::DataType::FUNCTION);

    [[maybe_unused]] auto& retrieved = data.get<arc::DataType::FUNCTION>();
    (void)retrieved;
}
