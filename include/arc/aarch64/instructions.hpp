/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <cstdint>
#include <arc/support/instruction.hpp>

namespace arc
{
	/**
	 * @brief AArch64 instruction opcodes
	 */
	enum class Aarch64Opcode : std::uint16_t
	{
		/* arithmetic instructions */
		ADD_IMM,        /* add with immediate */
		ADD_REG,        /* add register */
		SUB_IMM,        /* subtract immediate */
		SUB_REG,        /* subtract register */
		MUL,            /* multiply */
		MADD,           /* multiply-add */
		MSUB,           /* multiply-subtract */
		UDIV,           /* unsigned divide */
		SDIV,           /* signed divide */

		/* logical instructions */
		AND_IMM,        /* bitwise and immediate */
		AND_REG,        /* bitwise and register */
		ORR_IMM,        /* bitwise or immediate */
		ORR_REG,        /* bitwise or register */
		EOR_IMM,        /* bitwise xor immediate */
		EOR_REG,        /* bitwise xor register */
		MVN,            /* bitwise not */

		/* shift instructions */
		LSL_IMM,        /* logical shift left immediate */
		LSL_REG,        /* logical shift left register */
		LSR_IMM,        /* logical shift right immediate */
		LSR_REG,        /* logical shift right register */
		ASR_IMM,        /* arithmetic shift right immediate */
		ASR_REG,        /* arithmetic shift right register */

		/* memory instructions */
		LDR_IMM,        /* load register immediate offset */
		LDR_REG,        /* load register register offset */
		STR_IMM,        /* store register immediate offset */
		STR_REG,        /* store register register offset */
		LDP,            /* load pair */
		STP,            /* store pair */

		/* branch instructions */
		B,              /* unconditional branch */
		BL,             /* branch with link */
		BR,             /* branch register */
		BLR,            /* branch with link register */
		RET,            /* return */

		/* conditional branch */
		B_EQ,           /* branch if equal */
		B_NE,           /* branch if not equal */
		B_LT,           /* branch if less than */
		B_LE,           /* branch if less than or equal */
		B_GT,           /* branch if greater than */
		B_GE,           /* branch if greater than or equal */

		/* compare and branch */
		CBZ,            /* compare and branch if zero */
		CBNZ,           /* compare and branch if not zero */

		/* comparison instructions */
		CMP_IMM,        /* compare immediate */
		CMP_REG,        /* compare register */
		TST_IMM,        /* test immediate */
		TST_REG,        /* test register */

		/* conditional select */
		CSEL,           /* conditional select */
		CSET,           /* conditional set */

		/* move instructions */
		MOV_IMM,        /* move immediate */
		MOV_REG,        /* move register */
		MOVZ,           /* move wide with zero */
		MOVK,           /* move wide with keep */

		/* system instructions */
		NOP,            /* no operation */
		BRK,            /* breakpoint */
	};

	/**
	 * @brief AArch64 instruction specification
	 */
	struct InstructionSpec
	{
		using Opcode = Aarch64Opcode;

		static constexpr std::size_t max_operands() { return 4; }
		static constexpr std::size_t encoding_size() { return 4; }
	};

	/**
	 * @brief AArch64-specific operand types extending the generic operand
	 */
	struct AArch64Operand : public Operand
	{
		enum class ExtendType : std::uint8_t
		{
			NONE,
			UXTB,   /* unsigned extend byte */
			UXTH,   /* unsigned extend halfword */
			UXTW,   /* unsigned extend word */
			UXTX,   /* unsigned extend doubleword */
			SXTB,   /* signed extend byte */
			SXTH,   /* signed extend halfword */
			SXTW,   /* signed extend word */
			SXTX    /* signed extend doubleword */
		};

		enum class ShiftType : std::uint8_t
		{
			NONE,
			LSL,    /* logical shift left */
			LSR,    /* logical shift right */
			ASR,    /* arithmetic shift right */
			ROR     /* rotate right */
		};

		ExtendType extend = ExtendType::NONE;
		ShiftType shift = ShiftType::NONE;
		std::uint8_t shift_amount = 0;

		constexpr AArch64Operand() = default;
		constexpr AArch64Operand(const Operand& base) : Operand(base) {}

		static constexpr AArch64Operand reg(std::uint8_t reg_id, std::uint8_t size = 8)
		{
			return AArch64Operand{ Operand::reg(reg_id, size) };
		}

