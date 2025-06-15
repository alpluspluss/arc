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
	arc::Node *dummy_node = reinterpret_cast<arc::Node *>(0x12345678);

	arc::DataTraits<arc::DataType::POINTER>::value ptr_data;
	ptr_data.pointee = dummy_node;
	ptr_data.addr_space = 1;

	data.set<decltype(ptr_data), arc::DataType::POINTER>(ptr_data);
	EXPECT_EQ(data.type(), arc::DataType::POINTER);

	auto &retrieved = data.get<arc::DataType::POINTER>();
	EXPECT_EQ(retrieved.pointee, dummy_node);
	EXPECT_EQ(retrieved.addr_space, 1);
}

TEST_F(TypedDataFixture, SetVectorData)
{
	arc::DataTraits<arc::DataType::VECTOR>::value vec_data;
	vec_data.elem_type = arc::DataType::FLOAT32;

	data.set<decltype(vec_data), arc::DataType::VECTOR>(vec_data);
	EXPECT_EQ(data.type(), arc::DataType::VECTOR);

	auto &retrieved = data.get<arc::DataType::VECTOR>();
	EXPECT_EQ(retrieved.elem_type, arc::DataType::FLOAT32);
}

TEST_F(TypedDataFixture, SetArrayData)
{
	arc::Node *elem1 = reinterpret_cast<arc::Node *>(0x1000);
	arc::Node *elem2 = reinterpret_cast<arc::Node *>(0x2000);
	arc::Node *elem3 = reinterpret_cast<arc::Node *>(0x3000);

	arc::DataTraits<arc::DataType::ARRAY>::value arr_data;
	arr_data.elements = arc::u16slice<arc::Node *> { elem1, elem2, elem3 };
	arr_data.elem_type = arc::DataType::INT32;

	data.set<decltype(arr_data), arc::DataType::ARRAY>(std::move(arr_data));
	EXPECT_EQ(data.type(), arc::DataType::ARRAY);

	auto &retrieved = data.get<arc::DataType::ARRAY>();
	EXPECT_EQ(retrieved.elements.size(), 3);
	EXPECT_EQ(retrieved.elements[0], elem1);
	EXPECT_EQ(retrieved.elements[1], elem2);
	EXPECT_EQ(retrieved.elements[2], elem3);
	EXPECT_EQ(retrieved.elem_type, arc::DataType::INT32);
}

TEST_F(TypedDataFixture, SetStructData)
{
	arc::DataTraits<arc::DataType::STRUCT>::value struct_data;

	arc::TypedData field1_value = {};
	arc::TypedData field2_value = {};
	field1_value.set<std::int32_t, arc::DataType::INT32>(42);
	field2_value.set<float, arc::DataType::FLOAT32>(3.14f);

	struct_data.fields = arc::u8slice<std::tuple<arc::StringTable::StringId, arc::DataType, arc::TypedData> > {
		{ 1, arc::DataType::INT32, std::move(field1_value) },
		{ 2, arc::DataType::FLOAT32, std::move(field2_value) }
	};
	struct_data.alignment = 8;
	struct_data.name = 123;

	data.set<decltype(struct_data), arc::DataType::STRUCT>(std::move(struct_data));
	EXPECT_EQ(data.type(), arc::DataType::STRUCT);

	auto &retrieved = data.get<arc::DataType::STRUCT>();
	EXPECT_EQ(retrieved.fields.size(), 2);

	EXPECT_EQ(std::get<0>(retrieved.fields[0]), 1);
	EXPECT_EQ(std::get<1>(retrieved.fields[0]), arc::DataType::INT32);
	EXPECT_EQ(std::get<2>(retrieved.fields[0]).get<arc::DataType::INT32>(), 42);

	EXPECT_EQ(std::get<0>(retrieved.fields[1]), 2);
	EXPECT_EQ(std::get<1>(retrieved.fields[1]), arc::DataType::FLOAT32);
	EXPECT_FLOAT_EQ(std::get<2>(retrieved.fields[1]).get<arc::DataType::FLOAT32>(), 3.14f);

	EXPECT_EQ(retrieved.alignment, 8);
	EXPECT_EQ(retrieved.name, 123);
}

TEST_F(TypedDataFixture, SelfReferenceStruct)
{
	arc::DataTraits<arc::DataType::STRUCT>::value node_struct;

	arc::TypedData next_field_type;
	arc::DataTraits<arc::DataType::POINTER>::value ptr_type;
	ptr_type.pointee = nullptr;
	ptr_type.addr_space = 0;
	next_field_type.set<decltype(ptr_type), arc::DataType::POINTER>(std::move(ptr_type));

	arc::TypedData value_field_type;

	node_struct.fields = arc::u8slice<std::tuple<arc::StringTable::StringId, arc::DataType, arc::TypedData> > {
		{ 1, arc::DataType::POINTER, std::move(next_field_type) },
		{ 2, arc::DataType::INT32, std::move(value_field_type) }
	};
	node_struct.alignment = 8;
	node_struct.name = 99;

	data.set<decltype(node_struct), arc::DataType::STRUCT>(std::move(node_struct));
	EXPECT_EQ(data.type(), arc::DataType::STRUCT);

	auto &retrieved = data.get<arc::DataType::STRUCT>();
	EXPECT_EQ(retrieved.fields.size(), 2);
	EXPECT_EQ(retrieved.name, 99);

	EXPECT_EQ(std::get<0>(retrieved.fields[0]), 1);
	EXPECT_EQ(std::get<1>(retrieved.fields[0]), arc::DataType::POINTER);
	EXPECT_EQ(std::get<2>(retrieved.fields[0]).type(), arc::DataType::POINTER);

	EXPECT_EQ(std::get<0>(retrieved.fields[1]), 2);
	EXPECT_EQ(std::get<1>(retrieved.fields[1]), arc::DataType::INT32);
	EXPECT_EQ(std::get<2>(retrieved.fields[1]).type(), arc::DataType::VOID);
}

