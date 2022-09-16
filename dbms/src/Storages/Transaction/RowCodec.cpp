#include <Storages/Transaction/RowCodec.h>

namespace DB
{

DecodedFields::const_iterator findByColumnID(Int64 col_id, const DecodedFields & row)
{
    const static auto cmp = [](const DecodedField & e, const Int64 cid) -> bool { return e.col_id < cid; };
    auto it = std::lower_bound(row.cbegin(), row.cend(), col_id, cmp);
    if (it != row.cend() && it->col_id == col_id)
        return it;
    return row.cend();
}

template <typename T>
static T decodeUInt(size_t & cursor, const TiKVValue::Base & raw_value)
{
    T res = readLittleEndian<T>(&raw_value[cursor]);
    cursor += sizeof(T);
    return res;
}

template <typename T>
static void encodeUInt(T u, std::stringstream & ss)
{
    u = toLittleEndian(u);
    ss.write(reinterpret_cast<const char *>(&u), sizeof(u));
}

template <typename Target, typename Source>
static void decodeUInts(size_t & cursor, const TiKVValue::Base & raw_value, size_t n, std::vector<Target> & target)
{
    target.reserve(n);
    for (size_t i = 0; i < n; i++)
        target.emplace_back(decodeUInt<Source>(cursor, raw_value));
}

template <typename Sign, typename Source>
static Sign castIntWithSign(Source i)
{
    if constexpr (std::is_signed_v<Sign>)
        return static_cast<Sign>(static_cast<std::make_signed_t<Source>>(i));
    else
        return static_cast<Sign>(static_cast<std::make_unsigned_t<Source>>(i));
}

template <typename T>
static T decodeIntWithLength(const TiKVValue::Base & raw_value, size_t pos, size_t length)
{
    switch (length)
    {
        case sizeof(UInt8):
            return castIntWithSign<T>(decodeUInt<UInt8>(pos, raw_value));
        case sizeof(UInt16):
            return castIntWithSign<T>(decodeUInt<UInt16>(pos, raw_value));
        case sizeof(UInt32):
            return castIntWithSign<T>(decodeUInt<UInt32>(pos, raw_value));
        case sizeof(UInt64):
            return castIntWithSign<T>(decodeUInt<UInt64>(pos, raw_value));
        default:
            throw Exception(std::string("Invalid integer length ") + std::to_string(length) + " at position " + std::to_string(pos)
                    + " in value: " + raw_value,
                ErrorCodes::LOGICAL_ERROR);
    }
}

template <typename Target, typename Sign, typename = std::enable_if_t<std::is_signed_v<Sign>>>
static std::make_signed_t<Target> castIntWithLength(Sign i)
{
    return static_cast<std::make_signed_t<Target>>(i);
}

template <typename Target, typename Sign, typename = std::enable_if_t<std::is_unsigned_v<Sign>>>
static std::make_unsigned_t<Target> castIntWithLength(Sign i)
{
    return static_cast<std::make_unsigned_t<Target>>(i);
}

template <typename T>
static void encodeIntWithLength(T i, std::stringstream & ss)
{
    if (castIntWithLength<UInt64>(castIntWithLength<UInt8>(i)) == i)
        encodeUInt(static_cast<UInt8>(castIntWithLength<UInt8>(i)), ss);
    else if (castIntWithLength<UInt64>(castIntWithLength<UInt16>(i)) == i)
        encodeUInt(static_cast<UInt16>(castIntWithLength<UInt16>(i)), ss);
    else if (castIntWithLength<UInt64>(castIntWithLength<UInt32>(i)) == i)
        encodeUInt(static_cast<UInt32>(castIntWithLength<UInt32>(i)), ss);
    else
        encodeUInt(static_cast<UInt64>(i), ss);
}

enum struct RowCodecVer : UInt8
{
    ROW_V2 = 128,
};

bool hasMissingColumns(const DecodedFields & decoded_fields, const TableInfo & table_info, const ColumnIdToIndex & column_lut)
{
    for (const auto & id_to_idx : column_lut)
    {
        const auto & column_info = table_info.columns[id_to_idx.second];
        if (auto it = findByColumnID(column_info.id, decoded_fields); it != decoded_fields.end())
            continue;
        // We consider a missing column could be safely filled with NULL, unless it has not default value and is NOT NULL.
        // This could saves lots of unnecessary schema syncs for old data with a schema that has newly added columns.
        // for clustered index, if the pk column does not exists, it can still be decoded from the key
        if (!(table_info.is_common_handle && column_info.hasPriKeyFlag()) && column_info.hasNoDefaultValueFlag()
            && column_info.hasNotNullFlag())
            return true;
    }
    return false;
}

DecodedRow * decodeRowV1(const TiKVValue::Base & raw_value, const TableInfo & table_info, const ColumnIdToIndex & column_lut)
{
    /// Decode fields based on codec flag, fill into decoded_fields.
    DecodedFields decoded_fields;
    {
        size_t cursor = 0;

        while (cursor < raw_value.size())
        {
            Field f = DecodeDatum(cursor, raw_value);
            if (f.isNull())
                break;
            ColumnID col_id = f.get<ColumnID>();
            decoded_fields.emplace_back(col_id, DecodeDatum(cursor, raw_value));
        }

        if (cursor != raw_value.size())
            throw Exception(std::string(__PRETTY_FUNCTION__) + ": cursor is not end, remaining: " + raw_value.substr(cursor),
                ErrorCodes::LOGICAL_ERROR);

        {
            // must be sorted, for binary search.
            ::std::sort(decoded_fields.begin(), decoded_fields.end());
        }
    }

    /// Analyze schema, fill unknown_fields and has_missing_columns.
    DecodedFields unknown_fields;
    bool has_missing_columns = false;
    {
        bool schema_match = decoded_fields.size() == column_lut.size()
            && std::all_of(decoded_fields.cbegin(), decoded_fields.cend(),
                [&column_lut](const auto & field) { return column_lut.count(field.col_id); });

        if (!schema_match)
        {
            DecodedFields tmp;
            tmp.reserve(decoded_fields.size());
            for (auto && item : decoded_fields)
            {
                if (column_lut.count(item.col_id))
                    tmp.emplace_back(std::move(item));
                else
                    unknown_fields.emplace_back(std::move(item));
            }
            tmp.swap(decoded_fields);
        }

        has_missing_columns = hasMissingColumns(decoded_fields, table_info, column_lut);
    }

    /// Pack them all and return.
    return new DecodedRow(has_missing_columns, std::move(unknown_fields), true, std::move(decoded_fields));
}

namespace RowV2
{
static constexpr UInt8 BigRowMask = 0x1;

template <bool is_big = false>
struct Types
{
    using ColumnIDType = UInt8;
    using ValueOffsetType = UInt16;
};

template <>
struct Types<true>
{
    using ColumnIDType = UInt32;
    using ValueOffsetType = UInt32;
};

/// Specific decode methods for individual types.
/// Copied from https://github.com/pingcap/tidb/blob/master/util/rowcodec/decoder.go#L111
/// Should be always kept identical with it.
Field decodeNotNullColumn(size_t cursor, const TiKVValue::Base & raw_value, size_t length, const ColumnInfo & column_info)
{
    switch (column_info.tp)
    {
        case TiDB::TypeLongLong:
        case TiDB::TypeLong:
        case TiDB::TypeInt24:
        case TiDB::TypeShort:
        case TiDB::TypeTiny:
            if (column_info.hasUnsignedFlag())
                return decodeIntWithLength<UInt64>(raw_value, cursor, length);
            else
                return decodeIntWithLength<Int64>(raw_value, cursor, length);
        case TiDB::TypeYear:
            return decodeIntWithLength<Int64>(raw_value, cursor, length);
        case TiDB::TypeFloat:
        case TiDB::TypeDouble:
            return DecodeFloat64(cursor, raw_value);
        case TiDB::TypeVarString:
        case TiDB::TypeVarchar:
        case TiDB::TypeString:
        case TiDB::TypeBlob:
        case TiDB::TypeTinyBlob:
        case TiDB::TypeMediumBlob:
        case TiDB::TypeLongBlob:
        case TiDB::TypeJSON: // JSON portion could be quickly navigated by length, instead of doing real decoding.
            return raw_value.substr(cursor, length);
        case TiDB::TypeNewDecimal:
            return DecodeDecimal(cursor, raw_value);
        case TiDB::TypeTimestamp:
        case TiDB::TypeDate:
        case TiDB::TypeDatetime:
        case TiDB::TypeBit:
        case TiDB::TypeSet:
        case TiDB::TypeEnum:
            return decodeIntWithLength<UInt64>(raw_value, cursor, length);
        case TiDB::TypeTime:
            return decodeIntWithLength<Int64>(raw_value, cursor, length);
        default:
            throw Exception(std::string("Invalid TP ") + std::to_string(column_info.tp) + " of column " + column_info.name);
    }
}

TiKVValue::Base encodeNotNullColumn(const Field & field, const ColumnInfo & column_info)
{
    std::stringstream ss;

    switch (column_info.tp)
    {
        case TiDB::TypeLongLong:
        case TiDB::TypeLong:
        case TiDB::TypeInt24:
        case TiDB::TypeShort:
        case TiDB::TypeTiny:
            column_info.hasUnsignedFlag() ? encodeIntWithLength(field.safeGet<UInt64>(), ss)
                                          : encodeIntWithLength(field.safeGet<Int64>(), ss);
            break;
        case TiDB::TypeYear:
            encodeIntWithLength(field.safeGet<Int64>(), ss);
            break;
        case TiDB::TypeFloat:
        case TiDB::TypeDouble:
            EncodeFloat64(field.safeGet<Float64>(), ss);
            break;
        case TiDB::TypeVarString:
        case TiDB::TypeVarchar:
        case TiDB::TypeString:
        case TiDB::TypeBlob:
        case TiDB::TypeTinyBlob:
        case TiDB::TypeMediumBlob:
        case TiDB::TypeLongBlob:
        case TiDB::TypeJSON:
            return field.safeGet<String>();
        case TiDB::TypeNewDecimal:
            EncodeDecimalForRow(field, ss, column_info);
            break;
        case TiDB::TypeTimestamp:
        case TiDB::TypeDate:
        case TiDB::TypeDatetime:
        case TiDB::TypeBit:
        case TiDB::TypeSet:
        case TiDB::TypeEnum:
            encodeIntWithLength(field.safeGet<UInt64>(), ss);
            break;
        case TiDB::TypeTime:
            encodeIntWithLength(field.safeGet<Int64>(), ss);
            break;
        default:
            throw Exception(std::string("Invalid TP ") + std::to_string(column_info.tp) + " of column " + column_info.name);
    }

    return ss.str();
}
} // namespace RowV2

template <bool is_big>
struct RowDecoderV2
{
    RowDecoderV2(const TiKVValue::Base & raw_value_, const TableInfo & table_info_, const ColumnIdToIndex & column_lut_)
        : raw_value(raw_value_), table_info(table_info_), column_lut(column_lut_)
    {}

