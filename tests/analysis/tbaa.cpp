/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <print>
#include <arc/analysis/tbaa.hpp>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <arc/foundation/pass-manager.hpp>
#include <arc/support/dump.hpp>
#include <gtest/gtest.h>

class TBAAFixture : public testing::Test
{
protected:
	void SetUp() override
	{
		module = std::make_unique<arc::Module>("tbaa_test_module");
		builder = std::make_unique<arc::Builder>(*module);
		pass_manager = std::make_unique<arc::PassManager>();
		pass_manager->add<arc::TypeBasedAliasAnalysisPass>();
	}

	void TearDown() override
	{
		pass_manager.reset();
		builder.reset();
		module.reset();
	}

	std::unique_ptr<arc::Module> module;
	std::unique_ptr<arc::Builder> builder;
	std::unique_ptr<arc::PassManager> pass_manager;

	const arc::TypeBasedAliasResult& run_tbaa()
	{
		pass_manager->run(*module);
		return pass_manager->get<arc::TypeBasedAliasResult>();
	}
};

TEST_F(TBAAFixture, BasicAllocationSiteTracking)
{
	arc::Node* store1_node = nullptr;
	arc::Node* store2_node = nullptr;

	builder->function<arc::DataType::VOID>("test_basic_alloc")
		.body([&](arc::Builder& fb)
		{
			auto* alloc1 = fb.alloc<arc::DataType::INT32>(fb.lit(1));
			auto* alloc2 = fb.alloc<arc::DataType::INT32>(fb.lit(1));

			store1_node = fb.store(fb.lit(42), alloc1);
			store2_node = fb.store(fb.lit(99), alloc2);
			[[maybe_unused]] auto* load1 = fb.load(alloc1);
			[[maybe_unused]] auto* load2 = fb.load(alloc2);

			return fb.ret();
		});

	std::println("before TBAA");
	dump(*module);

	auto& tbaa = run_tbaa();

	std::println("\nafter TBAA");
	arc::dump(*module);

	/* different allocations should not alias */
	EXPECT_EQ(tbaa.alias(store1_node, store2_node), arc::TBAAResult::NO_ALIAS);

	std::println("basic allocation site tracking: passed");
}

TEST_F(TBAAFixture, SameAllocationMustAlias)
{
	arc::Node* store_node = nullptr;
	arc::Node* load_node = nullptr;

	builder->function<arc::DataType::VOID>("test_same_alloc")
		.body([&](arc::Builder& fb)
		{
			auto* alloc = fb.alloc<arc::DataType::INT32>(fb.lit(1));

			store_node = fb.store(fb.lit(42), alloc);
			[[maybe_unused]] auto* store2 = fb.store(fb.lit(99), alloc);
			load_node = fb.load(alloc);

			return fb.ret();
		});

	auto& tbaa = run_tbaa();

	/* same allocation should must alias */
	EXPECT_EQ(tbaa.alias(store_node, load_node), arc::TBAAResult::MUST_ALIAS);

	std::println("same allocation must alias: passed");
}

TEST_F(TBAAFixture, FunctionCallAllocationSites)
{
	arc::Node* store1_node = nullptr;
	arc::Node* store2_node = nullptr;

	/* some dumb malloc idk lol */
	auto* malloc_func = builder->function<arc::DataType::POINTER>("malloc")
		.param<arc::DataType::UINT64>("size")
		.body([](arc::Builder& fb, arc::Node* size)
		{
			auto* fake_alloc = fb.alloc<arc::DataType::INT8>(size);
			auto* ptr = fb.addr_of(fake_alloc);
			return fb.ret(ptr);
		});

	builder->function<arc::DataType::VOID>("test_call_alloc")
		.body([&](arc::Builder& fb)
		{
			auto* ptr1 = fb.call(malloc_func, { fb.lit(static_cast<std::uint64_t>(64)) });
			auto* ptr2 = fb.call(malloc_func, { fb.lit(static_cast<std::uint64_t>(32)) });

			store1_node = fb.ptr_store(fb.lit(42), ptr1);
			store2_node = fb.ptr_store(fb.lit(99), ptr2);

			return fb.ret();
		});

	auto& tbaa = run_tbaa();

	/* different call sites should not alias */
	EXPECT_EQ(tbaa.alias(store1_node, store2_node), arc::TBAAResult::NO_ALIAS);

	std::println("function call allocation sites: passed");
}

TEST_F(TBAAFixture, TypeBasedNoAlias)
{
	arc::Node* int_store = nullptr;
	arc::Node* float_store = nullptr;

	builder->function<arc::DataType::VOID>("test_type_no_alias")
		.body([&](arc::Builder& fb)
		{
			auto* int_alloc = fb.alloc<arc::DataType::INT32>(fb.lit(1));
			auto* float_alloc = fb.alloc<arc::DataType::FLOAT32>(fb.lit(1));

			int_store = fb.store(fb.lit(static_cast<std::int32_t>(42)), int_alloc);
			float_store = fb.store(fb.lit(3.14f), float_alloc);

			return fb.ret();
		});

	auto& tbaa = run_tbaa();

	/* different types should not alias */
	EXPECT_EQ(tbaa.alias(int_store, float_store), arc::TBAAResult::NO_ALIAS);

	std::println("type-based no alias: passed");
}

