/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/support/slice.hpp>
#include <gtest/gtest.h>
#include <string>
#include <algorithm>
#include <numeric>

class SliceFixture : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(SliceFixture, DefaultConstruction)
{
	arc::slice<int> s;

	EXPECT_TRUE(s.empty());
	EXPECT_EQ(s.size(), 0);
	EXPECT_EQ(s.capacity(), 0);
	EXPECT_EQ(s.data(), nullptr);
	EXPECT_EQ(s.begin(), s.end());
}

TEST_F(SliceFixture, SizeConstruction)
{
	arc::slice<int> s(5);

	EXPECT_FALSE(s.empty());
	EXPECT_EQ(s.size(), 5);
	EXPECT_GE(s.capacity(), 5);
	EXPECT_NE(s.data(), nullptr);

	for (int i: s)
	{
		EXPECT_EQ(i, 0);
	}
}

TEST_F(SliceFixture, SizeAndValueConstruction)
{
	arc::slice<int> s(5, 42);

	EXPECT_EQ(s.size(), 5);
	for (int i: s)
	{
		EXPECT_EQ(i, 42);
	}
}

TEST_F(SliceFixture, InitializerListConstruction)
{
	arc::slice<std::size_t> s { 1, 2, 3, 4, 5 };

	EXPECT_EQ(s.size(), 5);
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		EXPECT_EQ(static_cast<std::size_t>(s[i]), static_cast<std::size_t>(i + 1));
	}
}

TEST_F(SliceFixture, IteratorConstruction)
{
	std::vector<int> vec { 10, 20, 30 };
	arc::slice<int> s(vec.begin(), vec.end());

	EXPECT_EQ(s.size(), 3);
	EXPECT_EQ(s[0], 10);
	EXPECT_EQ(s[1], 20);
	EXPECT_EQ(s[2], 30);
}

TEST_F(SliceFixture, CopyConstruction)
{
	arc::slice<int> original { 1, 2, 3 };
	arc::slice<int> copy(original);

	EXPECT_EQ(original.size(), copy.size());
	EXPECT_TRUE(std::equal(original.begin(), original.end(), copy.begin()));

	original[0] = 99;
	EXPECT_EQ(copy[0], 1);
}

TEST_F(SliceFixture, MoveConstruction)
{
	arc::slice<int> original { 1, 2, 3 };
	auto original_data = original.data();
	auto original_size = original.size();

	arc::slice<int> moved(std::move(original));

	EXPECT_EQ(moved.size(), original_size);
	EXPECT_EQ(moved.data(), original_data);

	EXPECT_TRUE(original.empty());
	EXPECT_EQ(original.data(), nullptr);
}

TEST_F(SliceFixture, ElementAccess)
{
	arc::slice<int> s { 10, 20, 30, 40, 50 };

	EXPECT_EQ(s[0], 10);
	EXPECT_EQ(s[4], 50);

	EXPECT_EQ(s.at(2), 30);
	EXPECT_THROW(s.at(5), std::out_of_range);

	EXPECT_EQ(s.front(), 10);
	EXPECT_EQ(s.back(), 50);

	s[1] = 999;
	EXPECT_EQ(s[1], 999);
}

TEST_F(SliceFixture, Iterators)
{
	arc::slice<int> s { 1, 2, 3, 4, 5 };

	int expected = 1;
	for (auto it = s.begin(); it != s.end(); ++it)
	{
		EXPECT_EQ(*it, expected++);
	}

	expected = 1;
	for (const auto &value: s)
	{
		EXPECT_EQ(value, expected++);
	}

	expected = 5;
	for (auto it = s.rbegin(); it != s.rend(); ++it)
	{
		EXPECT_EQ(*it, expected--);
	}
}

TEST_F(SliceFixture, Capacity)
{
	arc::slice<int> s;

	EXPECT_EQ(s.capacity(), 0);

	s.reserve(10);
	EXPECT_GE(s.capacity(), 10);
	EXPECT_EQ(s.size(), 0);

	s.resize(5);
	EXPECT_EQ(s.size(), 5);

	s.shrink_to_fit();
	EXPECT_EQ(s.capacity(), s.size());
}

TEST_F(SliceFixture, Modifiers)
{
	arc::slice<int> s;

	s.push_back(10);
	s.push_back(20);
	EXPECT_EQ(s.size(), 2);
	EXPECT_EQ(s[0], 10);
	EXPECT_EQ(s[1], 20);

	s.emplace_back(30);
	EXPECT_EQ(s.size(), 3);
	EXPECT_EQ(s[2], 30);

	s.pop_back();
	EXPECT_EQ(s.size(), 2);
	EXPECT_EQ(s.back(), 20);

	s.clear();
	EXPECT_TRUE(s.empty());
	EXPECT_EQ(s.size(), 0);
}

TEST_F(SliceFixture, Insert)
{
	arc::slice<int> s { 1, 2, 3 };

	auto it = s.insert(s.begin() + 1, 99);
	EXPECT_EQ(s.size(), 4);
	EXPECT_EQ(s[1], 99);
	EXPECT_EQ(*it, 99);

	s.insert(s.end(), 2, 88);
	EXPECT_EQ(s.size(), 6);
	EXPECT_EQ(s[4], 88);
	EXPECT_EQ(s[5], 88);

	std::vector<int> vec { 77, 66 };
	s.insert(s.begin(), vec.begin(), vec.end());
	EXPECT_EQ(s.size(), 8);
	EXPECT_EQ(s[0], 77);
	EXPECT_EQ(s[1], 66);
}

