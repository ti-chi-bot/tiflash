// Copyright 2023 PingCAP, Inc.
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

#pragma once

#include <Columns/IColumn.h>
#include <Common/Arena.h>
#include <Common/PODArray.h>
#include <Common/SipHash.h>
#include <Common/memcpySmall.h>
#include <common/memcpy.h>


class ICollator;


namespace DB
{
/** Column for String values.
  */
class ColumnString final : public COWPtrHelper<IColumn, ColumnString>
{
public:
    using Chars_t = PaddedPODArray<UInt8>;

private:
    friend class COWPtrHelper<IColumn, ColumnString>;

    /// Maps i'th position to offset to i+1'th element. Last offset maps to the end of all chars (is the size of all chars).
    Offsets offsets;

    /// Bytes of strings, placed contiguously.
    /// For convenience, every string ends with terminating zero byte. Note that strings could contain zero bytes in the middle.
    Chars_t chars;

    size_t ALWAYS_INLINE offsetAt(size_t i) const { return i == 0 ? 0 : offsets[i - 1]; }

    /// Size of i-th element, including terminating zero.
    size_t ALWAYS_INLINE sizeAt(size_t i) const { return i == 0 ? offsets[0] : (offsets[i] - offsets[i - 1]); }

    template <bool positive>
    struct less;

    template <bool positive, typename Derived>
    struct LessWithCollation;

    ColumnString() = default;

    ColumnString(const ColumnString & src)
        : COWPtrHelper<IColumn, ColumnString>(src)
        , offsets(src.offsets.begin(), src.offsets.end())
        , chars(src.chars.begin(), src.chars.end()){};

public:
    const char * getFamilyName() const override { return "String"; }

    size_t size() const override { return offsets.size(); }

    size_t byteSize() const override { return chars.size() + offsets.size() * sizeof(offsets[0]); }

    size_t byteSize(size_t offset, size_t limit) const override
    {
        if (limit == 0)
            return 0;
        auto char_size = offsets[offset + limit - 1] - (offset == 0 ? 0 : offsets[offset - 1]);
        return char_size + limit * sizeof(offsets[0]);
    }

    size_t allocatedBytes() const override { return chars.allocated_bytes() + offsets.allocated_bytes(); }

    MutableColumnPtr cloneResized(size_t to_size) const override;

    Field operator[](size_t n) const override { return Field(&chars[offsetAt(n)], sizeAt(n) - 1); }

    void get(size_t n, Field & res) const override { res.assignString(&chars[offsetAt(n)], sizeAt(n) - 1); }

    StringRef getDataAt(size_t n) const override { return StringRef(&chars[offsetAt(n)], sizeAt(n) - 1); }

    StringRef getDataAtWithTerminatingZero(size_t n) const override
    {
        return StringRef(&chars[offsetAt(n)], sizeAt(n));
    }

/// Suppress gcc 7.3.1 warning: '*((void*)&<anonymous> +8)' may be used uninitialized in this function
#if !__clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

    void insert(const Field & x) override
    {
        const auto & s = DB::get<const String &>(x);
        insertData(s.data(), s.size());
    }

#if !__clang__
#pragma GCC diagnostic pop
#endif

    void insertFrom(const IColumn & src_, size_t n) override
    {
        const auto & src = static_cast<const ColumnString &>(src_);

        if (n != 0)
        {
            const size_t size_to_append = src.offsets[n] - src.offsets[n - 1];

            if (size_to_append == 1)
            {
                /// shortcut for empty string
                chars.push_back(0);
                offsets.push_back(chars.size());
            }
            else
            {
                const size_t old_size = chars.size();
                const size_t offset = src.offsets[n - 1];
                const size_t new_size = old_size + size_to_append;

                chars.resize(new_size);
                memcpySmallAllowReadWriteOverflow15(&chars[old_size], &src.chars[offset], size_to_append);
                offsets.push_back(new_size);
            }
        }
        else
        {
            const size_t old_size = chars.size();
            const size_t size_to_append = src.offsets[0];
            const size_t new_size = old_size + size_to_append;

            chars.resize(new_size);
            memcpySmallAllowReadWriteOverflow15(&chars[old_size], &src.chars[0], size_to_append);
            offsets.push_back(new_size);
        }
    }

    template <bool add_terminating_zero>
    ALWAYS_INLINE inline void insertDataImpl(const char * pos, size_t length)
    {
        const size_t old_size = chars.size();
        const size_t new_size = old_size + length + (add_terminating_zero ? 1 : 0);

        chars.resize(new_size);
        inline_memcpy(&chars[old_size], pos, length);

        if constexpr (add_terminating_zero)
            chars[old_size + length] = 0;
        offsets.push_back(new_size);
    }

    void insertData(const char * pos, size_t length) override { return insertDataImpl<true>(pos, length); }

    bool decodeTiDBRowV2Datum(size_t cursor, const String & raw_value, size_t length, bool /* force_decode */) override
    {
        insertData(raw_value.c_str() + cursor, length);
        return true;
    }

    void insertDataWithTerminatingZero(const char * pos, size_t length) override
    {
        return insertDataImpl<false>(pos, length);
    }

    void popBack(size_t n) override
    {
        size_t nested_n = offsets.back() - offsetAt(offsets.size() - n);
        chars.resize(chars.size() - nested_n);
        offsets.resize_assume_reserved(offsets.size() - n);
    }

