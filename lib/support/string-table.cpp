/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <stdexcept>
#include <string_view>
#include <arc/support/string-table.hpp>

namespace arc
{
	StringTable::StringTable() : next_id(1)
	{
		/* all empty strings are the same so we set it to zero
		 * then increments `next_id` to 1 */
		auto [it, inserted] = table.emplace("", 0);
		strs.emplace_back(it->first);
	}

	StringTable::~StringTable() = default;

	StringTable::StringId StringTable::intern(std::string_view str)
	{
		/* all empty strings are the same
		 * as noted in the constructor */
		if (str.empty())
			return 0;

		/* return the id if the string already exists
		 * to be efficient memory-wise */
		if (const auto it = table.find(str);
			it != table.end())
		{
			return it->second;
		}

		const StringId nid = next_id++;
		auto [it, inserted] = table.emplace(str, nid);
		strs.emplace_back(it->first);
		return nid;
	}

	std::string_view StringTable::get(const StringId id) const
	{
		if (id >= strs.size())
			throw std::out_of_range("StringTable::get: invalid string id" + std::to_string(id));
		return strs[id];
	}

	bool StringTable::contains(const std::string_view str) const
	{
		return table.contains(str);
	}

	std::size_t StringTable::size() const
	{
		return strs.size();
	}

	void StringTable::clear()
	{
		/* clear all data stored inside the table;
		 * this is useful for reusing the table */
		strs.clear();
		table.clear();
		auto [it, inserted] = table.emplace("", 0);
		strs.emplace_back(it->first);
		next_id = 1;
	}
}
