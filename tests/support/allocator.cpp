/* this file is part of the Bloom Project; licensed under the MIT license. see LICENSE for more info */

#include <memory>
#include <string>
#include <vector>
#include <arc/support/allocator.hpp>
#include <gtest/gtest.h>

class AllocatorFixture : public ::testing::Test
{
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(AllocatorFixture, AllocatorState)
{
    [[maybe_unused]] constexpr auto alloc = ach::allocator<int>();
    static_assert(sizeof(alloc) == 1);
    SUCCEED();
}

TEST_F(AllocatorFixture, BasicAllocation)
{
    ach::allocator<int> alloc;

    int* ptr = alloc.allocate(1);
    ASSERT_NE(ptr, nullptr);

    *ptr = 42;
    EXPECT_EQ(*ptr, 42);

    alloc.deallocate(ptr, 1);
}

TEST_F(AllocatorFixture, ManySmallAllocations)
{
    constexpr size_t NUM_ALLOC = 1000;
    ach::allocator<int> alloc;

    std::vector<int*> pointers;
    pointers.reserve(NUM_ALLOC);

    for (size_t i = 0; i < NUM_ALLOC; ++i)
    {
        int* ptr = alloc.allocate(1);
        ASSERT_NE(ptr, nullptr);
        *ptr = static_cast<int>(i);
        pointers.push_back(ptr);
    }

    for (size_t i = 0; i < NUM_ALLOC; ++i)
    {
        EXPECT_EQ(*pointers[i], static_cast<int>(i));
    }

    for (const auto ptr : pointers)
        alloc.deallocate(ptr, 1);
}

TEST_F(AllocatorFixture, LargeAllocation)
{
    ach::allocator<char> alloc;

    constexpr size_t large_size = 2 * 1024 * 1024;
    char* large_ptr = alloc.allocate(large_size);
    ASSERT_NE(large_ptr, nullptr);

    for (size_t i = 0; i < large_size; ++i)
        large_ptr[i] = static_cast<char>(i % 256);

    for (size_t i = 0; i < large_size; i += 1024)
        EXPECT_EQ(large_ptr[i], static_cast<char>(i % 256));

    alloc.deallocate(large_ptr, large_size);
}

namespace
{
    struct ComplexType
    {
        std::string name;
        int value;
        double score;

        ComplexType(std::string  n, const int v, const double s) : name(std::move(n)), value(v), score(s) {}

        bool operator==(const ComplexType& other) const
        {
            return name == other.name && value == other.value && score == other.score;
        }
    };
}

TEST_F(AllocatorFixture, ComplexType)
{
    ach::allocator<ComplexType> alloc;

    ComplexType* ptr = alloc.allocate(1);
    ASSERT_NE(ptr, nullptr);

    alloc.construct(ptr, "test", 42, 3.14);

    EXPECT_EQ(ptr->name, "test");
    EXPECT_EQ(ptr->value, 42);
    EXPECT_EQ(ptr->score, 3.14);

    alloc.destroy(ptr);
    alloc.deallocate(ptr, 1);
}

TEST_F(AllocatorFixture, AllocatorRebind)
{
    constexpr ach::allocator<int> int_alloc;

    ach::allocator<int>::rebind<double>::other double_alloc(int_alloc);

    double* ptr = double_alloc.allocate(1);
    ASSERT_NE(ptr, nullptr);

    *ptr = 3.14159;
    EXPECT_DOUBLE_EQ(*ptr, 3.14159);

    double_alloc.deallocate(ptr, 1);
}

struct alignas(64) AlignedType
{
    double values[8];
};

TEST_F(AllocatorFixture, AlignmentTest)
{
    struct alignas(64) AlignedStorage
    {
        double data[8];
    };

    ach::allocator<AlignedStorage> alloc;
    AlignedStorage* ptr = alloc.allocate(1);
    ASSERT_NE(ptr, nullptr);

    const auto values = new (ptr) double[8];
    for (auto i = 0; i < 8; ++i)
        values[i] = i * 1.1;

    for (auto i = 0; i < 8; ++i)
    {
        EXPECT_DOUBLE_EQ(values[i], i * 1.1);
    }

    alloc.deallocate(ptr, 1);
}

TEST_F(AllocatorFixture, AllocationPattern)
{
    ach::allocator<int> alloc;
    std::vector<int*> pointers;

    for (auto i = 0; i < 100; ++i)
    {
        int* ptr = alloc.allocate(1);
        *ptr = i;
        pointers.push_back(ptr);
    }

    for (size_t i = 0; i < pointers.size(); i += 2)
    {
        alloc.deallocate(pointers[i], 1);
        pointers[i] = nullptr;
    }

    for (auto i = 0; i < 50; ++i)
    {
        int* ptr = alloc.allocate(1);
        *ptr = i + 100;
        pointers.push_back(ptr);
    }

    for (const auto ptr : pointers)
    {
        if (ptr != nullptr)
            alloc.deallocate(ptr, 1);
    }
}

TEST_F(AllocatorFixture, MaxSize)
{
    constexpr ach::allocator<char> char_alloc;
    constexpr ach::allocator<int> int_alloc;
    constexpr ach::allocator<double> double_alloc;

    EXPECT_EQ(char_alloc.max_size(), std::numeric_limits<size_t>::max() / sizeof(char));
    EXPECT_EQ(int_alloc.max_size(), std::numeric_limits<size_t>::max() / sizeof(int));
    EXPECT_EQ(double_alloc.max_size(), std::numeric_limits<size_t>::max() / sizeof(double));
}

TEST_F(AllocatorFixture, SharedState)
{
    /* both allocators should use the same pool regardless of instances;
     * as long as they use the same thread-local storage */
    ach::allocator<int> alloc1;
    ach::allocator<int> alloc2;

    int* ptr = alloc1.allocate(1);
    *ptr = 42;

    alloc2.deallocate(ptr, 1);
}