    StringRef serializeValueIntoArena(
        size_t n,
        Arena & arena,
        char const *& begin,
        const TiDB::TiDBCollatorPtr & collator,
        String & sort_key_container) const override
    {
        size_t string_size = sizeAt(n);
        size_t offset = offsetAt(n);
        const void * src = &chars[offset];

        StringRef res;

        if (likely(collator != nullptr))
        {
            // Skip last zero byte.
            auto sort_key
                = collator->sortKeyFastPath(reinterpret_cast<const char *>(src), string_size - 1, sort_key_container);
            string_size = sort_key.size;
            src = sort_key.data;
        }
        res.size = sizeof(string_size) + string_size;
        char * pos = arena.allocContinue(res.size, begin);
        std::memcpy(pos, &string_size, sizeof(string_size));
        inline_memcpy(pos + sizeof(string_size), src, string_size);
        res.data = pos;
        return res;
    }

    inline const char * deserializeAndInsertFromArena(const char * pos, const TiDB::TiDBCollatorPtr & collator) override
    {
        const size_t string_size = *reinterpret_cast<const size_t *>(pos);
        pos += sizeof(string_size);

        if (likely(collator))
        {
            // https://github.com/pingcap/tiflash/pull/6135
            // - Generate empty string column
            // - Make size of `offsets` as previous way for func `ColumnString::size()`
            offsets.push_back(0);
            return pos + string_size;
        }
        else
        {
            insertDataWithTerminatingZero(pos, string_size);
            return pos + string_size;
        }
    }

    void updateHashWithValue(
        size_t n,
        SipHash & hash,
        const TiDB::TiDBCollatorPtr & collator,
        String & sort_key_container) const override
    {
        size_t string_size = sizeAt(n);
        size_t offset = offsetAt(n);
        if (likely(collator != nullptr))
        {
            // Skip last zero byte.
            auto sort_key = collator->sortKeyFastPath(
                reinterpret_cast<const char *>(&chars[offset]),
                string_size - 1,
                sort_key_container);
            string_size = sort_key.size;
            hash.update(reinterpret_cast<const char *>(&string_size), sizeof(string_size));
            hash.update(sort_key.data, sort_key.size);
        }
        else
        {
            hash.update(reinterpret_cast<const char *>(&string_size), sizeof(string_size));
            hash.update(reinterpret_cast<const char *>(&chars[offset]), string_size);
        }
    }

    void updateHashWithValues(
        IColumn::HashValues & hash_values,
        const TiDB::TiDBCollatorPtr & collator,
        String & sort_key_container) const override;

    void updateWeakHash32(WeakHash32 & hash, const TiDB::TiDBCollatorPtr &, String &) const override;

    void insertRangeFrom(const IColumn & src, size_t start, size_t length) override;

    ColumnPtr filter(const Filter & filt, ssize_t result_size_hint) const override;

    ColumnPtr permute(const Permutation & perm, size_t limit) const override;

    void insertDefault() override
    {
        chars.push_back(0);
        offsets.push_back(offsets.empty() ? 1 : (offsets.back() + 1));
    }

    int compareAt(size_t n, size_t m, const IColumn & rhs_, int /*nan_direction_hint*/) const override
    {
        const auto & rhs = static_cast<const ColumnString &>(rhs_);
        return getDataAtWithTerminatingZero(n).compare(rhs.getDataAtWithTerminatingZero(m));
    }

    int compareAt(size_t n, size_t m, const IColumn & rhs_, int, const ICollator & collator) const override
    {
        return compareAtWithCollationImpl(n, m, rhs_, collator);
    }
    /// Variant of compareAt for string comparison with respect of collation.
    int compareAtWithCollationImpl(size_t n, size_t m, const IColumn & rhs_, const ICollator & collator) const;

    void getPermutation(bool reverse, size_t limit, int nan_direction_hint, Permutation & res) const override;

    void getPermutation(const ICollator & collator, bool reverse, size_t limit, int, Permutation & res) const override
    {
        getPermutationWithCollationImpl(collator, reverse, limit, res);
    }

    /// Sorting with respect of collation.
    void getPermutationWithCollationImpl(const ICollator & collator, bool reverse, size_t limit, Permutation & res)
        const;

<<<<<<< HEAD
    ColumnPtr replicate(const Offsets & replicate_offsets) const override;
=======
    ColumnPtr replicateRange(size_t start_row, size_t end_row, const IColumn::Offsets & replicate_offsets)
        const override;
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))

    MutableColumns scatter(ColumnIndex num_columns, const Selector & selector) const override
    {
        return scatterImpl<ColumnString>(num_columns, selector);
    }

    void scatterTo(ScatterColumns & columns, const Selector & selector) const override
    {
        scatterToImpl<ColumnString>(columns, selector);
    }

    void gather(ColumnGathererStream & gatherer_stream) override;

    void reserve(size_t n) override;

    void getExtremes(Field & min, Field & max) const override;


    bool canBeInsideNullable() const override { return true; }


    Chars_t & getChars() { return chars; }
    const Chars_t & getChars() const { return chars; }

    Offsets & getOffsets() { return offsets; }
    const Offsets & getOffsets() const { return offsets; }
};


} // namespace DB
