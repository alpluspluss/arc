/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/support/inference.hpp>

namespace arc
{
	bool infer_binary_t(Node *lhs, Node *rhs)
	{
		if (!lhs || !rhs)
			return false;

		const DataType lhs_type = lhs->type_kind;
		const DataType rhs_type = rhs->type_kind;
		if (lhs_type == rhs_type)
		{
			/* no promotion needed if element types of both vectors are the same
			 * however, if one of the element types is not equal or equal to VOID
			 * then we need to promote both sides */
			if (lhs_type == DataType::VECTOR)
			{
				const auto &lhs_vec = lhs->value.get<DataType::VECTOR>();
				const auto &rhs_vec = rhs->value.get<DataType::VECTOR>();
				if (lhs_vec.elem_type == rhs_vec.elem_type)
					return true;

				const DataType promoted_elem = infer_primitive_types(lhs_vec.elem_type, rhs_vec.elem_type);
				if (promoted_elem == DataType::VOID)
					return false;

				/* update both vectors with promoted element type */
				auto lhs_promoted = lhs_vec;
				auto rhs_promoted = rhs_vec;
				lhs_promoted.elem_type = promoted_elem;
				rhs_promoted.elem_type = promoted_elem;
				lhs->value.set<DataTraits<DataType::VECTOR>::value, DataType::VECTOR>(lhs_promoted);
				rhs->value.set<DataTraits<DataType::VECTOR>::value, DataType::VECTOR>(rhs_promoted);
				return true;
			}
			/* non-vector types are the same; non-trivial types do not apply for the case */
			return true;
		}

		/* void always propagates errors */
		if (lhs_type == DataType::VOID || rhs_type == DataType::VOID)
			return false;

		/* handle vector types */
		if (lhs_type == DataType::VECTOR || rhs_type == DataType::VECTOR)
		{
			/* vector-scalar mixing is prohibited to avoid ambiguity about whether
			* to broadcast the scalar or extract vector elements */
			if (lhs_type != DataType::VECTOR || rhs_type != DataType::VECTOR)
				return false;

			const auto &lhs_vec = lhs->value.get<DataType::VECTOR>();
			const auto &rhs_vec = rhs->value.get<DataType::VECTOR>();

			/* promote element types */
			DataType promoted_elem = infer_primitive_types(lhs_vec.elem_type, rhs_vec.elem_type);
			if (promoted_elem == DataType::VOID)
				return false;

			/* update both vectors with promoted element type */
			auto lhs_promoted = lhs_vec;
			auto rhs_promoted = rhs_vec;
			lhs_promoted.elem_type = promoted_elem;
			rhs_promoted.elem_type = promoted_elem;
			lhs->value.set<DataTraits<DataType::VECTOR>::value, DataType::VECTOR>(lhs_promoted);
			rhs->value.set<DataTraits<DataType::VECTOR>::value, DataType::VECTOR>(rhs_promoted);
			return true;
		}

		/* delegate to primitive type inference and promote both operands to their common types;
		 * DataType::VOID check here because `infer_primitive_types` returns it. Resharper seems to
		 * believe this has already been checked for some reason */
		const DataType promoted_type = infer_primitive_types(lhs_type, rhs_type);
		// ReSharper disable once CppDFAConstantConditions
		if (promoted_type == DataType::VOID)
			// ReSharper disable once CppDFAUnreachableCode
			return false;
		lhs->type_kind = promoted_type;
		rhs->type_kind = promoted_type;
		return true;
	}

	DataType infer_primitive_types(DataType lhs, DataType rhs) noexcept
	{
		if (lhs == rhs)
			return lhs;

		/* these types require explicit casts in Arc's type system to maintain
		 * memory safety and prevent accidental pointer arithmetic or struct mixing */
		if (lhs == DataType::POINTER || rhs == DataType::POINTER ||
		    lhs == DataType::ARRAY || rhs == DataType::ARRAY ||
		    lhs == DataType::STRUCT || rhs == DataType::STRUCT ||
		    lhs == DataType::FUNCTION || rhs == DataType::FUNCTION ||
		    lhs == DataType::VECTOR || rhs == DataType::VECTOR)
		{
			return DataType::VOID;
		}

		/* bool promotion to int32 follows C convention and ensures
		 * consistent arithmetic behavior across all integer operations according
		 * to Arc's abstract machine model */
		if (lhs == DataType::BOOL)
			lhs = DataType::INT32;
		if (rhs == DataType::BOOL)
			rhs = DataType::INT32;

		/* float64 is preferred for mixed float/integer arithmetic to minimize
		 * precision loss; only keep float32 when both operands are explicitly float32 */
		if (is_float_t(lhs) || is_float_t(rhs))
		{
			if (lhs == DataType::FLOAT32 && rhs == DataType::FLOAT32)
				return DataType::FLOAT32;
			return DataType::FLOAT64;
		}

		if (!is_integer_t(lhs) || !is_integer_t(rhs))
			return DataType::VOID;

		/* promote to int32 minimum width to match target register sizes
		 * and avoid subword operations that require masking on most architectures */
		if (get_integer_rank(lhs) < get_integer_rank(DataType::INT32))
			lhs = DataType::INT32;
		if (get_integer_rank(rhs) < get_integer_rank(DataType::INT32))
			rhs = DataType::INT32;

		// ReSharper disable once CppDFAConstantConditions
		if (lhs == rhs)
			// ReSharper disable once CppDFAUnreachableCode
			return lhs;

		const int lhs_rank = get_integer_rank(lhs);
		const int rhs_rank = get_integer_rank(rhs);
		if (lhs_rank == rhs_rank)
		{
			/* mixed signedness at same rank: promote to larger signed type to prevent
			 * wrap-around bugs; uint64 is exception as no larger signed type exists */
			switch (lhs_rank)
			{
				case 2:
					return DataType::INT64;
				case 3:
					return DataType::UINT64;
				default:
					return DataType::VOID;
			}
		}

		return (lhs_rank > rhs_rank) ? lhs : rhs;
	}

	void set_t(TypedData &data, DataType type)
	{
		switch (type)
		{
			case DataType::VOID:
				set_t<DataType::VOID>(data);
				break;
			case DataType::BOOL:
				set_t<DataType::BOOL>(data);
				break;
			case DataType::INT8:
				set_t<DataType::INT8>(data);
				break;
			case DataType::INT16:
				set_t<DataType::INT16>(data);
				break;
			case DataType::INT32:
				set_t<DataType::INT32>(data);
				break;
			case DataType::INT64:
				set_t<DataType::INT64>(data);
				break;
			case DataType::UINT8:
				set_t<DataType::UINT8>(data);
				break;
			case DataType::UINT16:
				set_t<DataType::UINT16>(data);
				break;
			case DataType::UINT32:
				set_t<DataType::UINT32>(data);
				break;
			case DataType::UINT64:
				set_t<DataType::UINT64>(data);
				break;
			case DataType::FLOAT32:
				set_t<DataType::FLOAT32>(data);
				break;
			case DataType::FLOAT64:
				set_t<DataType::FLOAT64>(data);
				break;
			case DataType::POINTER:
				set_t<DataType::POINTER>(data);
				break;
			case DataType::ARRAY:
				set_t<DataType::ARRAY>(data);
				break;
			case DataType::STRUCT:
				set_t<DataType::STRUCT>(data);
				break;
			case DataType::FUNCTION:
				set_t<DataType::FUNCTION>(data);
				break;
			case DataType::VECTOR:
				set_t<DataType::VECTOR>(data);
				break;
			default:
				throw std::invalid_argument("Unknown DataType");
		}
	}
}
