/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/foundation/typed-data.hpp>

namespace arc
{
	TypedData::TypedData()
	{
		std::memset(storage, 0, MAX_SIZE);
	}

	TypedData::~TypedData()
	{
		destroy();
	}

	TypedData::TypedData(const TypedData &other)
	{
		copy_from(other);
	}

	TypedData::TypedData(TypedData &&other) noexcept
	{
		move_from(std::move(other));
	}

	TypedData &TypedData::operator=(const TypedData &other)
	{
		if (this != &other)
		{
			destroy();
			copy_from(other);
		}
		return *this;
	}

	TypedData &TypedData::operator=(TypedData &&other) noexcept
	{
		if (this != &other)
		{
			destroy();
			move_from(std::move(other));
		}
		return *this;
	}

	DataType TypedData::type() const
	{
		return current_type;
	}

	void TypedData::destroy()
	{
		if (current_type == DataType::VOID)
			return;

		if (current_type == DataType::FUNCTION)
		{
			auto& fn_data = *std::launder(reinterpret_cast<DataTraits<DataType::FUNCTION>::value*>(storage));
			if (fn_data.return_type)
			{
				ach::shared_allocator<TypedData> alloc;
				std::destroy_at(fn_data.return_type);
				alloc.deallocate(fn_data.return_type, 1);
			}
			std::destroy_at(&fn_data);
			current_type = DataType::VOID;
			return;
		}

		/* dispatch destruction based on type */
		switch (current_type)
		{
#define ARC_TYPEDATA_DESTROY(dt) \
   case DataType::dt: \
   	std::destroy_at(reinterpret_cast<typename DataTraits<DataType::dt>::value*>(storage)); \
   	break;

			ARC_TYPEDATA_DESTROY(BOOL)
			ARC_TYPEDATA_DESTROY(INT8)
			ARC_TYPEDATA_DESTROY(INT16)
			ARC_TYPEDATA_DESTROY(INT32)
			ARC_TYPEDATA_DESTROY(INT64)
			ARC_TYPEDATA_DESTROY(UINT8)
			ARC_TYPEDATA_DESTROY(UINT16)
			ARC_TYPEDATA_DESTROY(UINT32)
			ARC_TYPEDATA_DESTROY(UINT64)
			ARC_TYPEDATA_DESTROY(FLOAT32)
			ARC_TYPEDATA_DESTROY(FLOAT64)
			ARC_TYPEDATA_DESTROY(VECTOR)
			ARC_TYPEDATA_DESTROY(POINTER)
			ARC_TYPEDATA_DESTROY(ARRAY)
			ARC_TYPEDATA_DESTROY(STRUCT)
			ARC_TYPEDATA_DESTROY(FUNCTION)
			ARC_TYPEDATA_DESTROY(VOID)

#undef ARC_TYPEDATA_DESTROY

			default:
				break;
		}
		current_type = DataType::VOID;
	}

