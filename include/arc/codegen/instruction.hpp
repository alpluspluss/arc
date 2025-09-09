/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <array>
#include <cstdint>

namespace arc
{
	/**
	 * @brief Concept defining requirements for target instruction types
	 */
	template<typename T>
	concept TargetInstruction = requires {
		typename T::Opcode;
		{ T::max_operands() } -> std::convertible_to<std::size_t>;
		{ T::encoding_size() } -> std::convertible_to<std::size_t>;
		std::is_enum_v<typename T::Opcode>;
	};

	/**
	 * @brief Generic operand representation for instructions
	 */
	struct Operand
	{
		enum class Type : std::uint8_t
		{
			NONE,
			REGISTER,
			IMMEDIATE,
			MEMORY,
			LABEL
		};

		Type type = Type::NONE;
		std::int64_t value = 0;
		std::uint8_t size = 0; /* operand size in bytes */

		constexpr Operand() = default;
		constexpr Operand(const Type t, std::int64_t v, std::uint8_t s = 0)
			: type(t), value(v), size(s) {}

		static constexpr Operand reg(std::uint32_t reg_id, std::uint8_t size = 8)
		{
			return { Type::REGISTER, reg_id, size };
		}

		static constexpr Operand imm(std::int64_t immediate, std::uint8_t size = 4)
		{
			return { Type::IMMEDIATE, immediate, size };
		}

		static constexpr Operand mem(std::uint32_t address, std::uint8_t size = 8)
		{
			return { Type::MEMORY, address, size };
		}

		static constexpr Operand label(std::uint32_t label_id)
		{
			return { Type::LABEL, label_id, 0 };
		}
	};

	/**
	 * @brief Generic instruction representation
	 * @tparam InstructionType Target-specific instruction struct
	 */
	template<TargetInstruction InstructionType>
	struct Instruction
	{
		using instruction_type = InstructionType;
		using opcode_type = typename InstructionType::Opcode;

		Instruction* prev = nullptr;
		Instruction* next = nullptr;
		opcode_type opcode;
		std::array<Operand, InstructionType::max_operands()> operands;
		bool marked_for_deletion = false;

		constexpr Instruction() = default;
		constexpr explicit Instruction(opcode_type op) : opcode(op) {}

		/**
		 * @brief Get the maximum number of operands for this instruction type
		 */
		static constexpr std::size_t max_operands()
		{
			return InstructionType::max_operands();
		}

		/**
		 * @brief Get the encoding size in bytes for this target
		 */
		static constexpr std::size_t encoding_size()
		{
			return InstructionType::encoding_size();
		}

		/**
		 * @brief Set operand at specified index
		 * @param index Operand index
		 * @param operand Operand to set
		 */
		void set_operand(std::size_t index, const Operand& operand)
		{
			if (index < operands.size())
				operands[index] = operand;
		}

		/**
		 * @brief Get operand at specified index
		 * @param index Operand index
		 * @return Reference to operand
		 */
		const Operand& get_operand(std::size_t index) const
		{
			static constexpr Operand empty{};
			return index < operands.size() ? operands[index] : empty;
		}

		/**
		 * @brief Mark instruction for deletion during cleanup
		 */
		void mark_deleted()
		{
			marked_for_deletion = true;
		}

		/**
		 * @brief Check if instruction is marked for deletion
		 */
		bool is_marked_for_deletion() const
		{
			return marked_for_deletion;
		}
	};

	/**
	 * @brief Intrusive doubly-linked list for efficient instruction management
	 * @tparam InstructionType Target-specific instruction struct
	 */
	template<TargetInstruction InstructionType>
	class InstructionList
	{
	public:
		using instruction_type = Instruction<InstructionType>;

	private:
		instruction_type* head = nullptr;
		instruction_type* tail = nullptr;
		std::size_t count = 0;

	public:
		/**
		 * @brief Append instruction to end of list
		 * @param insn Instruction to append
		 */
		void append(instruction_type* insn)
		{
			if (!insn)
				return;

			insn->next = nullptr;
			insn->prev = tail;

			if (tail)
				tail->next = insn;
			else
				head = insn;

			tail = insn;
			++count;
		}

