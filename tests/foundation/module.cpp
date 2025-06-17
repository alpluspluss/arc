/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/foundation/module.hpp>
#include <arc/foundation/region.hpp>
#include <gtest/gtest.h>

class ModuleFixture : public ::testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("test_module");
	}

	void TearDown() override {}

	std::unique_ptr<arc::Module> module;
};

TEST_F(ModuleFixture, BasicProperties)
{
	EXPECT_EQ(module->name(), "test_module");
	EXPECT_NE(module->root(), nullptr);
	EXPECT_NE(module->rodata(), nullptr);
	EXPECT_TRUE(module->functions().empty());
}

TEST_F(ModuleFixture, RootAndRodataRegions)
{
	auto* root = module->root();
	auto* rodata = module->rodata();

	EXPECT_EQ(root->name(), ".__global");
	EXPECT_EQ(rodata->name(), ".__rodata");
	EXPECT_EQ(&root->module(), module.get());
	EXPECT_EQ(&rodata->module(), module.get());

	EXPECT_EQ(root->parent(), nullptr);
	EXPECT_EQ(rodata->parent(), nullptr);
}

TEST_F(ModuleFixture, StringInterning)
{
	const auto id1 = module->intern_str("hello");
	const auto id2 = module->intern_str("world");
	const auto id3 = module->intern_str("hello");

	EXPECT_NE(id1, id2); /* diff */
	EXPECT_EQ(id1, id3); /* same */

	EXPECT_EQ(module->strtable().get(id1), "hello");
	EXPECT_EQ(module->strtable().get(id2), "world");
}

TEST_F(ModuleFixture, EmptyStringInterning)
{
	auto empty_id1 = module->intern_str("");
	auto empty_id2 = module->intern_str("");

	EXPECT_EQ(empty_id1, empty_id2);
	EXPECT_EQ(module->strtable().get(empty_id1), "");
}

TEST_F(ModuleFixture, RegionCreation)
{
	auto* region1 = module->create_region("foo");
	auto* region2 = module->create_region("bar");

	EXPECT_NE(region1, nullptr);
	EXPECT_NE(region2, nullptr);
	EXPECT_NE(region1, region2);

	EXPECT_EQ(region1->name(), "foo");
	EXPECT_EQ(region2->name(), "bar");

	EXPECT_EQ(region1->parent(), module->root());
	EXPECT_EQ(region2->parent(), module->root());
}