	void TypedData::copy_from(const TypedData &other)
	{
		current_type = other.current_type;
		if (current_type == DataType::VOID)
		{
			std::memset(storage, 0, MAX_SIZE);
			return;
		}

		if (current_type == DataType::FUNCTION)
		{
			const auto& other_fn_data = other.get<DataType::FUNCTION>();
			DataTraits<DataType::FUNCTION>::value fn_data;
			if (other_fn_data.return_type)
			{
				ach::shared_allocator<TypedData> alloc;
				TypedData* new_return_type = alloc.allocate(1);
				std::construct_at(new_return_type, *other_fn_data.return_type);
				fn_data.return_type = new_return_type;
			}
			else
			{
				fn_data.return_type = nullptr;
			}

			std::construct_at(reinterpret_cast<DataTraits<DataType::FUNCTION>::value*>(storage), fn_data);
			return;
		}

		/* dispatch copy construction based on type */
		switch (current_type)
		{
#define ARC_TYPEDATA_COPY(dt) \
   case DataType::dt: \
   	std::construct_at(reinterpret_cast<typename DataTraits<DataType::dt>::value*>(storage), \
   					 other.get<DataType::dt>()); \
   	break;

			ARC_TYPEDATA_COPY(BOOL)
			ARC_TYPEDATA_COPY(INT8)
			ARC_TYPEDATA_COPY(INT16)
			ARC_TYPEDATA_COPY(INT32)
			ARC_TYPEDATA_COPY(INT64)
			ARC_TYPEDATA_COPY(UINT8)
			ARC_TYPEDATA_COPY(UINT16)
			ARC_TYPEDATA_COPY(UINT32)
			ARC_TYPEDATA_COPY(UINT64)
			ARC_TYPEDATA_COPY(FLOAT32)
			ARC_TYPEDATA_COPY(FLOAT64)
			ARC_TYPEDATA_COPY(VECTOR)
			ARC_TYPEDATA_COPY(POINTER)
			ARC_TYPEDATA_COPY(ARRAY)
			ARC_TYPEDATA_COPY(STRUCT)
			ARC_TYPEDATA_COPY(FUNCTION)
			ARC_TYPEDATA_COPY(VOID)

#undef ARC_TYPEDATA_COPY

			/* the fallback isn't 100% reliable but if we are getting
			 * UB from here then it's definitely this. we would need to cover
			 * the move safely regardless of types */
			default:
				std::memcpy(storage, other.storage, MAX_SIZE);
				break;
		}
	}

	void TypedData::move_from(TypedData &&other) noexcept
	{
		current_type = other.current_type;
		if (current_type == DataType::VOID)
		{
			std::memset(storage, 0, MAX_SIZE);
			return;
		}

		if (current_type == DataType::FUNCTION)
		{
			auto& other_fn_data = other.get<DataType::FUNCTION>();
			DataTraits<DataType::FUNCTION>::value fn_data;

			fn_data.return_type = other_fn_data.return_type;
			other_fn_data.return_type = nullptr;

			std::construct_at(reinterpret_cast<DataTraits<DataType::FUNCTION>::value*>(storage), fn_data);

			other.current_type = DataType::VOID;
			return;
		}

		/* dispatch */
		switch (current_type)
		{
#define ARC_TYPEDATA_MOVE(dt) \
case DataType::dt: \
std::construct_at(reinterpret_cast<typename DataTraits<DataType::dt>::value*>(storage), \
std::move(other.get<DataType::dt>())); \
break;

			ARC_TYPEDATA_MOVE(BOOL)
			ARC_TYPEDATA_MOVE(INT8)
			ARC_TYPEDATA_MOVE(INT16)
			ARC_TYPEDATA_MOVE(INT32)
			ARC_TYPEDATA_MOVE(INT64)
			ARC_TYPEDATA_MOVE(UINT8)
			ARC_TYPEDATA_MOVE(UINT16)
			ARC_TYPEDATA_MOVE(UINT32)
			ARC_TYPEDATA_MOVE(UINT64)
			ARC_TYPEDATA_MOVE(FLOAT32)
			ARC_TYPEDATA_MOVE(FLOAT64)
			ARC_TYPEDATA_MOVE(VECTOR)
			ARC_TYPEDATA_MOVE(POINTER)
			ARC_TYPEDATA_MOVE(ARRAY)
			ARC_TYPEDATA_MOVE(STRUCT)
			ARC_TYPEDATA_MOVE(FUNCTION)
			ARC_TYPEDATA_MOVE(VOID)

#undef ARC_TYPEDATA_MOVE

			default:
				/* shouldn't hit here as we handled all
				 * enum types in the switch cases but if it does then it's probably
				 * going to cause a UB */
				std::memcpy(storage, other.storage, MAX_SIZE);
				break;
		}

		/* and then destroy the moved-from object */
		other.destroy();
	}
}
