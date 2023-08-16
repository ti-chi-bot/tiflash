// Copyright 2023 PingCAP, Ltd.
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

#include <Common/FmtUtils.h>
#include <Core/ColumnNumbers.h>
#include <DataStreams/IProfilingBlockInputStream.h>
#include <Interpreters/WindowDescription.h>
#include <WindowFunctions/WindowUtils.h>

#include <deque>
#include <memory>
#include <tuple>

namespace DB
{
/* Implementation details.*/
struct WindowTransformAction
{
<<<<<<< HEAD
    WindowTransformAction(const Block & input_header, const WindowDescription & window_description_, const String & req_id);
=======
private:
    // Used for calculating the frame start
    std::tuple<RowNumber, bool> stepToFrameStart(const RowNumber & current_row, const WindowFrame & frame);
    // Used for calculating the frame end
    std::tuple<RowNumber, bool> stepToFrameEnd(const RowNumber & current_row, const WindowFrame & frame);

    RowNumber stepInPreceding(const RowNumber & moved_row, size_t step_num);
    std::tuple<RowNumber, bool> stepInFollowing(const RowNumber & moved_row, size_t step_num);

    // distance is left - right.
    UInt64 distance(RowNumber left, RowNumber right);

public:
    WindowTransformAction(
        const Block & input_header,
        const WindowDescription & window_description_,
        const String & req_id);
>>>>>>> 9b0011ef67 (Make window function return null when frame is valid (#7939))

    void cleanUp();

    void advancePartitionEnd();
    bool isDifferentFromPrevPartition(UInt64 current_partition_row);

    bool arePeers(const RowNumber & x, const RowNumber & y) const;

    void advanceFrameStart();
    void advanceFrameEndCurrentRow();
    void advanceFrameEnd();

    void writeOutCurrentRow();

    Block tryGetOutputBlock();
    void releaseAlreadyOutputWindowBlock();

    void initialWorkspaces();
    void initialPartitionAndOrderColumnIndices();

    Columns & inputAt(const RowNumber & x)
    {
        assert(x.block >= first_block_number);
        assert(x.block - first_block_number < window_blocks.size());
        return window_blocks[x.block - first_block_number].input_columns;
    }

    const Columns & inputAt(const RowNumber & x) const { return const_cast<WindowTransformAction *>(this)->inputAt(x); }

    auto & blockAt(const UInt64 block_number)
    {
        assert(block_number >= first_block_number);
        assert(block_number - first_block_number < window_blocks.size());
        return window_blocks[block_number - first_block_number];
    }

    const auto & blockAt(const UInt64 block_number) const
    {
        return const_cast<WindowTransformAction *>(this)->blockAt(block_number);
    }

    auto & blockAt(const RowNumber & x) { return blockAt(x.block); }

    const auto & blockAt(const RowNumber & x) const { return const_cast<WindowTransformAction *>(this)->blockAt(x); }

    size_t blockRowsNumber(const RowNumber & x) const { return blockAt(x).rows; }

    MutableColumns & outputAt(const RowNumber & x)
    {
        assert(x.block >= first_block_number);
        assert(x.block - first_block_number < window_blocks.size());
        return window_blocks[x.block - first_block_number].output_columns;
    }

    void advanceRowNumber(RowNumber & x) const;

    bool lead(RowNumber & x, size_t offset) const;

    bool lag(RowNumber & x, size_t offset) const;

    RowNumber blocksEnd() const { return RowNumber{first_block_number + window_blocks.size(), 0}; }

    void appendBlock(Block & current_block);

    void tryCalculate();

    bool onlyHaveRowNumber();

    bool onlyHaveRowNumberAndRank();

    Int64 getPartitionEndRow(size_t block_rows);

    void appendInfo(FmtBuffer & buffer) const;

    LoggerPtr log;

    bool input_is_finished = false;

    Block output_header;

    WindowDescription window_description;

    // Indices of the PARTITION BY columns in block.
    std::vector<size_t> partition_column_indices;
    // Indices of the ORDER BY columns in block;
    std::vector<size_t> order_column_indices;

    // Per-window-function scratch spaces.
    std::vector<WindowFunctionWorkspace> workspaces;

    // A sliding window of blocks we currently need. We add the input blocks as
    // they arrive, and discard the blocks we don't need anymore. The blocks
    // have an always-incrementing index. The index of the first block is in
    // `first_block_number`.
    std::deque<WindowBlock> window_blocks;
    UInt64 first_block_number = 0;
    // The next block we are going to pass to the consumer.
    UInt64 next_output_block_number = 0;
    // The first row for which we still haven't calculated the window functions.
    // Used to determine which resulting blocks we can pass to the consumer.
    RowNumber first_not_ready_row;

    // Boundaries of the current partition.
    // partition_start doesn't point to a valid block, because we want to drop
    // the blocks early to save memory. We still have to track it so that we can
    // cut off a PRECEDING frame at the partition start.
    // The `partition_end` is past-the-end, as usual. When
    // partition_ended = false, it still haven't ended, and partition_end is the
    // next row to check.
    RowNumber partition_start;
    RowNumber partition_end;
    bool partition_ended = false;

    // The row for which we are now computing the window functions.
    RowNumber current_row;
    // The start of current peer group, needed for CURRENT ROW frame start.
    // For ROWS frame, always equal to the current row, and for RANGE and GROUP
    // frames may be earlier.
    RowNumber peer_group_last;

    // Row and group numbers in partition for calculating rank() and friends.
    UInt64 current_row_number = 1;
    UInt64 peer_group_start_row_number = 1;
    UInt64 peer_group_number = 1;

    // The frame is [frame_start, frame_end) if frame_ended && frame_started,
    // and unknown otherwise. Note that when we move to the next row, both the
    // frame_start and the frame_end may jump forward by an unknown amount of
    // blocks, e.g. if we use a RANGE frame. This means that sometimes we don't
    // know neither frame_end nor frame_start.
    // We update the states of the window functions after we find the final frame
    // boundaries.
    // After we have found the final boundaries of the frame, we can immediately
    // output the result for the current row, w/o waiting for more data.
    RowNumber frame_start;
    RowNumber frame_end;
    bool frame_ended = false;
    bool frame_started = false;

    // The previous frame boundaries that correspond to the current state of the
    // aggregate function. We use them to determine how to update the aggregation
    // state after we find the new frame.
    RowNumber prev_frame_start;

    //TODO: used as template parameters
    bool only_have_row_number = false;
    bool only_have_pure_window = false;
};

class WindowBlockInputStream : public IProfilingBlockInputStream
{
    static constexpr auto NAME = "Window";

public:
    WindowBlockInputStream(
        const BlockInputStreamPtr & input,
        const WindowDescription & window_description_,
        const String & req_id);

    Block getHeader() const override { return action.output_header; };

    String getName() const override { return NAME; }

protected:
    Block readImpl() override;
    void appendInfo(FmtBuffer & buffer) const override;
    bool returnIfCancelledOrKilled();

private:
    WindowTransformAction action;
};

} // namespace DB
