#include <Columns/ColumnNullable.h>
#include <DataTypes/DataTypeDateTime.h>
#include <DataTypes/DataTypeDecimal.h>
#include <DataTypes/DataTypeMyDate.h>
#include <DataTypes/DataTypeMyDateTime.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/isSupportedDataTypeCast.h>
#include <Functions/FunctionHelpers.h>
#include <Storages/DeltaMerge/DeltaMergeHelpers.h>
#include <Storages/DeltaMerge/convertColumnTypeHelpers.h>

namespace DB
{
namespace DM
{

namespace
{

/// some helper functions for casting column data type

template <typename TypeFrom, typename TypeTo>
void insertRangeFromWithNumericTypeCast(const ColumnPtr &    from_col, //
                                        const ColumnPtr &    null_map,
                                        const ColumnDefine & read_define,
                                        MutableColumnPtr &   to_col,
                                        size_t               rows_offset,
                                        size_t               rows_limit)
{
    // Caller should ensure that both from_col / to_col
    // * is numeric
    // * no nullable wrapper
    // * both signed or unsigned
    static_assert(std::is_integral_v<TypeFrom>);
    static_assert(std::is_integral_v<TypeTo>);
    constexpr bool is_both_signed_or_unsigned = !(std::is_unsigned_v<TypeFrom> ^ std::is_unsigned_v<TypeTo>);
    static_assert(is_both_signed_or_unsigned);
    assert(from_col != nullptr);
    assert(to_col != nullptr);
    assert(from_col->isNumeric());
    assert(to_col->isNumeric());
    assert(!from_col->isColumnNullable());
    assert(!to_col->isColumnNullable());
    assert(!from_col->isColumnConst());
    assert(!to_col->isColumnConst());

    // Something like `insertRangeFrom(from_col, rows_offset, rows_limit)` with static_cast
    const PaddedPODArray<TypeFrom> & from_array   = toColumnVectorData<TypeFrom>(from_col);
    PaddedPODArray<TypeTo> *         to_array_ptr = toMutableColumnVectorDataPtr<TypeTo>(to_col);
    to_array_ptr->reserve(rows_limit);
    for (size_t i = 0; i < rows_limit; ++i)
    {
        (*to_array_ptr).emplace_back(static_cast<TypeTo>(from_array[rows_offset + i]));
    }

    if (unlikely(null_map))
    {
        /// We are applying cast from nullable to not null, scan to fill "NULL" with default value

        TypeTo default_value = 0; // if read_define.default_value is empty, fill with 0
        if (read_define.default_value.isNull())
        {
            // Do nothing
        }
        else if (read_define.default_value.getType() == Field::Types::Int64)
        {
            default_value = read_define.default_value.safeGet<Int64>();
        }
        else if (read_define.default_value.getType() == Field::Types::UInt64)
        {
            default_value = read_define.default_value.safeGet<UInt64>();
        }
        else
        {
            throw Exception("Invalid column value type", ErrorCodes::BAD_ARGUMENTS);
        }

        const size_t to_offset_before_inserted = to_array_ptr->size() - rows_limit;

        for (size_t i = 0; i < rows_limit; ++i)
        {
            const size_t to_offset = to_offset_before_inserted + i;
            if (null_map->getInt(rows_offset + i) != 0)
            {
                // `from_col[rows_offset + i]` is "NULL", fill `to_col[x]` with default value
                (*to_array_ptr)[to_offset] = static_cast<TypeTo>(default_value);
            }
        }
    }
}


bool castNonNullNumericColumn(const DataTypePtr &  disk_type_not_null_,
                              const ColumnPtr &    disk_col_not_null,
                              const ColumnDefine & read_define,
                              const ColumnPtr &    null_map,
                              MutableColumnPtr &   memory_col_not_null,
                              size_t               rows_offset,
                              size_t               rows_limit)
{
    /// Caller should ensure that type is not nullable
    assert(disk_type_not_null_ != nullptr);
    assert(disk_col_not_null != nullptr);
    assert(read_define.type != nullptr);
    assert(memory_col_not_null != nullptr);

    const IDataType * disk_type_not_null = disk_type_not_null_.get();
    const IDataType * read_type_not_null = read_define.type.get();

    /// Caller should ensure nullable is unwrapped
    assert(!disk_type_not_null->isNullable());
    assert(!read_type_not_null->isNullable());

    /// Caller should ensure that dist_type != read_type
    assert(!disk_type_not_null->equals(*read_type_not_null));

    if (checkDataType<DataTypeUInt32>(disk_type_not_null))
    {
        using FromType = UInt32;
        if (checkDataType<DataTypeUInt64>(read_type_not_null))
        {
            insertRangeFromWithNumericTypeCast<FromType, UInt64>(
                disk_col_not_null, null_map, read_define, memory_col_not_null, rows_offset, rows_limit);
            return true;
        }
    }
    else if (checkDataType<DataTypeInt32>(disk_type_not_null))
    {
        using FromType = Int32;
        if (checkDataType<DataTypeInt64>(read_type_not_null))
        {
            insertRangeFromWithNumericTypeCast<FromType, Int64>(
                disk_col_not_null, null_map, read_define, memory_col_not_null, rows_offset, rows_limit);
            return true;
        }
    }
    else if (checkDataType<DataTypeUInt16>(disk_type_not_null))
    {
        using FromType = UInt16;
        if (checkDataType<DataTypeUInt32>(read_type_not_null))
        {
            insertRangeFromWithNumericTypeCast<FromType, UInt32>(
                disk_col_not_null, null_map, read_define, memory_col_not_null, rows_offset, rows_limit);
            return true;
        }
        else if (checkDataType<DataTypeUInt64>(read_type_not_null))
        {
            insertRangeFromWithNumericTypeCast<FromType, UInt64>(
                disk_col_not_null, null_map, read_define, memory_col_not_null, rows_offset, rows_limit);
            return true;
        }
    }
    else if (checkDataType<DataTypeInt16>(disk_type_not_null))
    {
        using FromType = Int16;
        if (checkDataType<DataTypeInt32>(read_type_not_null))
        {
            insertRangeFromWithNumericTypeCast<FromType, Int32>(
                disk_col_not_null, null_map, read_define, memory_col_not_null, rows_offset, rows_limit);
            return true;
        }
        else if (checkDataType<DataTypeInt64>(read_type_not_null))
        {
            insertRangeFromWithNumericTypeCast<FromType, Int64>(
                disk_col_not_null, null_map, read_define, memory_col_not_null, rows_offset, rows_limit);
            return true;
        }
    }
    else if (checkDataType<DataTypeUInt8>(disk_type_not_null))
    {
        using FromType = UInt8;
        if (checkDataType<DataTypeUInt32>(read_type_not_null))
        {
            insertRangeFromWithNumericTypeCast<FromType, UInt32>(
                disk_col_not_null, null_map, read_define, memory_col_not_null, rows_offset, rows_limit);
            return true;
        }
        else if (checkDataType<DataTypeUInt64>(read_type_not_null))
        {
            insertRangeFromWithNumericTypeCast<FromType, UInt64>(
                disk_col_not_null, null_map, read_define, memory_col_not_null, rows_offset, rows_limit);
            return true;
        }
        else if (checkDataType<DataTypeUInt16>(read_type_not_null))
        {
            insertRangeFromWithNumericTypeCast<FromType, UInt16>(
                disk_col_not_null, null_map, read_define, memory_col_not_null, rows_offset, rows_limit);
            return true;
        }
    }
    else if (checkDataType<DataTypeInt8>(disk_type_not_null))
    {
        using FromType = Int8;
        if (checkDataType<DataTypeInt32>(read_type_not_null))
        {
            insertRangeFromWithNumericTypeCast<FromType, Int32>(
                disk_col_not_null, null_map, read_define, memory_col_not_null, rows_offset, rows_limit);
            return true;
        }
        else if (checkDataType<DataTypeInt64>(read_type_not_null))
        {
            insertRangeFromWithNumericTypeCast<FromType, Int64>(
                disk_col_not_null, null_map, read_define, memory_col_not_null, rows_offset, rows_limit);
            return true;
        }
        else if (checkDataType<DataTypeInt16>(read_type_not_null))
        {
            insertRangeFromWithNumericTypeCast<FromType, Int16>(
                disk_col_not_null, null_map, read_define, memory_col_not_null, rows_offset, rows_limit);
            return true;
        }
    }

    // else is not support
    return false;
}

} // namespace

void convertColumnByColumnDefine(const DataTypePtr &  disk_type,
                                 const ColumnPtr &    disk_col,
                                 const ColumnDefine & read_define,
                                 MutableColumnPtr     memory_col,
                                 size_t               rows_offset,
                                 size_t               rows_limit)
{
    const DataTypePtr & read_type = read_define.type;

    // Unwrap nullable(what)
    ColumnPtr        disk_col_not_null;
    MutableColumnPtr memory_col_not_null;
    ColumnPtr        null_map;
    DataTypePtr      disk_type_not_null = disk_type;
    DataTypePtr      read_type_not_null = read_type;
    if (disk_type->isNullable() && read_type->isNullable())
    {
        // nullable -> nullable, copy null map
        const auto & disk_nullable_col   = typeid_cast<const ColumnNullable &>(*disk_col);
        const auto & disk_null_map       = disk_nullable_col.getNullMapData();
        auto &       memory_nullable_col = typeid_cast<ColumnNullable &>(*memory_col);
        auto &       memory_null_map     = memory_nullable_col.getNullMapData();
        memory_null_map.insert(disk_null_map.begin(), disk_null_map.end());

        disk_col_not_null   = disk_nullable_col.getNestedColumnPtr();
        memory_col_not_null = memory_nullable_col.getNestedColumn().getPtr();

        const auto * type_nullable = typeid_cast<const DataTypeNullable *>(disk_type.get());
        disk_type_not_null         = type_nullable->getNestedType();
        type_nullable              = typeid_cast<const DataTypeNullable *>(read_type.get());
        read_type_not_null         = type_nullable->getNestedType();
    }
    else if (!disk_type->isNullable() && read_type->isNullable())
    {
        // not null -> nullable, set null map to all not null
        auto & memory_nullable_col = typeid_cast<ColumnNullable &>(*memory_col);
        auto & nullmap_data        = memory_nullable_col.getNullMapData();
        nullmap_data.resize_fill(rows_offset + rows_limit, 0);

        disk_col_not_null   = disk_col;
        memory_col_not_null = memory_nullable_col.getNestedColumn().getPtr();

        const auto * type_nullable = typeid_cast<const DataTypeNullable *>(read_type.get());
        read_type_not_null         = type_nullable->getNestedType();
    }
    else if (disk_type->isNullable() && !read_type->isNullable())
    {
        // nullable -> not null, fill "NULL" values with default value later
        const auto & disk_nullable_col = typeid_cast<const ColumnNullable &>(*disk_col);
        null_map                       = disk_nullable_col.getNullMapColumnPtr();
        disk_col_not_null              = disk_nullable_col.getNestedColumnPtr();
        memory_col_not_null            = std::move(memory_col);

        const auto * type_nullable = typeid_cast<const DataTypeNullable *>(disk_type.get());
        disk_type_not_null         = type_nullable->getNestedType();
    }
    else
    {
        // not null -> not null
        disk_col_not_null   = disk_col;
        memory_col_not_null = std::move(memory_col);
    }

    assert(memory_col_not_null != nullptr);
    assert(disk_col_not_null != nullptr);
    assert(read_type_not_null != nullptr);
    assert(disk_type_not_null != nullptr);

    ColumnDefine read_define_not_null(read_define);
    read_define_not_null.type = read_type_not_null;
    if (disk_type_not_null->equals(*read_type_not_null))
    {
        // just change from nullable -> not null / not null -> nullable
        memory_col_not_null->insertRangeFrom(*disk_col_not_null, rows_offset, rows_limit);

        if (null_map)
        {
            /// We are applying cast from nullable to not null, scan to fill "NULL" with default value

            for (size_t i = 0; i < rows_limit; ++i)
            {
                if (unlikely(null_map->getInt(i) != 0))
                {
                    // `from_col[i]` is "NULL", fill `to_col[rows_offset + i]` with default value
                    // TiDB/MySQL don't support this, should not call here.
                    throw Exception("Reading mismatch data type pack. Cast from " + disk_type->getName() + " to " + read_type->getName()
                                        + " with \"NULL\" value is NOT supported!",
                                    ErrorCodes::NOT_IMPLEMENTED);
                }
            }
        }
    }
    else if (!castNonNullNumericColumn(
                 disk_type_not_null, disk_col_not_null, read_define_not_null, null_map, memory_col_not_null, rows_offset, rows_limit))
    {
        throw Exception("Reading mismatch data type pack. Cast and assign from " + disk_type->getName() + " to " + read_type->getName()
                            + " is NOT supported!",
                        ErrorCodes::NOT_IMPLEMENTED);
    }
}

ColumnPtr convertColumnByColumnDefineIfNeed(const DataTypePtr & from_type, ColumnPtr && from_col, const ColumnDefine & to_column_define)
{
    // No need to convert
    if (likely(from_type->equals(*to_column_define.type)))
        return std::move(from_col);

    // Check if support
    if (unlikely(!isSupportedDataTypeCast(from_type, to_column_define.type)))
    {
        throw Exception("Reading mismatch data type pack. Cast from " + from_type->getName() + " to " + to_column_define.type->getName()
                            + " is NOT supported!",
                        ErrorCodes::NOT_IMPLEMENTED);
    }

    // Cast column's data from DataType in disk to what we need now
    auto to_col = to_column_define.type->createColumn();
    to_col->reserve(from_col->size());
    convertColumnByColumnDefine(from_type, from_col, to_column_define, to_col->getPtr(), 0, from_col->size());
    return to_col;
}

ColumnPtr createColumnWithDefaultValue(const ColumnDefine & column_define, size_t num_rows)
{
    ColumnPtr column;
    // Read default value from `column_define.default_value`
    if (column_define.default_value.isNull())
    {
        column = column_define.type->createColumnConstWithDefaultValue(num_rows);
    }
    else
    {
        column = column_define.type->createColumnConst(num_rows, column_define.default_value);
    }
    column = column->convertToFullColumnIfConst();
    return column;
}

} // namespace DM
} // namespace DB
