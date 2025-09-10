/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <cstdint>
#include <queue>
#include <unordered_map>
#include <arc/codegen/instruction.hpp>
#include <arc/foundation/node.hpp>
#include <arc/foundation/region.hpp>
#include <arc/foundation/typed-data.hpp>
#include <arc/support/algorithm.hpp>
#include <arc/support/allocator.hpp>
#include <arc/support/slice.hpp>
#include <print>
namespace arc
{
	struct Node;
	class Region;

	/**
	 * @brief Type of DAG node
	 */
	enum class NodeKind : std::uint8_t
	{
		/** @brief Target instruction node */
		INSTRUCTION,
		/** @brief Intermediate value producer */
		VALUE,
		/** @brief Register operand */
		REGISTER,
		/** @brief Immediate operand */
		IMMEDIATE,
		/** @brief Memory operand */
		MEMORY,
		/** @brief Control/memory dependency chain */
		CHAIN,
		/** @brief Entry point marker */
		ENTRY,
		/** @brief Region boundary marker */
		REGION_BOUNDARY
	};

	enum class SelectionState : std::uint8_t
	{
		UNSELECTED = 0,
		SELECTED = 1 << 0,
		SCHEDULED = 1 << 1
	};

	template<TargetInstruction T>
	class SelectionDAG
	{
	public:
		/**
		 * @brief DAG node representation
		 */
		struct DAGNode
		{
			/** @brief Bidirectional list for node graph */
			u8slice<DAGNode *> operands;
			u8slice<DAGNode *> users;
			/** @brief Original IR node this was created from */
			Node *source = nullptr;
			/** @brief Target instruction */
			std::optional<typename T::Opcode> opcode;
			/** @brief value information */
			std::uint32_t value_id = 0;
			DataType value_t = DataType::VOID;
			/** @brief Operand data for leaf nodes */
			Operand operand;
			/** @brief Type of DAG node */
			NodeKind kind;
			/** @brief Selection state */
			SelectionState state = SelectionState::UNSELECTED;
			/** @brief Contructor */
			explicit DAGNode(NodeKind k) : kind(k) {}
		};

		explicit SelectionDAG(Region *r) : source(r) {}

		/**
		 * @brief Build DAG from Arc IR region
		 */
		void build()
		{
			if (!source)
				return;

			auto* entry_chain = make_node<NodeKind::ENTRY>();
			entry_nodes.push_back(entry_chain);
			chain_root_nodes.push_back(entry_chain);

			/* convert each IR node to DAG nodes */
			for (Node* ir_node : source->nodes())
			{
				if (!ir_node)
					continue;
				to_dag(ir_node, entry_chain);
			}

			for (const auto& n : dag_nodes)
			{
				DAGNode* dag = n;
				Node* ir = dag->source;

				if (!ir)
					continue;

				/* connect to IR input nodes */
				for (Node* input : ir->inputs)
				{
					if (DAGNode* input_dag = find(input))
					{
						dag->operands.push_back(input_dag);
						input_dag->users.push_back(dag);
					}
				}
			}
		}

		/**
		 * @brief Get all DAG nodes
		 */
		const std::vector<DAGNode *> &nodes() const
		{
			return dag_nodes;
		}

		/**
		 * @brief Get all entry nodes
		 */
		const std::vector<DAGNode *> &entries() const
		{
			return entry_nodes;
		}

		/**
		 * @brief Get all chain root nodes
		 */
		const std::vector<DAGNode *> &chain_roots() const
		{
			return chain_root_nodes;
		}

		/**
		 * @brief Find DAG node corresponding to given IR node
		 */
		DAGNode *find(Node *n) const
		{
			auto it = node_map.find(n);
			return it != node_map.end() ? it->second : nullptr;
		}

		/**
		 * @brief Create a new DAG node
		 */
		template<NodeKind K>
		DAGNode *make_node()
		{
			ach::shared_allocator<DAGNode> alloc;
			auto *dag = alloc.allocate(1);
			std::construct_at<DAGNode>(dag, K);
			return dag;
		}

		/**
		 * @brief Create immediate operand DAG node
		 * @tparam U Data type of immediate
		 * @param value Immediate value
		 */
		template<DataType U>
		DAGNode *make_imm(std::int64_t value)
		{
			auto *node = make_node<NodeKind::IMMEDIATE>();
			node->value_t = U;
			node->operand = Operand::imm(value);
			return node;
		}

		/**
		 * @brief Create register operand DAG node
		 * @tparam U Data type of register
		 * @param reg_id Register identifier
		 */
		template<DataType U>
		DAGNode *make_reg(std::uint32_t reg_id)
		{
			auto *n = make_node<NodeKind::REGISTER>();
			n->value_t = U;
			n->operand = Operand::reg(reg_id);
			return n;
		}