    DecodedRow * decode() &&
    {
        size_t cursor = 2; // Skip the initial codec ver and row flag.
        num_not_null_columns = decodeUInt<UInt16>(cursor, raw_value);
        num_null_columns = decodeUInt<UInt16>(cursor, raw_value);
        decodeUInts<ColumnID, typename RowV2::Types<is_big>::ColumnIDType>(cursor, raw_value, num_not_null_columns, not_null_column_ids);
        decodeUInts<ColumnID, typename RowV2::Types<is_big>::ColumnIDType>(cursor, raw_value, num_null_columns, null_column_ids);
        decodeUInts<size_t, typename RowV2::Types<is_big>::ValueOffsetType>(cursor, raw_value, num_not_null_columns, value_offsets);
        values_start_pos = cursor; // Values starts from current cursor.

        DecodedFields decoded_fields, unknown_fields;
        size_t id_not_null = 0, id_null = 0;
        // Merge ordered not null/null columns to keep order.
        while (id_not_null < not_null_column_ids.size() || id_null < null_column_ids.size())
        {
            bool is_null;
            if (id_not_null < not_null_column_ids.size() && id_null < null_column_ids.size())
                is_null = not_null_column_ids[id_not_null] > null_column_ids[id_null];
            else
                is_null = id_null < null_column_ids.size();

            auto res = is_null ? decodeNullColumn(id_null++) : decodeNotNullColumn(id_not_null++);
            if (std::get<1>(res))
                unknown_fields.emplace_back(std::move(std::get<0>(res)));
            else
                decoded_fields.emplace_back(std::move(std::get<0>(res)));
        }

        bool has_missing_columns = hasMissingColumns(decoded_fields, table_info, column_lut);
        return new DecodedRow(has_missing_columns, std::move(unknown_fields), false, std::move(decoded_fields));
    }

private:
    /// Returns <decoded (null) field, is unknown>.
    std::tuple<DecodedField, bool> decodeNullColumn(size_t id)
    {
        return std::make_tuple(DecodedField{null_column_ids[id], Field()}, !column_lut.count(null_column_ids[id]));
    }