		static constexpr AArch64Operand reg_extended(std::uint8_t reg_id, ExtendType ext, std::uint8_t size = 8)
		{
			AArch64Operand op{ Operand::reg(reg_id, size) };
			op.extend = ext;
			return op;
		}

		static constexpr AArch64Operand reg_shifted(std::uint8_t reg_id, ShiftType shift_type,
		                                           std::uint8_t amount, std::uint8_t size = 8)
		{
			AArch64Operand op{ Operand::reg(reg_id, size) };
			op.shift = shift_type;
			op.shift_amount = amount;
			return op;
		}

		static constexpr AArch64Operand imm12(std::uint16_t immediate)
		{
			return AArch64Operand{ Operand::imm(immediate, 2) };
		}

		static constexpr AArch64Operand imm16(std::uint16_t immediate)
		{
			return AArch64Operand{ Operand::imm(immediate, 2) };
		}

		static constexpr AArch64Operand imm26(std::uint32_t immediate)
		{
			return AArch64Operand{ Operand::imm(immediate, 4) };
		}

		static constexpr AArch64Operand mem_offset(std::uint8_t base_reg, std::int16_t offset, std::uint8_t size = 8)
		{
			AArch64Operand op{ Operand::mem(static_cast<std::uint32_t>(base_reg), size) };
			op.value = (static_cast<std::uint32_t>(offset & 0xFFFF) << 16) | base_reg;
			return op;
		}

		static constexpr AArch64Operand mem_reg(std::uint8_t base_reg, std::uint8_t index_reg,
		                                       ExtendType ext = ExtendType::NONE, std::uint8_t size = 8)
		{
			AArch64Operand op = { Operand::mem((static_cast<std::uint32_t>(index_reg) << 8) | base_reg, size) };
			op.extend = ext;
			return op;
		}
	};

	/**
	 * @brief AArch64 register encoding
	 */
	namespace registers
	{
		/* general purpose registers */
		constexpr std::uint8_t X0 = 0, W0 = 0;
		constexpr std::uint8_t X1 = 1, W1 = 1;
		constexpr std::uint8_t X2 = 2, W2 = 2;
		constexpr std::uint8_t X3 = 3, W3 = 3;
		constexpr std::uint8_t X4 = 4, W4 = 4;
		constexpr std::uint8_t X5 = 5, W5 = 5;
		constexpr std::uint8_t X6 = 6, W6 = 6;
		constexpr std::uint8_t X7 = 7, W7 = 7;
		constexpr std::uint8_t X8 = 8, W8 = 8;
		constexpr std::uint8_t X9 = 9, W9 = 9;
		constexpr std::uint8_t X10 = 10, W10 = 10;
		constexpr std::uint8_t X11 = 11, W11 = 11;
		constexpr std::uint8_t X12 = 12, W12 = 12;
		constexpr std::uint8_t X13 = 13, W13 = 13;
		constexpr std::uint8_t X14 = 14, W14 = 14;
		constexpr std::uint8_t X15 = 15, W15 = 15;
		constexpr std::uint8_t X16 = 16, W16 = 16;
		constexpr std::uint8_t X17 = 17, W17 = 17;
		constexpr std::uint8_t X18 = 18, W18 = 18;
		constexpr std::uint8_t X19 = 19, W19 = 19;
		constexpr std::uint8_t X20 = 20, W20 = 20;
		constexpr std::uint8_t X21 = 21, W21 = 21;
		constexpr std::uint8_t X22 = 22, W22 = 22;
		constexpr std::uint8_t X23 = 23, W23 = 23;
		constexpr std::uint8_t X24 = 24, W24 = 24;
		constexpr std::uint8_t X25 = 25, W25 = 25;
		constexpr std::uint8_t X26 = 26, W26 = 26;
		constexpr std::uint8_t X27 = 27, W27 = 27;
		constexpr std::uint8_t X28 = 28, W28 = 28;
		constexpr std::uint8_t X29 = 29, W29 = 29; /* frame pointer */
		constexpr std::uint8_t X30 = 30, W30 = 30; /* link register */
		constexpr std::uint8_t XZR = 31, WZR = 31; /* zero register */
		constexpr std::uint8_t SP = 31;             /* stack pointer */