		/**
		 * @brief Create memory operand DAG node
		 * @tparam U Data type of memory access
		 * @param addr Memory address
		 */
		template<DataType U>
		DAGNode *make_mem(std::uint32_t addr)
		{
			auto *n = make_node<NodeKind::MEMORY>();
			n->value_t = U;
			n->operand = Operand::mem(addr);
			return n;
		}

		/**
		 * @brief Topological sort of DAG nodes
		 */
		std::vector<DAGNode *> sort()
		{
			/* this is Khan's in-degree topo sort algorithm;
			 * see: https://en.wikipedia.org/wiki/Topological_sorting#Kahn's_algorithm */
			std::vector<DAGNode *> result;
			std::unordered_map<DAGNode *, std::size_t> in_degree;
			std::queue<DAGNode *> zero_in_degree;

			for (auto *n: nodes())
				in_degree[n] = n->operands.size();

			/* seed nodes with zero in-degree into the ready queue
			 * then process nodes in topological order */
			for (const auto &[n, deg]: in_degree)
			{
				if (deg == 0)
					zero_in_degree.push(n);
			}

			while (!zero_in_degree.empty())
			{
				DAGNode *current = zero_in_degree.front();
				zero_in_degree.pop();
				result.push_back(current);

				/* update users */
				for (auto *user: current->users)
				{
					if (--in_degree[user] == 0)
						zero_in_degree.push(user);
				}
			}

			return result;
		}

		/**
		 * @brief Linearize DAG nodes in topological order for live range analysis
		 */
		void linearize()
		{
			auto sorted = sort();
			for (std::size_t i = 0; i < sorted.size(); ++i)
				sorted[i]->value_id = static_cast<std::uint32_t>(i + 1);
		}

	private:
		std::unordered_map<Node *, DAGNode *> node_map;
		std::vector<DAGNode *> dag_nodes;
		std::vector<DAGNode *> entry_nodes;
		std::vector<DAGNode *> chain_root_nodes;
		Region *source;
		std::uint32_t next_value = 1;

