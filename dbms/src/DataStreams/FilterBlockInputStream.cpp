#include <Columns/ColumnConst.h>
#include <Columns/ColumnsCommon.h>
#include <Columns/ColumnsNumber.h>
#include <Columns/FilterDescription.h>
#include <Common/typeid_cast.h>
#include <Interpreters/ExpressionActions.h>

#include <DataStreams/FilterBlockInputStream.h>

namespace DB
{

namespace ErrorCodes
{
extern const int ILLEGAL_TYPE_OF_COLUMN_FOR_FILTER;
extern const int LOGICAL_ERROR;
} // namespace ErrorCodes


FilterBlockInputStream::FilterBlockInputStream(
    const BlockInputStreamPtr & input, const ExpressionActionsPtr & expression_, const String & filter_column_name)
    : expression(expression_)
{
    children.push_back(input);

    /// Determine position of filter column.
    header = input->getHeader();
    expression->execute(header);

    filter_column = header.getPositionByName(filter_column_name);
    auto & column_elem = header.safeGetByPosition(filter_column);

    /// Isn't the filter already constant?
    if (column_elem.column)
        constant_filter_description = ConstantFilterDescription(*column_elem.column);

    if (!constant_filter_description.always_false && !constant_filter_description.always_true)
    {
        /// Replace the filter column to a constant with value 1.
        FilterDescription filter_description_check(*column_elem.column);
        column_elem.column = column_elem.type->createColumnConst(header.rows(), UInt64(1));
    }
}


String FilterBlockInputStream::getName() const { return "Filter"; }


Block FilterBlockInputStream::getTotals()
{
    if (IProfilingBlockInputStream * child = dynamic_cast<IProfilingBlockInputStream *>(&*children.back()))
    {
        totals = child->getTotals();
        expression->executeOnTotals(totals);
    }

    return totals;
}


Block FilterBlockInputStream::getHeader() const { return header; }

bool IsUInt(String type) {
    if (type == "Nullable(UInt32)") return true;
    if (type == "UInt32") return true;
    if (type == "Nullable(UInt64)") return true;
    if (type == "UInt64") return true;
    return false;
}

bool IsInt(String type) {
    if (type == "Nullable(Int32)") return true;
    if (type == "Int32") return true;
    if (type == "Nullable(Int64)") return true;
    if (type == "Int64") return true;
    return false;
}

bool IsString(String type) {
    if (type == "Nullable(String)") return true;
    if (type == "String") return true;
    if (type == "Nullable(FixedString)") return true;
    if (type == "FixedString") return true;
    return false;
}

Block FilterBlockInputStream::readImpl()
{
    Block res;

    if (constant_filter_description.always_false)
        return res;

    /// Until non-empty block after filtering or end of stream.
    while (1)
    {
        IColumn::Filter * child_filter = nullptr;

        res = children.back()->read(child_filter, true);

        if (!res)
            return res;

        expression->execute(res);

        if (constant_filter_description.always_true && !child_filter && bfs[0] == nullptr)
            return res;

        size_t columns = res.columns();
        size_t rows = res.rows();
        ColumnPtr column_of_filter = res.safeGetByPosition(filter_column).column;

        IColumn::Filter  column_of_filter_for_bloom;
        if (bfs[0] != nullptr) {
            for (unsigned i = 0;i < rows;i++) {
                column_of_filter_for_bloom.push_back(1);
            }
            auto fnvHash = std::make_shared<FNVhash>();
            for (unsigned bfnum = 0; !join_keys[bfnum].empty() ;bfnum++) {
                auto join_key = join_keys[bfnum];
                auto bf = bfs[bfnum];

                std::vector<ColumnWithTypeAndName > cols;
                bool canUseBloom = true;
                for (unsigned i = 0;i < join_key.size();i++) {
                    ColumnWithTypeAndName col = res.getByName(join_key[i]);
                    String type = col.type->getName();
                    if (!IsInt(type) && !IsUInt(type) && !IsString(type)) {
                        canUseBloom = false;
                        break;
                    }
                    cols.push_back(col);
                }
                if (canUseBloom) {
                    fnvHash ->resetHash(rows);

                    UInt8 flag[1];
                    UInt8 tmp[8];
                    for (unsigned j = 0;j < join_key.size();j++) {
                        if (IsInt(cols[j].type->getName())) {
                            for (unsigned i = 0;i < rows;i++) {
                                flag[0] = 8;
                                *(UInt64 *)(tmp) = cols[j].column->get64(i);
                                fnvHash->myhash(i,flag,1);
                                fnvHash->myhash(i,tmp,8);
                            }
                        }
                        if (IsUInt(cols[j].type->getName())) {
                            for (unsigned i = 0;i < rows;i++) {
                                flag[0] = 9;
                                *(UInt64 *)(tmp) = cols[j].column->get64(i);
                                fnvHash->myhash(i,flag,1);
                                fnvHash->myhash(i,tmp,8);
                            }
                        }
                        if (IsString(cols[j].type->getName())) {
                            for (unsigned i = 0;i < rows;i++) {
                                flag[0] = 2;
                                auto t = (*cols[j].column)[i].get<String>();
                                fnvHash->myhash(i,flag, 1);
                                fnvHash->myhash(i,(UInt8 *) (t.c_str()), t.length());
                            }
                        }
                    }

                    for (unsigned i = 0;i < rows;i++) {
                        if (column_of_filter->get64(i) == 0) {
                            column_of_filter_for_bloom[i] = 0;
                            continue;
                        }
                        if (bf->ProbeU64(fnvHash->sum64(i))) {
                        } else {
                            column_of_filter_for_bloom[i] = 0;
                        }
                    }
                }
            }
        }

        if (unlikely(child_filter && child_filter->size() != rows))
            throw Exception("Unexpected child filter size", ErrorCodes::LOGICAL_ERROR);

        /** It happens that at the stage of analysis of expressions (in sample_block) the columns-constants have not been calculated yet,
            *  and now - are calculated. That is, not all cases are covered by the code above.
            * This happens if the function returns a constant for a non-constant argument.
            * For example, `ignore` function.
            */
        constant_filter_description = ConstantFilterDescription(*column_of_filter);

        if (constant_filter_description.always_false)
        {
            res.clear();
            return res;
        }

        IColumn::Filter * filter = nullptr ;
        ColumnPtr filter_holder;

        if (constant_filter_description.always_true)
        {
            if (child_filter)
                filter = child_filter;
            else
            {
                if (bfs[0] == nullptr)return res;
            }
        }
        else
        {
            FilterDescription filter_and_holder(*column_of_filter);
            filter = const_cast<IColumn::Filter *>(filter_and_holder.data);
            filter_holder = filter_and_holder.data_holder;

            if (child_filter)
            {
                /// Merge child_filter
                UInt8 * a = filter->data();
                UInt8 * b = child_filter->data();
                for (size_t i = 0; i < rows; ++i)
                {
                    *a = *a > 0 && *b != 0;
                    ++a;
                    ++b;
                }
            }
        }

        if (filter == nullptr) {
            filter = &column_of_filter_for_bloom;
        } else
        if (!column_of_filter_for_bloom.empty()) {
            UInt8 *a = filter->data();
            for (size_t i = 0;i < rows;++i) {
                *a = *a > 0 && column_of_filter_for_bloom[i] != 0;
                ++a;
            }
        }

        /** Let's find out how many rows will be in result.
          * To do this, we filter out the first non-constant column
          *  or calculate number of set bytes in the filter.
          */
        size_t first_non_constant_column = 0;
        for (size_t i = 0; i < columns; ++i)
        {
            if (!res.safeGetByPosition(i).column->isColumnConst())
            {
                first_non_constant_column = i;

                if (first_non_constant_column != static_cast<size_t>(filter_column))
                    break;
            }
        }

        size_t filtered_rows = 0;
        if (first_non_constant_column != static_cast<size_t>(filter_column))
        {
            ColumnWithTypeAndName & current_column = res.safeGetByPosition(first_non_constant_column);
            current_column.column = current_column.column->filter(*filter, -1);
            filtered_rows = current_column.column->size();
        }
        else
        {
            filtered_rows = countBytesInFilter(*filter);
        }

        /// If the current block is completely filtered out, let's move on to the next one.
        if (filtered_rows == 0)
            continue;

        /// If all the rows pass through the filter.
        if (filtered_rows == rows)
        {
            /// Replace the column with the filter by a constant.
            res.safeGetByPosition(filter_column).column
                = res.safeGetByPosition(filter_column).type->createColumnConst(filtered_rows, UInt64(1));
            /// No need to touch the rest of the columns.
            return res;
        }

        /// Filter the rest of the columns.
        for (size_t i = 0; i < columns; ++i)
        {
            ColumnWithTypeAndName & current_column = res.safeGetByPosition(i);

            if (i == static_cast<size_t>(filter_column))
            {
                /// The column with filter itself is replaced with a column with a constant `1`, since after filtering, nothing else will remain.
                /// NOTE User could pass column with something different than 0 and 1 for filter.
                /// Example:
                ///  SELECT materialize(100) AS x WHERE x
                /// will work incorrectly.
                current_column.column = current_column.type->createColumnConst(filtered_rows, UInt64(1));
                continue;
            }

            if (i == first_non_constant_column)
                continue;

            if (current_column.column->isColumnConst())
                current_column.column = current_column.column->cut(0, filtered_rows);
            else
                current_column.column = current_column.column->filter(*filter, -1);
        }

        return res;
    }
}


} // namespace DB
