<<<<<<< HEAD
// Copyright 2022 PingCAP, Ltd.
=======
// Copyright 2023 PingCAP, Inc.
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <Interpreters/NullableUtils.h>


namespace DB
{
void extractNestedColumnsAndNullMap(
    ColumnRawPtrs & key_columns,
    ColumnPtr & null_map_holder,
    ConstNullMapPtr & null_map)
{
    if (key_columns.size() == 1)
    {
        auto & column = key_columns[0];
        if (!column->isColumnNullable())
            return;

        const ColumnNullable & column_nullable = static_cast<const ColumnNullable &>(*column);
        null_map = &column_nullable.getNullMapData();
        null_map_holder = column_nullable.getNullMapColumnPtr();
        column = &column_nullable.getNestedColumn();
    }
    else
    {
        for (auto & column : key_columns)
        {
            if (column->isColumnNullable())
            {
                const ColumnNullable & column_nullable = static_cast<const ColumnNullable &>(*column);
                column = &column_nullable.getNestedColumn();

                if (!null_map_holder)
                {
                    null_map_holder = column_nullable.getNullMapColumnPtr();
                }
                else
                {
                    MutableColumnPtr mutable_null_map_holder = (*std::move(null_map_holder)).mutate();

<<<<<<< HEAD
                    PaddedPODArray<UInt8> & mutable_null_map = static_cast<ColumnUInt8 &>(*mutable_null_map_holder).getData();
=======
                    PaddedPODArray<UInt8> & mutable_null_map
                        = typeid_cast<ColumnUInt8 &>(*mutable_null_map_holder).getData();
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))
                    const PaddedPODArray<UInt8> & other_null_map = column_nullable.getNullMapData();
                    for (size_t i = 0, size = mutable_null_map.size(); i < size; ++i)
                        mutable_null_map[i] |= other_null_map[i];

                    null_map_holder = std::move(mutable_null_map_holder);
                }
            }
        }

        null_map = null_map_holder ? &static_cast<const ColumnUInt8 &>(*null_map_holder).getData() : nullptr;
    }
}

<<<<<<< HEAD
=======
void extractAllKeyNullMap(
    ColumnRawPtrs & key_columns,
    ColumnPtr & all_key_null_map_holder,
    ConstNullMapPtr & all_key_null_map)
{
    if (key_columns.empty())
        return;

    for (auto & column : key_columns)
    {
        /// If one column is not nullable, just return.
        if (!column->isColumnNullable())
            return;
    }

    if (key_columns.size() == 1)
    {
        auto & column = key_columns[0];

        const auto & column_nullable = typeid_cast<const ColumnNullable &>(*column);
        all_key_null_map_holder = column_nullable.getNullMapColumnPtr();
    }
    else
    {
        for (auto & column : key_columns)
        {
            const auto & column_nullable = typeid_cast<const ColumnNullable &>(*column);

            if (!all_key_null_map_holder)
            {
                all_key_null_map_holder = column_nullable.getNullMapColumnPtr();
            }
            else
            {
                MutableColumnPtr mutable_null_map_holder = (*std::move(all_key_null_map_holder)).mutate();

                PaddedPODArray<UInt8> & mutable_null_map
                    = typeid_cast<ColumnUInt8 &>(*mutable_null_map_holder).getData();
                const PaddedPODArray<UInt8> & other_null_map = column_nullable.getNullMapData();
                for (size_t i = 0, size = mutable_null_map.size(); i < size; ++i)
                    mutable_null_map[i] &= other_null_map[i];

                all_key_null_map_holder = std::move(mutable_null_map_holder);
            }
        }
    }

    all_key_null_map = &typeid_cast<const ColumnUInt8 &>(*all_key_null_map_holder).getData();
}

>>>>>>> 6638f2067b (Fix license and format coding style (#7962))
} // namespace DB