		/**
		 * @brief Insert instruction after specified position
		 * @param pos Position to insert after (nullptr for beginning)
		 * @param insn Instruction to insert
		 */
		void insert_after(instruction_type* pos, instruction_type* insn)
		{
			if (!insn)
				return;

			if (!pos)
			{
				prepend(insn);
				return;
			}

			insn->next = pos->next;
			insn->prev = pos;

			if (pos->next)
				pos->next->prev = insn;
			else
				tail = insn;

			pos->next = insn;
			++count;
		}

		/**
		 * @brief Insert instruction before specified position
		 * @param pos Position to insert before (nullptr for end)
		 * @param insn Instruction to insert
		 */
		void insert_before(instruction_type* pos, instruction_type* insn)
		{
			if (!insn)
				return;

			if (!pos)
			{
				append(insn);
				return;
			}

			insn->next = pos;
			insn->prev = pos->prev;

			if (pos->prev)
				pos->prev->next = insn;
			else
				head = insn;

			pos->prev = insn;
			++count;
		}

		/**
		 * @brief Remove instruction from list
		 * @param insn Instruction to remove
		 */
		void remove(instruction_type* insn)
		{
			if (!insn)
				return;

			if (insn->prev)
				insn->prev->next = insn->next;
			else
				head = insn->next;

			if (insn->next)
				insn->next->prev = insn->prev;
			else
				tail = insn->prev;

			insn->prev = insn->next = nullptr;
			--count;
		}

		/**
		 * @brief Replace instruction with another
		 * @param old_insn Instruction to replace
		 * @param new_insn Replacement instruction
		 */
		void replace(instruction_type* old_insn, instruction_type* new_insn)
		{
			if (!old_insn || !new_insn)
				return;

			new_insn->prev = old_insn->prev;
			new_insn->next = old_insn->next;

			if (old_insn->prev)
				old_insn->prev->next = new_insn;
			else
				head = new_insn;

			if (old_insn->next)
				old_insn->next->prev = new_insn;
			else
				tail = new_insn;

			old_insn->prev = old_insn->next = nullptr;
		}

		/**
		 * @brief Remove all instructions marked for deletion
		 * @return Number of instructions removed
		 */
		std::size_t remove_marked()
		{
			std::size_t removed = 0;
			instruction_type* current = head;

			while (current)
			{
				instruction_type* next = current->next;
				if (current->is_marked_for_deletion())
				{
					remove(current);
					++removed;
				}
				current = next;
			}

			return removed;
		}

		/**
		 * @brief Get first instruction in list
		 */
		instruction_type* begin() const
		{
			return head;
		}

		/**
		 * @brief Get last instruction in list
		 */
		instruction_type* end() const
		{
			return nullptr;
		}

		/**
		 * @brief Get number of instructions in list
		 */
		std::size_t size() const
		{
			return count;
		}

		/**
		 * @brief Check if list is empty
		 */
		bool empty() const
		{
			return count == 0;
		}

		/**
		 * @brief Clear all instructions from list
		 */
		void clear()
		{
			head = tail = nullptr;
			count = 0;
		}

	private:
		void prepend(instruction_type* insn)
		{
			insn->prev = nullptr;
			insn->next = head;

			if (head)
				head->prev = insn;
			else
				tail = insn;

			head = insn;
			++count;
		}
	};

	/**
	 * @brief Iterator for instruction list traversal
	 * @tparam InstructionType Target-specific instruction struct
	 */
	template<TargetInstruction InstructionType>
	class InstructionIterator
	{
	public:
		using instruction_type = Instruction<InstructionType>;

	private:
		instruction_type* current;

	public:
		explicit InstructionIterator(instruction_type* insn) : current(insn) {}

		instruction_type& operator*() const { return *current; }
		instruction_type* operator->() const { return current; }

		InstructionIterator& operator++()
		{
			if (current)
				current = current->next;
			return *this;
		}

		InstructionIterator operator++(int)
		{
			InstructionIterator tmp = *this;
			++(*this);
			return tmp;
		}

		InstructionIterator& operator--()
		{
			if (current)
				current = current->prev;
			return *this;
		}

		bool operator==(const InstructionIterator& other) const
		{
			return current == other.current;
		}

		bool operator!=(const InstructionIterator& other) const
		{
			return current != other.current;
		}

		/**
		 * @brief Get the underlying instruction pointer
		 */
		instruction_type* get() const
		{
			return current;
		}
	};
}