TEST_F(TBAAFixture, EscapeAnalysisBasic)
{
	arc::Node* local1_alloc = nullptr;
	arc::Node* local2_alloc = nullptr;

	auto* escape_func = builder->function<arc::DataType::VOID>("escape_func")
		.param<arc::DataType::POINTER>("ptr")
		.body([](arc::Builder& fb, arc::Node* ptr)
		{
			/* function that receives pointer causes escape */
			return fb.ret();
		});

	builder->function<arc::DataType::VOID>("test_escape")
		.body([&](arc::Builder& fb)
		{
			local1_alloc = fb.alloc<arc::DataType::INT32>(fb.lit(1));
			local2_alloc = fb.alloc<arc::DataType::INT32>(fb.lit(1));

			auto* ptr1 = fb.addr_of(local1_alloc);
			fb.call(escape_func, { ptr1 }); /* local1 escapes */

			[[maybe_unused]] auto* store1 = fb.store(fb.lit(42), local1_alloc);
			[[maybe_unused]] auto* store2 = fb.store(fb.lit(99), local2_alloc);

			return fb.ret();
		});

	auto& tbaa = run_tbaa();

	/* check that local1 escaped but local2 didn't */
	EXPECT_TRUE(tbaa.has_escaped(local1_alloc));
	EXPECT_FALSE(tbaa.has_escaped(local2_alloc));

	std::println("escape analysis basic: passed");
}

TEST_F(TBAAFixture, ReturnEscape)
{
	arc::Node* alloc_node = nullptr;

	builder->function<arc::DataType::POINTER>("test_return_escape")
		.body([&](arc::Builder& fb)
		{
			alloc_node = fb.alloc<arc::DataType::INT32>(fb.lit(1));
			auto* ptr = fb.addr_of(alloc_node);
			return fb.ret(ptr); /* returning pointer causes escape */
		});

	auto& tbaa = run_tbaa();

	EXPECT_TRUE(tbaa.has_escaped(alloc_node));

	std::println("return escape: passed");
}

TEST_F(TBAAFixture, PointerStoreEscape)
{
	arc::Node* local_alloc = nullptr;
	auto* global_ptr_storage = builder->alloc<arc::DataType::POINTER>(builder->lit(1));
	builder->function<arc::DataType::VOID>("test_ptr_store_escape")
	.body([&](arc::Builder& fb)
	{
		local_alloc = fb.alloc<arc::DataType::INT32>(fb.lit(1));
		auto* ptr = fb.addr_of(local_alloc);

		std::println("local_alloc type: {}", static_cast<int>(local_alloc->type_kind));
		std::println("ptr type: {} (POINTER={})",
					 static_cast<int>(ptr->type_kind),
					 static_cast<int>(arc::DataType::POINTER));

		[[maybe_unused]] auto* store = fb.store(ptr, global_ptr_storage);
		return fb.ret();
	});

	auto& tbaa = run_tbaa();

	EXPECT_TRUE(tbaa.has_escaped(local_alloc));

	std::println("pointer store escape: passed");
}

TEST_F(TBAAFixture, NonEscapedLocalsNoAlias)
{
	arc::Node* store1_node = nullptr;
	arc::Node* store2_node = nullptr;

	builder->function<arc::DataType::VOID>("test_non_escaped")
		.body([&](arc::Builder& fb)
		{
			auto* local1 = fb.alloc<arc::DataType::INT32>(fb.lit(1));
			auto* local2 = fb.alloc<arc::DataType::INT32>(fb.lit(1));

			/* both locals stay local */
			store1_node = fb.store(fb.lit(42), local1);
			store2_node = fb.store(fb.lit(99), local2);
			[[maybe_unused]] auto* load1 = fb.load(local1);
			[[maybe_unused]] auto* load2 = fb.load(local2);

			return fb.ret();
		});

	auto& tbaa = run_tbaa();

	/* different non-escaped locals should not alias */
	EXPECT_EQ(tbaa.alias(store1_node, store2_node), arc::TBAAResult::NO_ALIAS);

	std::println("non-escaped locals no alias: passed");
}

TEST_F(TBAAFixture, PointerArithmeticTracking)
{
	arc::Node* store1_node = nullptr;
	arc::Node* store2_node = nullptr;
	arc::Node* store3_node = nullptr;

	builder->function<arc::DataType::VOID>("test_ptr_arithmetic")
		.body([&](arc::Builder& fb)
		{
			auto* base = fb.alloc<arc::DataType::INT32>(fb.lit(4)); /* array of 4 ints */
			auto* ptr = fb.addr_of(base);

			/* have to do this because C++ evaluates expressions from left to right,
			 * so we need to create separate nodes for each offset */
			const auto n1 = fb.lit(0);
			const auto n2 = fb.lit(4);
			const auto n3 = fb.lit(8);
			auto* ptr_offset_0 = fb.ptr_add(ptr, n1);
			auto* ptr_offset_4 = fb.ptr_add(ptr, n2); /* next int */
			auto* ptr_offset_8 = fb.ptr_add(ptr, n3); /* next int */

			store1_node = fb.ptr_store(fb.lit(10), ptr_offset_0);
			store2_node = fb.ptr_store(fb.lit(20), ptr_offset_4);
			store3_node = fb.ptr_store(fb.lit(30), ptr_offset_8);

			return fb.ret();
		});

	auto& tbaa = run_tbaa();

	/* different offsets should not alias */
	EXPECT_EQ(tbaa.alias(store1_node, store2_node), arc::TBAAResult::NO_ALIAS);
	EXPECT_EQ(tbaa.alias(store2_node, store3_node), arc::TBAAResult::NO_ALIAS);
	EXPECT_EQ(tbaa.alias(store1_node, store3_node), arc::TBAAResult::NO_ALIAS);

	std::println("pointer arithmetic tracking: passed");
}