    /// Returns <decoded field, is unknown>.
    std::tuple<DecodedField, bool> decodeNotNullColumn(size_t id)
    {
        ColumnID column_id = not_null_column_ids[id];
        size_t start = id ? value_offsets[id - 1] : 0;
        size_t length = value_offsets[id] - start;

        if (!column_lut.count(column_id))
            // Unknown column, preserve its bytes (string) portion in the raw value.
            return std::make_tuple(DecodedField{column_id, raw_value.substr(values_start_pos + start, length)}, true);

        const auto & column_info = table_info.columns[column_lut.find(column_id)->second];
        size_t cursor = values_start_pos + start;
        return std::make_tuple(DecodedField{column_id, RowV2::decodeNotNullColumn(cursor, raw_value, length, column_info)}, false);
    }

private:
    const TiKVValue::Base & raw_value;
    const TableInfo & table_info;
    const ColumnIdToIndex & column_lut;

    size_t num_not_null_columns = 0;
    size_t num_null_columns = 0;
    std::vector<ColumnID> not_null_column_ids;
    std::vector<ColumnID> null_column_ids;
    std::vector<size_t> value_offsets;
    size_t values_start_pos = 0;
};

DecodedRow * decodeRowV2(const TiKVValue::Base & raw_value, const TableInfo & table_info, const ColumnIdToIndex & column_lut)
{
    size_t cursor = 1; // Skip the initial codec ver.
    UInt8 row_flag = decodeUInt<UInt8>(cursor, raw_value);
    bool is_big = row_flag & RowV2::BigRowMask;
    return is_big ? RowDecoderV2<true>(raw_value, table_info, column_lut).decode()
                  : RowDecoderV2<false>(raw_value, table_info, column_lut).decode();
}

DecodedRow * decodeRow(const TiKVValue::Base & raw_value, const TableInfo & table_info, const ColumnIdToIndex & column_lut)
{
    switch (static_cast<UInt8>(raw_value[0]))
    {
        case static_cast<UInt8>(RowCodecVer::ROW_V2):
            return decodeRowV2(raw_value, table_info, column_lut);
        default:
            return decodeRowV1(raw_value, table_info, column_lut);
    }
}

Field decodeUnknownColumnV2(const Field & unknown, const ColumnInfo & column_info)
{
    if (unknown.isNull())
        return Field();

    const auto & raw_value = unknown.safeGet<TiKVValue::Base>();
    return RowV2::decodeNotNullColumn(0, raw_value, raw_value.length(), column_info);
}

void encodeRowV1(const TiDB::TableInfo & table_info, const std::vector<Field> & fields, std::stringstream & ss)
{
    size_t column_in_key = 0;
    if (table_info.pk_is_handle)
        column_in_key = 1;
    else if (table_info.is_common_handle)
        column_in_key = table_info.getPrimaryIndexInfo().idx_cols.size();
    if (table_info.columns.size() < fields.size() + column_in_key)
        throw Exception(std::string("Encoding row has ") + std::to_string(table_info.columns.size()) + " columns but "
                + std::to_string(fields.size() + table_info.pk_is_handle) + " values: ",
            ErrorCodes::LOGICAL_ERROR);

    size_t encoded_fields_idx = 0;
    for (auto & column_info : table_info.columns)
    {
        if ((table_info.pk_is_handle || table_info.is_common_handle) && column_info.hasPriKeyFlag())
            continue;
        EncodeDatum(Field(column_info.id), TiDB::CodecFlagInt, ss);
        EncodeDatumForRow(fields[encoded_fields_idx++], column_info.getCodecFlag(), ss, column_info);
        if (encoded_fields_idx == fields.size())
            break;
    }
}

struct RowEncoderV2
{
    RowEncoderV2(const TableInfo & table_info_, const std::vector<Field> & fields_) : table_info(table_info_), fields(fields_) {}

