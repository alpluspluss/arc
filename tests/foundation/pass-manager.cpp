/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/foundation/region.hpp>
#include <gtest/gtest.h>

class PassManagerFixture : public ::testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("test_module");
		pm = std::make_unique<arc::PassManager>();
		execution_order.clear();
	}

	void TearDown() override {}

	std::unique_ptr<arc::Module> module;
	std::unique_ptr<arc::PassManager> pm;
	static inline std::vector<std::string> execution_order;

	friend class MockAnalysisPass;
	friend class DependentAnalysisPass;
	friend class MockTransformPass;
	friend class SimpleTransformPass;
};

class MockAnalysisResult final : public arc::Analysis
{
public:
	int computation_result = 42;
	bool was_updated = false;

	bool update(const std::vector<arc::Region *> &modified_regions) override
	{
		was_updated = true;
		computation_result += static_cast<int>(modified_regions.size());
		return true;
	}
};

class MockAnalysisPass final : public arc::AnalysisPass
{
public:
	[[nodiscard]] std::string name() const override
	{
		return "mock-analysis";
	}

	arc::Analysis *run(const arc::Module &module) override
	{
		PassManagerFixture::execution_order.push_back(name());

		ach::allocator<MockAnalysisResult> alloc;
		auto *result = alloc.allocate(1);
		std::construct_at(result);
		return result;
	}
};

class DependentAnalysisResult : public arc::Analysis
{
public:
	int value = 100;

	bool update(const std::vector<arc::Region *> &modified_regions) override
	{
		return false;
	}
};

class DependentAnalysisPass : public arc::AnalysisPass
{
public:
	[[nodiscard]] std::string name() const override
	{
		return "dependent-analysis";
	}

	[[nodiscard]] std::vector<std::string> require() const override
	{
		return { "mock-analysis" };
	}

	arc::Analysis *run(const arc::Module &module) override
	{
		PassManagerFixture::execution_order.push_back(name());

		ach::allocator<DependentAnalysisResult> alloc;
		auto *result = alloc.allocate(1);
		std::construct_at(result);
		return result;
	}
};

class MockTransformPass : public arc::TransformPass
{
public:
	[[nodiscard]] std::string name() const override
	{
		return "mock-transform";
	}

	[[nodiscard]] std::vector<std::string> require() const override
	{
		return { "mock-analysis" };
	}

	[[nodiscard]] std::vector<std::string> invalidates() const override
	{
		return { "dependent-analysis", "mock-analysis" };
	}

	std::vector<arc::Region *> run(arc::Module &module, arc::PassManager &pm) override
	{
		PassManagerFixture::execution_order.push_back(name());

		const auto &analysis = pm.get<MockAnalysisResult>();
		EXPECT_EQ(analysis.computation_result, 42);

		std::vector<arc::Region *> modified;
		if (!module.root()->children().empty())
			modified.push_back(module.root()->children()[0]);
		return modified;
	}
};

class SimpleTransformPass final : public arc::TransformPass
{
public:
	[[nodiscard]] std::string name() const override
	{
		return "simple-transform";
	}

	std::vector<arc::Region *> run(arc::Module &module, arc::PassManager &pm) override
	{
		PassManagerFixture::execution_order.push_back(name());
		return {};
	}
};

TEST_F(PassManagerFixture, BasicPassExecution)
{
	pm->add<MockAnalysisPass>()
			.add<SimpleTransformPass>();

	EXPECT_EQ(pm->pass_count(), 2);

	pm->run(*module);

	EXPECT_EQ(execution_order.size(), 2);
	EXPECT_EQ(execution_order[0], "mock-analysis");
	EXPECT_EQ(execution_order[1], "simple-transform");

	EXPECT_TRUE(pm->has_analysis("mock-analysis"));
}

TEST_F(PassManagerFixture, DependencyResolution)
{
	pm->add<MockAnalysisPass>()
		.add<DependentAnalysisPass>();

	pm->run(*module);

	EXPECT_EQ(execution_order.size(), 2);
	EXPECT_EQ(execution_order[0], "mock-analysis");
	EXPECT_EQ(execution_order[1], "dependent-analysis");

	EXPECT_TRUE(pm->has_analysis("mock-analysis"));
	EXPECT_TRUE(pm->has_analysis("dependent-analysis"));
}

TEST_F(PassManagerFixture, MissingDependencyError)
{
	pm->add<DependentAnalysisPass>();

	EXPECT_THROW(pm->run(*module), std::runtime_error);
}

TEST_F(PassManagerFixture, AnalysisInvalidation)
{
	module->create_region("test_region");
	pm->add<MockAnalysisPass>()
			.add<DependentAnalysisPass>()
			.add<MockTransformPass>();

	pm->run(*module);
	EXPECT_TRUE(pm->has_analysis("mock-analysis"));
	EXPECT_FALSE(pm->has_analysis("dependent-analysis"));

	const auto &analysis = pm->get<MockAnalysisResult>();
	EXPECT_TRUE(analysis.was_updated);
	EXPECT_EQ(analysis.computation_result, 43); /* 42 + 1 modified region from `MockTransformPass` */
}

TEST_F(PassManagerFixture, AnalysisCaching)
{
	pm->add<MockAnalysisPass>()
			.add<MockAnalysisPass>(); /* add same analysis twice */

	pm->run(*module);
	EXPECT_EQ(std::ranges::count(execution_order, "mock-analysis"), 1);
}

TEST_F(PassManagerFixture, ClearAnalyses)
{
	pm->add<MockAnalysisPass>();
	pm->run(*module);

	EXPECT_TRUE(pm->has_analysis("mock-analysis"));

	pm->clear_analyses();

	EXPECT_FALSE(pm->has_analysis("mock-analysis"));
}

TEST_F(PassManagerFixture, GetAnalysisResult)
{
	pm->add<MockAnalysisPass>();
	pm->run(*module);

	const auto &result = pm->get<MockAnalysisResult>();
	EXPECT_EQ(result.computation_result, 42);
}

TEST_F(PassManagerFixture, GetMissingAnalysisThrows)
{
	EXPECT_THROW(pm->get<MockAnalysisResult>(), std::runtime_error);
}
