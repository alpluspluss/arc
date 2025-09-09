/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <functional>
#include <vector>
#include <algorithm>
#include <arc/codegen/selection-dag.hpp>
#include <arc/codegen/instruction.hpp>

namespace arc
{
	enum class TargetArch : std::uint8_t
	{
		AARCH64,
		X86_64,
		RISCV64
	};

	template<typename T>
	concept InstructionSelectorTarget = requires
	{
		typename T::instruction_type;
		requires TargetInstruction<typename T::instruction_type>;
		{ T::target_arch() } -> std::same_as<TargetArch>;
	};

	template<TargetArch Target>
	struct TargetTraits;

	template<InstructionSelectorTarget T>
	class InstructionSelector
	{
	public:
		using instruction_type = typename T::instruction_type;
		using dag_type = SelectionDAG<instruction_type>;
		using dag_node = typename dag_type::DAGNode;

		struct Pattern
		{
			std::function<bool(dag_node *)> predicate;
			std::function<dag_node*(dag_node *)> generator;
			std::int32_t priority = 0;
			std::string_view name;

			bool operator<(const Pattern &other) const
			{
				return priority > other.priority;
			}
		};

		explicit InstructionSelector(dag_type &dag) : selection_dag(dag) {}

		void define(Pattern &&pattern)
		{
			pats.push_back(std::move(pattern));
			std::sort(pats.begin(), pats.end());
		}

		void define(std::function<bool(dag_node *)> matcher,
		            std::function<dag_node*(dag_node *)> generator,
		            std::int32_t priority = 0,
		            std::string_view name = "unnamed")
		{
			define(Pattern {
				.predicate = std::move(matcher),
				.generator = std::move(generator),
				.priority = priority,
				.name = name
			});
		}

		bool select(dag_node *node)
		{
			if (!node || (node->state & SelectionState::SELECTED) != SelectionState::UNSELECTED)
				return false;

			for (const auto &pattern: pats)
			{
				if (pattern.predicate(node))
				{
					if (auto *result = pattern.generator(node))
					{
						node->state |= SelectionState::SELECTED;
						return true;
					}
				}
			}
			return false;
		}

		std::vector<Instruction<instruction_type> *> select_all()
		{
			std::vector<Instruction<instruction_type> *> instructions;

			for (auto sorted_nodes = selection_dag.sort();
			     auto *node: sorted_nodes)
			{
				if (node->kind == NodeKind::VALUE ||
					node->kind == NodeKind::CHAIN ||
					node->kind == NodeKind::IMMEDIATE)
				{
					select(node);
				}
			}

			return instructions;
		}

		const std::vector<Pattern> &patterns() const
		{
			return pats;
		}

		/**
		 * @brief Create instruction DAG node with opcode and operands
		 * @param opcode Target instruction opcode
		 * @param operands Vector of operand DAG nodes
		 * @return Pointer to created instruction node
		 */
		dag_node *make_instruction(typename instruction_type::Opcode opcode,
		                           const std::vector<dag_node *> &operands = {})
		{
			auto *insn_node = selection_dag.template make_node<NodeKind::INSTRUCTION>();
			insn_node->opcode = opcode;

			for (auto *operand: operands)
			{
				insn_node->operands.push_back(operand);
				operand->users.push_back(insn_node);
			}

			return insn_node;
		}

		/**
		 * @brief Create register operand DAG node
		 * @tparam U Data type for the register
		 * @param reg_id Physical register identifier
		 * @return Pointer to created register node
		 */
		template<DataType U>
		dag_node *make_reg(std::uint32_t reg_id)
		{
			return selection_dag.template make_reg<U>(reg_id);
		}

		/**
		 * @brief Create immediate operand DAG node
		 * @tparam U Data type for the immediate
		 * @param value Immediate value
		 * @return Pointer to created immediate node
		 */
		template<DataType U>
		dag_node *make_imm(std::int64_t value)
		{
			return selection_dag.template make_imm<U>(value);
		}

		/**
		 * @brief Create memory operand DAG node
		 * @tparam U Data type for the memory access
		 * @param address Memory address
		 * @return Pointer to created memory node
		 */
		template<DataType U>
		dag_node *make_mem(std::uint32_t address)
		{
			return selection_dag.template make_mem<U>(address);
		}

	private:
		std::vector<Pattern> pats;
		dag_type &selection_dag;
	};

	/**
	 * @brief Factory function to create instruction selector for target architecture
	 * @tparam Target Target architecture enum value
	 * @param dag Selection DAG reference
	 * @return Unique pointer to instruction selector instance
	 */
	template<TargetArch Target>
	std::unique_ptr<InstructionSelector<TargetTraits<Target> > > create_selector(
		SelectionDAG<typename TargetTraits<Target>::instruction_type> &dag)
	{
		return std::make_unique<InstructionSelector<TargetTraits<Target> > >(dag);
	}
}
