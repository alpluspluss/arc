/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace arc
{
	/**
	 * @brief A string table to intern strings.
	 */
	class StringTable
	{
	public:
		using StringId = std::uint32_t;
		static constexpr StringId INVALID_STRING_ID = std::numeric_limits<StringId>::max();

		/**
		 * @brief Constructs a new StringTable.
		 */
		StringTable();

		/**
		 * @brief Destroys the StringTable.
		 */
		~StringTable();

		/**
		 * @param str String to intern.
		 * @return String index. Invalid if equal `INVALID_STRING_ID`.
		 */
		StringId intern(std::string_view str);

		/**
		 * @param id String index to retrieve.
		 * @return Interned string. Empty if `id` is equal to `INVALID_STRING_ID`.
		 */
		[[nodiscard]] std::string_view get(StringId id) const;

		/**
		 * @param str String to find
		 * @return `true` if found otherwise `false`
		 */
		[[nodiscard]] bool contains(std::string_view str) const;

		/**
		 * @return Current size of the table
		 */
		[[nodiscard]] std::size_t size() const;

		/**
		 * @brief Clear all data stored inside the table
		 */
		void clear();

	private:
		/* for string_view hash. it is required as we want to do heterogeneous lookup*/
		struct StringHash
		{
			using is_transparent = void;
			std::size_t operator()(const std::string_view sv) const
			{
				return std::hash<std::string_view>{}(sv);
			}
		};

		/* for comparing strings heterogeneously */
		struct StringEqual
		{
			using is_transparent = void;
			bool operator()(const std::string_view lhs, const std::string_view rhs) const
			{
				return lhs == rhs;
			}
		};

		std::unordered_map<std::string, StringId, StringHash, StringEqual> table;
		std::vector<std::string_view> strs;
		StringId next_id;
	};
}
