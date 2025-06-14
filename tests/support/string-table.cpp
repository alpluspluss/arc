/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <arc/support/string-table.hpp>
#include <gtest/gtest.h>

class StringTableFixture : public testing::Test
{
protected:
	void SetUp() override {}

	void TearDown() override
	{
		table.clear();
	}

	arc::StringTable table;
};

TEST_F(StringTableFixture, StringTableEmpty)
{
	EXPECT_EQ(table.intern(""), 0);
	EXPECT_EQ(table.size(), 1); /* empty string is always present */
}

TEST_F(StringTableFixture, StringTableIntern)
{
	/* same string should return the same id regardless. if
	 * the body is different then the string table should
	 * return a different number */
	EXPECT_EQ(table.intern("test1"), 1);
	EXPECT_EQ(table.intern("test1"), 1);
	EXPECT_EQ(table.intern("test2"), 2);
	EXPECT_EQ(table.intern("test3"), 3);
}

TEST_F(StringTableFixture, GetReturnsCorrectString)
{
	auto id1 = table.intern("hello");
	auto id2 = table.intern("world");

	EXPECT_EQ(table.get(id1), "hello");
	EXPECT_EQ(table.get(id2), "world");
	EXPECT_EQ(table.get(0), "");

	EXPECT_THROW((void)table.get(999), std::out_of_range);
}

TEST_F(StringTableFixture, ContainsWorks)
{
	table.intern("test");
	EXPECT_TRUE(table.contains("test"));
	EXPECT_FALSE(table.contains("test1"));
}

TEST_F(StringTableFixture, ClearResetsTable)
{
	table.intern("one");
	table.intern("two");

	table.clear();

	EXPECT_EQ(table.size(), 1);
	EXPECT_FALSE(table.contains("one"));
	EXPECT_FALSE(table.contains("two"));
	EXPECT_EQ(table.intern(""), 0);
}