TEST_F(SliceFixture, Erase)
{
	arc::slice<int> s { 1, 2, 3, 4, 5 };

	auto it = s.erase(s.begin() + 2); /* remove 3 */
	EXPECT_EQ(s.size(), 4);
	EXPECT_EQ(s[2], 4); /* 4 moved to position 2 */
	EXPECT_EQ(*it, 4);

	s.erase(s.begin() + 1, s.begin() + 3);
	EXPECT_EQ(s.size(), 2);
	EXPECT_EQ(s[0], 1);
	EXPECT_EQ(s[1], 5);
}

TEST_F(SliceFixture, Resize)
{
	arc::slice<int> s { 1, 2, 3 };

	s.resize(5);
	EXPECT_EQ(s.size(), 5);
	EXPECT_EQ(s[0], 1);
	EXPECT_EQ(s[3], 0);

	s.resize(7, 99);
	EXPECT_EQ(s.size(), 7);
	EXPECT_EQ(s[5], 99);
	EXPECT_EQ(s[6], 99);

	s.resize(2);
	EXPECT_EQ(s.size(), 2);
	EXPECT_EQ(s[0], 1);
	EXPECT_EQ(s[1], 2);
}

TEST_F(SliceFixture, Assignment)
{
	arc::slice<int> s1 { 1, 2, 3 };
	arc::slice<int> s2;

	s2 = s1;
	EXPECT_EQ(s1.size(), s2.size());
	EXPECT_TRUE(std::equal(s1.begin(), s1.end(), s2.begin()));

	s2 = { 10, 20, 30, 40 };
	EXPECT_EQ(s2.size(), 4);
	EXPECT_EQ(s2[1], 20);

	arc::slice<int> s3;
	auto s2_data = s2.data();
	s3 = std::move(s2);
	EXPECT_EQ(s3.data(), s2_data);
	EXPECT_TRUE(s2.empty());
}

TEST_F(SliceFixture, Assign)
{
	arc::slice<int> s;

	s.assign(3, 42);
	EXPECT_EQ(s.size(), 3);
	EXPECT_TRUE(std::ranges::all_of(s, [](int x) { return x == 42; }));

	std::vector<int> vec { 10, 20, 30 };
	s.assign(vec.begin(), vec.end());
	EXPECT_EQ(s.size(), 3);
	EXPECT_EQ(s[1], 20);

	s.assign({ 100, 200, 300, 400 });
	EXPECT_EQ(s.size(), 4);
	EXPECT_EQ(s[2], 300);
}

TEST_F(SliceFixture, Comparison)
{
	arc::slice<int> s1 { 1, 2, 3 };
	arc::slice<int> s2 { 1, 2, 3 };
	arc::slice<int> s3 { 1, 2, 4 };

	EXPECT_TRUE(s1 == s2);
	EXPECT_FALSE(s1 == s3);
	EXPECT_FALSE(s1 != s2);
	EXPECT_TRUE(s1 != s3);
}

TEST_F(SliceFixture, STLAlgorithms)
{
	arc::slice<int> s { 5, 2, 8, 1, 9 };

	std::ranges::sort(s);
	EXPECT_TRUE(std::ranges::is_sorted(s.begin(), s.end()));

	auto it = std::ranges::find(s, 8);
	EXPECT_NE(it, s.end());
	EXPECT_EQ(*it, 8);

	int sum = std::accumulate(s.begin(), s.end(), 0);
	EXPECT_EQ(sum, 25); /* 1+2+5+8+9 */
}

TEST_F(SliceFixture, SizeTypes)
{
	const arc::u8slice<int> small_slice;
	const arc::u16slice<int> medium_slice;
	const arc::u32slice<int> large_slice;

	EXPECT_EQ(sizeof(small_slice), 10);
	EXPECT_EQ(sizeof(medium_slice), 12);
	EXPECT_EQ(sizeof(large_slice), 16);

	EXPECT_EQ(small_slice.max_size(), 255);
	EXPECT_EQ(medium_slice.max_size(), 65535);
	EXPECT_EQ(large_slice.max_size(), 4294967295U);
}

TEST_F(SliceFixture, ComplexType)
{
	struct ComplexType
	{
		std::string name;
		int value;

		ComplexType(std::string n, int v) : name(std::move(n)), value(v) {}

		bool operator==(const ComplexType &other) const
		{
			return name == other.name && value == other.value;
		}
	};

	arc::slice<ComplexType> s;

	s.emplace_back("test", 42);
	s.emplace_back("hello", 100);

	EXPECT_EQ(s.size(), 2);
	EXPECT_EQ(s[0].name, "test");
	EXPECT_EQ(s[0].value, 42);
	EXPECT_EQ(s[1].name, "hello");
	EXPECT_EQ(s[1].value, 100);
}

TEST_F(SliceFixture, ExceptionSafety)
{
	arc::slice<std::string> s;
	try
	{
		s.at(10);
		FAIL() << "expected std::out_of_range";
	}
	catch (const std::out_of_range &)
	{
		EXPECT_TRUE(s.empty());
		s.push_back("test");
		EXPECT_EQ(s.size(), 1);
	}
}

TEST_F(SliceFixture, DeductionGuides)
{
	std::vector<int> vec { 1, 2, 3 };

	auto s1 = arc::slice(vec.begin(), vec.end());
	static_assert(std::is_same_v<decltype(s1), arc::slice<int> >);
	EXPECT_EQ(s1.size(), 3);

	auto s2 = arc::slice { 1, 2, 3, 4 };
	static_assert(std::is_same_v<decltype(s2), arc::slice<int> >);
	EXPECT_EQ(s2.size(), 4);
}