    void encode(std::stringstream & ss) &&
    {
        size_t column_in_key = 0;
        if (table_info.pk_is_handle)
            column_in_key = 1;
        else if (table_info.is_common_handle)
            column_in_key = table_info.getPrimaryIndexInfo().idx_cols.size();
        if (table_info.columns.size() < fields.size() + column_in_key)
            throw Exception(std::string("Encoding row has ") + std::to_string(table_info.columns.size()) + " columns but "
                    + std::to_string(fields.size() + table_info.pk_is_handle) + " values: ",
                ErrorCodes::LOGICAL_ERROR);

        bool is_big = false;
        size_t value_length = 0;

        /// Cache encoded individual columns.
        for (size_t i_col = 0, i_val = 0; i_col < table_info.columns.size(); i_col++)
        {
            const auto & column_info = table_info.columns[i_col];
            const auto & field = fields[i_val];
            if ((table_info.pk_is_handle || table_info.is_common_handle) && column_info.hasPriKeyFlag())
                continue;
            if (column_info.id > std::numeric_limits<typename RowV2::Types<false>::ColumnIDType>::max())
                is_big = true;
            if (!field.isNull())
            {
                num_not_null_columns++;
                auto value = RowV2::encodeNotNullColumn(field, column_info);
                value_length += value.length();
                not_null_column_id_values.emplace(column_info.id, std::move(value));
            }
            else
            {
                num_null_columns++;
                null_column_ids.emplace(column_info.id);
            }
            i_val++;

            if (i_val == fields.size())
                break;
        }
        is_big = is_big || value_length > std::numeric_limits<RowV2::Types<false>::ValueOffsetType>::max();

        /// Encode header.
        encodeUInt(UInt8(RowCodecVer::ROW_V2), ss);
        UInt8 row_flag = 0;
        row_flag |= is_big ? RowV2::BigRowMask : 0;
        encodeUInt(row_flag, ss);

        /// Encode column numbers and IDs.
        encodeUInt(static_cast<UInt16>(num_not_null_columns), ss);
        encodeUInt(static_cast<UInt16>(num_null_columns), ss);
        is_big ? encodeColumnIDs<RowV2::Types<true>::ColumnIDType>(ss) : encodeColumnIDs<RowV2::Types<false>::ColumnIDType>(ss);

        /// Encode value offsets.
        is_big ? encodeValueOffsets<RowV2::Types<true>::ValueOffsetType>(ss) : encodeValueOffsets<RowV2::Types<false>::ValueOffsetType>(ss);

        /// Encode values.
        encodeValues(ss);
    }

private:
    template <typename T>
    void encodeColumnIDs(std::stringstream & ss)
    {
        for (const auto & not_null_id_val : not_null_column_id_values)
        {
            encodeUInt(static_cast<T>(not_null_id_val.first), ss);
        }
        for (const auto & null_id : null_column_ids)
        {
            encodeUInt(static_cast<T>(null_id), ss);
        }
    }

