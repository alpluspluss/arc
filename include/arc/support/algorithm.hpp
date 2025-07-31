/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <algorithm>
#include <arc/support/slice.hpp>

namespace arc
{
	/**
	 * @brief Remove all occurrences of a value from a slice
	 * @tparam T Element type
	 * @tparam SizeType Size type of the slice
	 * @param slice Slice to remove from
	 * @param value Value to remove
	 * @return Number of elements removed
	 */
	template<typename T, typename SizeType>
	std::size_t erase(slice<T, SizeType>& slice, const T& value)
	{
		auto new_end = std::remove(slice.begin(), slice.end(), value);
		const std::size_t removed_count = std::distance(new_end, slice.end());
		slice.erase(new_end, slice.end());
		return removed_count;
	}
}