TEST_F(TypedDataFixture, NestedStructInstance)
{
	arc::TypedData street_val, zip_val;
	street_val.set<arc::StringTable::StringId, arc::DataType::STRING>(123);
	zip_val.set<int, arc::DataType::INT32>(12345);

	arc::DataTraits<arc::DataType::STRUCT>::value addr_struct;
	addr_struct.fields = arc::u8slice<std::tuple<arc::StringTable::StringId, arc::DataType, arc::TypedData> > {
		{ 10, arc::DataType::STRING, std::move(street_val) },
		{ 11, arc::DataType::INT32, std::move(zip_val) }
	};
	addr_struct.alignment = 4;
	addr_struct.name = 50;

	arc::TypedData addr_val, age_val;
	addr_val.set<decltype(addr_struct), arc::DataType::STRUCT>(std::move(addr_struct));
	age_val.set<int, arc::DataType::INT32>(30);

	arc::DataTraits<arc::DataType::STRUCT>::value person_struct;
	person_struct.fields = arc::u8slice<std::tuple<arc::StringTable::StringId, arc::DataType, arc::TypedData> > {
		{ 20, arc::DataType::STRUCT, std::move(addr_val) },
		{ 21, arc::DataType::INT32, std::move(age_val) }
	};
	person_struct.alignment = 8;
	person_struct.name = 60;

	data.set<decltype(person_struct), arc::DataType::STRUCT>(std::move(person_struct));
	EXPECT_EQ(data.type(), arc::DataType::STRUCT);

	auto &person = data.get<arc::DataType::STRUCT>();
	EXPECT_EQ(person.fields.size(), 2);
	EXPECT_EQ(person.name, 60);

	EXPECT_EQ(std::get<1>(person.fields[0]), arc::DataType::STRUCT);
	auto &nested_addr = std::get<2>(person.fields[0]).get<arc::DataType::STRUCT>();
	EXPECT_EQ(nested_addr.name, 50);
	EXPECT_EQ(nested_addr.fields.size(), 2);

	EXPECT_EQ(std::get<2>(nested_addr.fields[0]).get<arc::DataType::STRING>(), 123);
	EXPECT_EQ(std::get<2>(nested_addr.fields[1]).get<arc::DataType::INT32>(), 12345);

	EXPECT_EQ(std::get<1>(person.fields[1]), arc::DataType::INT32);
	EXPECT_EQ(std::get<2>(person.fields[1]).get<arc::DataType::INT32>(), 30);
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
	arc::Node *elem1 = reinterpret_cast<arc::Node *>(0x1000);
	arc::Node *elem2 = reinterpret_cast<arc::Node *>(0x2000);

	arc::DataTraits<arc::DataType::ARRAY>::value arr_data;
	arr_data.elements = arc::u16slice<arc::Node *> { elem1, elem2 };
	arr_data.elem_type = arc::DataType::INT32;

	data.set<decltype(arr_data), arc::DataType::ARRAY>(std::move(arr_data));

	arc::TypedData moved(std::move(data));
	EXPECT_EQ(moved.type(), arc::DataType::ARRAY);

	auto &moved_array = moved.get<arc::DataType::ARRAY>();
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
	ptr_data.pointee = reinterpret_cast<arc::Node *>(0x12345678);
	ptr_data.addr_space = 2;

	data.set<decltype(ptr_data), arc::DataType::POINTER>(ptr_data);

	arc::TypedData other = std::move(data);

	EXPECT_EQ(other.type(), arc::DataType::POINTER);
	auto &other_ptr = other.get<arc::DataType::POINTER>();
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

	auto &void_val = data.get<arc::DataType::VOID>();

	auto &void_val2 = data.get<arc::DataType::VOID>();
	EXPECT_TRUE(void_val == void_val2);
}

TEST_F(TypedDataFixture, FunctionType)
{
	arc::DataTraits<arc::DataType::FUNCTION>::value func_data;

	data.set<decltype(func_data), arc::DataType::FUNCTION>(func_data);
	EXPECT_EQ(data.type(), arc::DataType::FUNCTION);

	[[maybe_unused]] auto &retrieved = data.get<arc::DataType::FUNCTION>();
	(void) retrieved;
}