		/* floating point registers */
		constexpr std::uint8_t D0 = 0, S0 = 0, H0 = 0, B0 = 0;
		constexpr std::uint8_t D1 = 1, S1 = 1, H1 = 1, B1 = 1;
		constexpr std::uint8_t D2 = 2, S2 = 2, H2 = 2, B2 = 2;
		constexpr std::uint8_t D3 = 3, S3 = 3, H3 = 3, B3 = 3;
		constexpr std::uint8_t D4 = 4, S4 = 4, H4 = 4, B4 = 4;
		constexpr std::uint8_t D5 = 5, S5 = 5, H5 = 5, B5 = 5;
		constexpr std::uint8_t D6 = 6, S6 = 6, H6 = 6, B6 = 6;
		constexpr std::uint8_t D7 = 7, S7 = 7, H7 = 7, B7 = 7;
		constexpr std::uint8_t D8 = 8, S8 = 8, H8 = 8, B8 = 8;
		constexpr std::uint8_t D9 = 9, S9 = 9, H9 = 9, B9 = 9;
		constexpr std::uint8_t D10 = 10, S10 = 10, H10 = 10, B10 = 10;
		constexpr std::uint8_t D11 = 11, S11 = 11, H11 = 11, B11 = 11;
		constexpr std::uint8_t D12 = 12, S12 = 12, H12 = 12, B12 = 12;
		constexpr std::uint8_t D13 = 13, S13 = 13, H13 = 13, B13 = 13;
		constexpr std::uint8_t D14 = 14, S14 = 14, H14 = 14, B14 = 14;
		constexpr std::uint8_t D15 = 15, S15 = 15, H15 = 15, B15 = 15;
		constexpr std::uint8_t D16 = 16, S16 = 16, H16 = 16, B16 = 16;
		constexpr std::uint8_t D17 = 17, S17 = 17, H17 = 17, B17 = 17;
		constexpr std::uint8_t D18 = 18, S18 = 18, H18 = 18, B18 = 18;
		constexpr std::uint8_t D19 = 19, S19 = 19, H19 = 19, B19 = 19;
		constexpr std::uint8_t D20 = 20, S20 = 20, H20 = 20, B20 = 20;
		constexpr std::uint8_t D21 = 21, S21 = 21, H21 = 21, B21 = 21;
		constexpr std::uint8_t D22 = 22, S22 = 22, H22 = 22, B22 = 22;
		constexpr std::uint8_t D23 = 23, S23 = 23, H23 = 23, B23 = 23;
		constexpr std::uint8_t D24 = 24, S24 = 24, H24 = 24, B24 = 24;
		constexpr std::uint8_t D25 = 25, S25 = 25, H25 = 25, B25 = 25;
		constexpr std::uint8_t D26 = 26, S26 = 26, H26 = 26, B26 = 26;
		constexpr std::uint8_t D27 = 27, S27 = 27, H27 = 27, B27 = 27;
		constexpr std::uint8_t D28 = 28, S28 = 28, H28 = 28, B28 = 28;
		constexpr std::uint8_t D29 = 29, S29 = 29, H29 = 29, B29 = 29;
		constexpr std::uint8_t D30 = 30, S30 = 30, H30 = 30, B30 = 30;
		constexpr std::uint8_t D31 = 31, S31 = 31, H31 = 31, B31 = 31;
	}

	/**
	 * @brief Type aliases for AArch64-specific instruction types
	 */
	using AArch64Instruction = Instruction<InstructionSpec>;
	using AArch64InstructionList = InstructionList<InstructionSpec>;
	using AArch64InstructionIterator = InstructionIterator<InstructionSpec>;

	/**
	 * @brief Condition codes for conditional instructions
	 */
	enum class ConditionCode : std::uint8_t
	{
		EQ = 0x0,    /* equal */
		NE = 0x1,    /* not equal */
		CS = 0x2,    /* carry set */
		CC = 0x3,    /* carry clear */
		MI = 0x4,    /* minus / negative */
		PL = 0x5,    /* plus / positive or zero */
		VS = 0x6,    /* overflow */
		VC = 0x7,    /* no overflow */
		HI = 0x8,    /* unsigned higher */
		LS = 0x9,    /* unsigned lower or same */
		GE = 0xa,    /* signed greater than or equal */
		LT = 0xb,    /* signed less than */
		GT = 0xc,    /* signed greater than */
		LE = 0xd,    /* signed less than or equal */
		AL = 0xe,    /* always (unconditional) */
		NV = 0xf     /* never */
	};
}