    template <typename T>
    void encodeValueOffsets(std::stringstream & ss)
    {
        T offset = 0;
        for (const auto & not_null_id_val : not_null_column_id_values)
        {
            offset += static_cast<T>(not_null_id_val.second.length());
            encodeUInt(offset, ss);
        }
    }

    void encodeValues(std::stringstream & ss)
    {
        for (const auto & not_null_id_val : not_null_column_id_values)
        {
            ss << not_null_id_val.second;
        }
    }

private:
    const TableInfo & table_info;
    const std::vector<Field> & fields;

    size_t num_not_null_columns = 0;
    size_t num_null_columns = 0;
    std::map<ColumnID, TiKVValue::Base> not_null_column_id_values;
    std::set<ColumnID> null_column_ids;
};

void encodeRowV2(const TiDB::TableInfo & table_info, const std::vector<Field> & fields, std::stringstream & ss)
{
    RowEncoderV2(table_info, fields).encode(ss);
}

<<<<<<< HEAD
=======
// pre-declar block
template <bool is_big>
bool appendRowV2ToBlockImpl(
    const TiKVValue::Base & raw_value,
    SortedColumnIDWithPosConstIter column_ids_iter,
    SortedColumnIDWithPosConstIter column_ids_iter_end,
    Block & block,
    size_t block_column_pos,
    const ColumnInfos & column_infos,
    ColumnID pk_handle_id,
    bool ignore_pk_if_absent,
    bool force_decode);

bool appendRowV1ToBlock(
    const TiKVValue::Base & raw_value,
    SortedColumnIDWithPosConstIter column_ids_iter,
    SortedColumnIDWithPosConstIter column_ids_iter_end,
    Block & block,
    size_t block_column_pos,
    const ColumnInfos & column_infos,
    ColumnID pk_handle_id,
    bool ignore_pk_if_absent,
    bool force_decode);
// pre-declar block end

bool appendRowToBlock(
    const TiKVValue::Base & raw_value,
    SortedColumnIDWithPosConstIter column_ids_iter,
    SortedColumnIDWithPosConstIter column_ids_iter_end,
    Block & block,
    size_t block_column_pos,
    const DecodingStorageSchemaSnapshotConstPtr & schema_snapshot,
    bool force_decode)
{
    const ColumnInfos & column_infos = schema_snapshot->column_infos;
    // when pk is handle, we need skip pk column when decoding value
    ColumnID pk_handle_id = InvalidColumnID;
    if (schema_snapshot->pk_is_handle)
    {
        pk_handle_id = schema_snapshot->pk_column_ids[0];
    }

    // For pk_is_handle table, the column with primary key flag is decoded from encoded key instead of encoded value.
    // For common handle table, the column with primary key flag is (usually) decoded from encoded key. We skip
    // filling the columns with primary key flags inside this method.
    // For other table (non-clustered, use hidden _tidb_rowid as handle), the column with primary key flags could be
    // changed, we need to fill missing column with default value.
    const bool ignore_pk_if_absent = schema_snapshot->is_common_handle || schema_snapshot->pk_is_handle;

    switch (static_cast<UInt8>(raw_value[0]))
    {
    case static_cast<UInt8>(RowCodecVer::ROW_V2):
    {
        auto row_flag = readLittleEndian<UInt8>(&raw_value[1]);
        bool is_big = row_flag & RowV2::BigRowMask;
        return is_big ? appendRowV2ToBlockImpl<true>(raw_value, column_ids_iter, column_ids_iter_end, block, block_column_pos, column_infos, pk_handle_id, ignore_pk_if_absent, force_decode)
                      : appendRowV2ToBlockImpl<false>(raw_value, column_ids_iter, column_ids_iter_end, block, block_column_pos, column_infos, pk_handle_id, ignore_pk_if_absent, force_decode);
    }
    default:
        return appendRowV1ToBlock(raw_value, column_ids_iter, column_ids_iter_end, block, block_column_pos, column_infos, pk_handle_id, ignore_pk_if_absent, force_decode);
    }
}

inline bool addDefaultValueToColumnIfPossible(const ColumnInfo & column_info, Block & block, size_t block_column_pos, bool ignore_pk_if_absent, bool force_decode)
{
    // We consider a missing column could be safely filled with NULL, unless it has not default value and is NOT NULL.
    // This could saves lots of unnecessary schema syncs for old data with a newer schema that has newly added columns.

    if (column_info.hasPriKeyFlag())
    {
        // For clustered index or pk_is_handle, if the pk column does not exists, it can still be decoded from the key
        if (ignore_pk_if_absent)
            return true;

        assert(!ignore_pk_if_absent);
        if (!force_decode)
            return false;
        // Else non-clustered index, and not pk_is_handle, it could be a row encoded by older schema,
        // we need to fill the column wich has primary key flag with default value.
        // fallthrough to fill default value when force_decode
    }

    if (column_info.hasNoDefaultValueFlag() && column_info.hasNotNullFlag())
    {
        if (!force_decode)
            return false;
        // Else the row does not contain this "not null" / "no default value" column,
        // it could be a row encoded by older schema.
        // fallthrough to fill default value when force_decode
    }
    // not null or has no default value, tidb will fill with specific value.
    auto * raw_column = const_cast<IColumn *>((block.getByPosition(block_column_pos)).column.get());
    raw_column->insert(column_info.defaultValueToField());
    return true;
}

template <bool is_big>
bool appendRowV2ToBlockImpl(
    const TiKVValue::Base & raw_value,
    SortedColumnIDWithPosConstIter column_ids_iter,
    SortedColumnIDWithPosConstIter column_ids_iter_end,
    Block & block,
    size_t block_column_pos,
    const ColumnInfos & column_infos,
    ColumnID pk_handle_id,
    bool ignore_pk_if_absent,
    bool force_decode)
{
    size_t cursor = 2; // Skip the initial codec ver and row flag.
    size_t num_not_null_columns = decodeUInt<UInt16>(cursor, raw_value);
    size_t num_null_columns = decodeUInt<UInt16>(cursor, raw_value);
    std::vector<ColumnID> not_null_column_ids;
    std::vector<ColumnID> null_column_ids;
    std::vector<size_t> value_offsets;
    decodeUInts<ColumnID, typename RowV2::Types<is_big>::ColumnIDType>(cursor, raw_value, num_not_null_columns, not_null_column_ids);
    decodeUInts<ColumnID, typename RowV2::Types<is_big>::ColumnIDType>(cursor, raw_value, num_null_columns, null_column_ids);
    decodeUInts<size_t, typename RowV2::Types<is_big>::ValueOffsetType>(cursor, raw_value, num_not_null_columns, value_offsets);
    size_t values_start_pos = cursor;
    size_t idx_not_null = 0;
    size_t idx_null = 0;
    // Merge ordered not null/null columns to keep order.
    while (idx_not_null < not_null_column_ids.size() || idx_null < null_column_ids.size())
    {
        if (column_ids_iter == column_ids_iter_end)
        {
            // extra column
            return force_decode;
        }

        bool is_null;
        if (idx_not_null < not_null_column_ids.size() && idx_null < null_column_ids.size())
            is_null = not_null_column_ids[idx_not_null] > null_column_ids[idx_null];
        else
            is_null = idx_null < null_column_ids.size();

        auto next_datum_column_id = is_null ? null_column_ids[idx_null] : not_null_column_ids[idx_not_null];
        const auto next_column_id = column_ids_iter->first;
        if (next_column_id > next_datum_column_id)
        {
            // The next column id to read is bigger than the column id of next datum in encoded row.
            // It means this is the datum of extra column. May happen when reading after dropping
            // a column.
            if (!force_decode)
                return false;
            // Ignore the extra column and continue to parse other datum
            if (is_null)
                idx_null++;
            else
                idx_not_null++;
        }
        else if (next_column_id < next_datum_column_id)
        {
            // The next column id to read is less than the column id of next datum in encoded row.
            // It means this is the datum of missing column. May happen when reading after adding
            // a column.
            // Fill with default value and continue to read data for next column id.
            const auto & column_info = column_infos[column_ids_iter->second];
            if (!addDefaultValueToColumnIfPossible(column_info, block, block_column_pos, ignore_pk_if_absent, force_decode))
                return false;
            column_ids_iter++;
            block_column_pos++;
        }
        else
        {
            // If pk_handle_id is a valid column id, then it means the table's pk_is_handle is true
            // we can just ignore the pk value encoded in value part
            if (unlikely(next_column_id == pk_handle_id))
            {
                column_ids_iter++;
                block_column_pos++;
                if (is_null)
                {
                    idx_null++;
                }
                else
                {
                    idx_not_null++;
                }
                continue;
            }

            // Parse the datum.
            auto * raw_column = const_cast<IColumn *>((block.getByPosition(block_column_pos)).column.get());
            const auto & column_info = column_infos[column_ids_iter->second];
            if (is_null)
            {
                if (!raw_column->isColumnNullable())
                {
                    if (!force_decode)
                    {
                        return false;
                    }
                    else
                    {
                        throw Exception("Detected invalid null when decoding data of column " + column_info.name + " with column type " + raw_column->getName(),
                                        ErrorCodes::LOGICAL_ERROR);
                    }
                }
                // ColumnNullable::insertDefault just insert a null value
                raw_column->insertDefault();
                idx_null++;
            }
            else
            {
                size_t start = idx_not_null ? value_offsets[idx_not_null - 1] : 0;
                size_t length = value_offsets[idx_not_null] - start;
                if (!raw_column->decodeTiDBRowV2Datum(values_start_pos + start, raw_value, length, force_decode))
                    return false;
                idx_not_null++;
            }
            column_ids_iter++;
            block_column_pos++;
        }
    }
    while (column_ids_iter != column_ids_iter_end)
    {
        if (column_ids_iter->first != pk_handle_id)
        {
            const auto & column_info = column_infos[column_ids_iter->second];
            if (!addDefaultValueToColumnIfPossible(column_info, block, block_column_pos, ignore_pk_if_absent, force_decode))
                return false;
        }
        column_ids_iter++;
        block_column_pos++;
    }
    return true;
}

using TiDB::DatumFlat;
bool appendRowV1ToBlock(
    const TiKVValue::Base & raw_value,
    SortedColumnIDWithPosConstIter column_ids_iter,
    SortedColumnIDWithPosConstIter column_ids_iter_end,
    Block & block,
    size_t block_column_pos,
    const ColumnInfos & column_infos,
    ColumnID pk_handle_id,
    bool ignore_pk_if_absent,
    bool force_decode)
{
    size_t cursor = 0;
    std::map<ColumnID, Field> decoded_fields;
    while (cursor < raw_value.size())
    {
        Field f = DecodeDatum(cursor, raw_value);
        if (f.isNull())
            break;
        ColumnID col_id = f.get<ColumnID>();
        decoded_fields.emplace(col_id, DecodeDatum(cursor, raw_value));
    }
    if (cursor != raw_value.size())
        throw Exception(std::string(__PRETTY_FUNCTION__) + ": cursor is not end, remaining: " + raw_value.substr(cursor),
                        ErrorCodes::LOGICAL_ERROR);

    auto decoded_field_iter = decoded_fields.begin();
    while (decoded_field_iter != decoded_fields.end())
    {
        if (column_ids_iter == column_ids_iter_end)
        {
            // extra column
            return force_decode;
        }

        auto next_field_column_id = decoded_field_iter->first;
        if (column_ids_iter->first > next_field_column_id)
        {
            // extra column
            if (!force_decode)
                return false;
            decoded_field_iter++;
        }
        else if (column_ids_iter->first < next_field_column_id)
        {
            const auto & column_info = column_infos[column_ids_iter->second];
            if (!addDefaultValueToColumnIfPossible(column_info, block, block_column_pos, ignore_pk_if_absent, force_decode))
                return false;
            column_ids_iter++;
            block_column_pos++;
        }
        else
        {
            // if pk_handle_id is a valid column id, then it means the table's pk_is_handle is true
            // we can just ignore the pk value encoded in value part
            if (unlikely(column_ids_iter->first == pk_handle_id))
            {
                decoded_field_iter++;
                column_ids_iter++;
                block_column_pos++;
                continue;
            }

            auto * raw_column = const_cast<IColumn *>((block.getByPosition(block_column_pos)).column.get());
            const auto & column_info = column_infos[column_ids_iter->second];
            DatumFlat datum(decoded_field_iter->second, column_info.tp);
            const Field & unflattened = datum.field();
            if (datum.overflow(column_info))
            {
                // Overflow detected, fatal if force_decode is true,
                // as schema being newer and narrow shouldn't happen.
                // Otherwise return false to outer, outer should sync schema and try again.
                if (force_decode)
                {
                    throw Exception("Detected overflow when decoding data " + std::to_string(unflattened.get<UInt64>()) + " of column "
                                        + column_info.name + " with column " + raw_column->getName(),
                                    ErrorCodes::LOGICAL_ERROR);
                }

                return false;
            }
            if (datum.invalidNull(column_info))
            {
                // Null value with non-null type detected, fatal if force_decode is true,
                // as schema being newer and with invalid null shouldn't happen.
                // Otherwise return false to outer, outer should sync schema and try again.
                if (force_decode)
                {
                    throw Exception("Detected invalid null when decoding data " + std::to_string(unflattened.get<UInt64>())
                                        + " of column " + column_info.name + " with type " + raw_column->getName(),
                                    ErrorCodes::LOGICAL_ERROR);
                }

                return false;
            }
            raw_column->insert(unflattened);
            decoded_field_iter++;
            column_ids_iter++;
            block_column_pos++;
        }
    }
    while (column_ids_iter != column_ids_iter_end)
    {
        if (column_ids_iter->first != pk_handle_id)
        {
            const auto & column_info = column_infos[column_ids_iter->second];
            if (!addDefaultValueToColumnIfPossible(column_info, block, block_column_pos, ignore_pk_if_absent, force_decode))
                return false;
        }
        column_ids_iter++;
        block_column_pos++;
    }
    return true;
}

>>>>>>> 8e411ae86b (Fix decode error when "NULL" value in the column with "primary key" flag (#5879))
} // namespace DB
