/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/foundation/module.hpp>
#include <arc/foundation/region.hpp>
#include <gtest/gtest.h>

#define REGION_TEST_NAME "test"

class RegionFixture : public ::testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("module");
		root = module->create_region(REGION_TEST_NAME);
	}

	void TearDown() override {}

	std::unique_ptr<arc::Module> module;
	arc::Region* root = nullptr;
};

TEST_F(RegionFixture, BasicProperties)
{
	EXPECT_EQ(root->name(), REGION_TEST_NAME);
	EXPECT_EQ(&root->module(), module.get());
	EXPECT_EQ(root->parent(), module->root());
	EXPECT_FALSE(root->nodes().empty()); /* ENTRY is default constructed */
	EXPECT_TRUE(root->children().empty());
}

TEST_F(RegionFixture, NameInterning)
{
	/* technically this should error in production anyway due to multiple same definitions */
	const auto* region2 = module->create_region(REGION_TEST_NAME);
	EXPECT_EQ(root->name(), region2->name());
}

TEST_F(RegionFixture, HierarchyManagement)
{
	auto* child = module->create_region("child", root);
	EXPECT_EQ(child->parent(), root);
	EXPECT_TRUE(std::ranges::find(root->children(), child) != root->children().end());
	EXPECT_EQ(root->children().size(), 1);
}

TEST_F(RegionFixture, MultipleChildren)
{
	auto* child1 = module->create_region("child1", root);
	auto* child2 = module->create_region("child2", root);

	EXPECT_EQ(root->children().size(), 2);
	EXPECT_TRUE(std::ranges::find(root->children(), child1) != root->children().end());
	EXPECT_TRUE(std::ranges::find(root->children(), child2) != root->children().end());
}

TEST_F(RegionFixture, DominanceAnalysisViaTree)
{
	auto* child = module->create_region("child", root);
	auto* grandchild = module->create_region("grandchild", child);

	/* parent always dominates children */
	EXPECT_TRUE(root->dominates_via_tree(child));
	EXPECT_TRUE(root->dominates_via_tree(grandchild));
	EXPECT_TRUE(child->dominates_via_tree(grandchild));

	/* children never dominate their parents */
	EXPECT_FALSE(child->dominates_via_tree(root));
	EXPECT_FALSE(grandchild->dominates_via_tree(root));
	EXPECT_FALSE(grandchild->dominates_via_tree(child));

	/* self-dominance */
	EXPECT_TRUE(root->dominates_via_tree(root));
}

TEST_F(RegionFixture, SiblingsDontDominate)
{
	const auto* child1 = module->create_region("child1", root);
	const auto* child2 = module->create_region("child2", root);

	EXPECT_FALSE(child1->dominates_via_tree(child2));
	EXPECT_FALSE(child2->dominates_via_tree(child1));
}

TEST_F(RegionFixture, IsTerminatedEmpty)
{
	EXPECT_FALSE(root->is_terminated());
}