		void to_dag(Node *ir, DAGNode *chain)
		{
			DAGNode *dag = nullptr;

			switch (ir->ir_type)
			{
				case NodeType::ENTRY:
				{
					/* note: this is handled at `build()` */
					return;
				}
				case NodeType::EXIT:
				{
					dag = make_node<NodeKind::CHAIN>();
					dag->operands.push_back(chain);
					chain->users.push_back(dag);
					break;
				}
				case NodeType::FUNCTION:
				{
					//throw std::runtime_error("NodeType::FUNCTION should not appear in region DAG conversion");
					break;
				}
				case NodeType::LIT:
				{
					std::int64_t v = extract_literal_value(ir);
					dag = make_imm<DataType::INT64>(v);
					dag->value_t = ir->type_kind; /* preserve original type */
					break;
				}
				case NodeType::ADD:
				case NodeType::SUB:
				case NodeType::MUL:
				case NodeType::DIV:
				case NodeType::MOD:
				case NodeType::GT:
				case NodeType::GTE:
				case NodeType::LT:
				case NodeType::LTE:
				case NodeType::EQ:
				case NodeType::NEQ:
				case NodeType::BAND:
				case NodeType::BOR:
				case NodeType::BXOR:
				case NodeType::BNOT:
				case NodeType::BSHL:
				case NodeType::BSHR:
				{
					dag = make_node<NodeKind::VALUE>();
					dag->value_t = ir->type_kind;
					break;
				}
				case NodeType::LOAD:
				case NodeType::PTR_LOAD:
				case NodeType::ATOMIC_LOAD:
				{
					dag = make_node<NodeKind::VALUE>();
					dag->value_t = ir->type_kind;
					/* add chain dependency for memory ordering */
					dag->operands.push_back(chain);
					chain->users.push_back(dag);
					break;
				}
				case NodeType::STORE:
				case NodeType::PTR_STORE:
				case NodeType::ATOMIC_STORE:
				{
					dag = make_node<NodeKind::CHAIN>();
					/* stores produce new chain state */
					dag->operands.push_back(chain);
					chain->users.push_back(dag);
					break;
				}
				case NodeType::ALLOC:
				{
					dag = make_node<NodeKind::VALUE>();
					dag->value_t = DataType::POINTER;
					break;
				}
				case NodeType::ADDR_OF:
				{
					dag = make_node<NodeKind::VALUE>();
					dag->value_t = DataType::POINTER;
					break;
				}
				case NodeType::PTR_ADD:
				{
					dag = make_node<NodeKind::VALUE>();
					dag->value_t = DataType::POINTER;
					break;
				}
				case NodeType::CAST:
				{
					dag = make_node<NodeKind::VALUE>();
					dag->value_t = ir->type_kind; /* target type of cast */
					break;
				}
				case NodeType::CALL:
				case NodeType::INVOKE:
				{
					dag = make_node<NodeKind::VALUE>();
					dag->value_t = ir->type_kind;
					/* calls have side effects, depend on chain */
					dag->operands.push_back(chain);
					chain->users.push_back(dag);
					break;
				}
				case NodeType::RET:
				{
					dag = make_node<NodeKind::CHAIN>();
					dag->operands.push_back(chain);
					chain->users.push_back(dag);
					break;
				}
				case NodeType::BRANCH:
				case NodeType::JUMP:
				{
					dag = make_node<NodeKind::CHAIN>();
					dag->operands.push_back(chain);
					chain->users.push_back(dag);
					break;
				}
				case NodeType::ACCESS:
				{
					dag = make_node<NodeKind::VALUE>();
					dag->value_t = ir->type_kind;
					break;
				}
				case NodeType::FROM:
				{
					dag = make_node<NodeKind::VALUE>();
					dag->value_t = ir->type_kind;

					/* FROM node needs immediate operand connection because
					 * their inputs may come from different regions not processed by
					 * this DAG build */
					for (Node* input : ir->inputs)
					{
						DAGNode* input_dag = find(input);
						if (!input_dag)
						{
							input_dag = make_node<NodeKind::VALUE>();
							input_dag->source = input;
							input_dag->value_t = input->type_kind;
							input_dag->value_id = next_value++;
							node_map[input] = input_dag;
							dag_nodes.push_back(input_dag);
						}
						dag->operands.push_back(input_dag);
						input_dag->users.push_back(dag);
					}
					break;
				}
				case NodeType::PARAM:
				{
					dag = make_node<NodeKind::VALUE>();
					dag->value_t = ir->type_kind;
					break;
				}
				case NodeType::ATOMIC_CAS:
				{
					dag = make_node<NodeKind::VALUE>();
					dag->value_t = ir->type_kind;
					/* atomic operations depend on chain */
					dag->operands.push_back(chain);
					chain->users.push_back(dag);
					break;
				}
				case NodeType::VECTOR_BUILD:
				{
					dag = make_node<NodeKind::VALUE>();
					dag->value_t = DataType::VECTOR; /* result is always vector type */
					break;
				}
				case NodeType::VECTOR_EXTRACT:
				{
					dag = make_node<NodeKind::VALUE>();
					/* extract produces scalar of the vector's element type */
					if (ir->inputs.size() > 0 && ir->inputs[0]->value.type() == DataType::VECTOR)
					{
						const auto &vec_data = ir->inputs[0]->value.get<DataType::VECTOR>();
						dag->value_t = vec_data.elem_type;
					}
					else
						throw std::runtime_error("invalid vector extract. all operands must be vectors");
					break;
				}
				case NodeType::VECTOR_SPLAT:
				{
					dag = make_node<NodeKind::VALUE>();
					dag->value_t = DataType::VECTOR; /* splat creates vector from scalar */
					break;
				}
				default:
				{
					dag = make_node<NodeKind::VALUE>();
					dag->value_t = ir->type_kind;
					break;
				}
			}

			if (dag)
			{
				dag->source = ir;
				dag->value_id = next_value++;
				node_map[ir] = dag;
				dag_nodes.push_back(dag);
			}
		}
	};

	inline SelectionState operator|(SelectionState lhs, SelectionState rhs)
	{
		return static_cast<SelectionState>(
			static_cast<std::underlying_type_t<SelectionState>>(lhs) |
			static_cast<std::underlying_type_t<SelectionState>>(rhs)
		);
	}

	inline SelectionState &operator|=(SelectionState &lhs, const SelectionState rhs)
	{
		lhs = lhs | rhs;
		return lhs;
	}

	inline SelectionState operator&(SelectionState lhs, SelectionState rhs)
	{
		return static_cast<SelectionState>(
			static_cast<std::underlying_type_t<SelectionState>>(lhs) &
			static_cast<std::underlying_type_t<SelectionState>>(rhs)
		);
	}

	inline SelectionState &operator&=(SelectionState &lhs, const SelectionState rhs)
	{
		lhs = lhs & rhs;
		return lhs;
	}

	inline SelectionState operator^(SelectionState lhs, SelectionState rhs)
	{
		return static_cast<SelectionState>(
			static_cast<std::underlying_type_t<SelectionState>>(lhs) ^
			static_cast<std::underlying_type_t<SelectionState>>(rhs)
		);
	}

	inline SelectionState &operator^=(SelectionState &lhs, const SelectionState rhs)
	{
		lhs = lhs ^ rhs;
		return lhs;
	}

	inline SelectionState operator~(SelectionState prop)
	{
		return static_cast<SelectionState>(
			~static_cast<std::underlying_type_t<SelectionState>>(prop)
		);
	}
}
