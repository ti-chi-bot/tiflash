// Copyright 2022 PingCAP, Ltd.
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

#include <Common/CurrentMetrics.h>
#include <Storages/DeltaMerge/Segment.h>
#include <Storages/DeltaMerge/SegmentReadTaskPool.h>

namespace CurrentMetrics
{
extern const Metric DT_SegmentReadTasks;
}

namespace DB::DM
{
SegmentReadTask::SegmentReadTask(const SegmentPtr & segment_, //
                                 const SegmentSnapshotPtr & read_snapshot_,
                                 const RowKeyRanges & ranges_)
    : segment(segment_)
    , read_snapshot(read_snapshot_)
    , ranges(ranges_)
{
    CurrentMetrics::add(CurrentMetrics::DT_SegmentReadTasks);
}

SegmentReadTask::SegmentReadTask(const SegmentPtr & segment_, const SegmentSnapshotPtr & read_snapshot_)
    : SegmentReadTask{segment_, read_snapshot_, RowKeyRanges{}}
{
}

SegmentReadTask::~SegmentReadTask()
{
    CurrentMetrics::sub(CurrentMetrics::DT_SegmentReadTasks);
}

std::pair<size_t, size_t> SegmentReadTask::getRowsAndBytes() const
{
    return {read_snapshot->delta->getRows() + read_snapshot->stable->getRows(),
            read_snapshot->delta->getBytes() + read_snapshot->stable->getBytes()};
}

SegmentReadTasks SegmentReadTask::trySplitReadTasks(const SegmentReadTasks & tasks, size_t expected_size)
{
    if (tasks.empty() || tasks.size() >= expected_size)
        return tasks;

    // Note that expected_size is normally small(less than 100), so the algorithm complexity here does not matter.

    // Construct a max heap, determined by ranges' count.
    auto cmp = [](const SegmentReadTaskPtr & a, const SegmentReadTaskPtr & b) {
        return a->ranges.size() < b->ranges.size();
    };
    std::priority_queue<SegmentReadTaskPtr, std::vector<SegmentReadTaskPtr>, decltype(cmp)> largest_ranges_first(cmp);
    for (const auto & task : tasks)
        largest_ranges_first.push(task);

    // Split the top task.
    while (largest_ranges_first.size() < expected_size && largest_ranges_first.top()->ranges.size() > 1)
    {
        auto top = largest_ranges_first.top();
        largest_ranges_first.pop();

        size_t split_count = top->ranges.size() / 2;

        auto left = std::make_shared<SegmentReadTask>(
            top->segment,
            top->read_snapshot->clone(),
            RowKeyRanges(top->ranges.begin(), top->ranges.begin() + split_count));
        auto right = std::make_shared<SegmentReadTask>(
            top->segment,
            top->read_snapshot->clone(),
            RowKeyRanges(top->ranges.begin() + split_count, top->ranges.end()));

        largest_ranges_first.push(left);
        largest_ranges_first.push(right);
    }

    SegmentReadTasks result_tasks;
    while (!largest_ranges_first.empty())
    {
        result_tasks.push_back(largest_ranges_first.top());
        largest_ranges_first.pop();
    }

    return result_tasks;
}

BlockInputStreamPtr SegmentReadTaskPool::buildInputStream(SegmentReadTaskPtr & t)
{
    MemoryTrackerSetter setter(true, mem_tracker.get());
    auto seg = t->segment;
    BlockInputStreamPtr stream;
    if (is_raw)
    {
        stream = seg->getInputStreamRaw(*dm_context, columns_to_read, t->read_snapshot, t->ranges, filter, do_range_filter_for_raw);
    }
    else
    {
        auto block_size = std::max(expected_block_size, static_cast<size_t>(dm_context->db_context.getSettingsRef().dt_segment_stable_pack_rows));
        stream = seg->getInputStream(*dm_context, columns_to_read, t->read_snapshot, t->ranges, filter, max_version, block_size);
    }
    LOG_FMT_DEBUG(log, "getInputStream succ, pool_id={} segment_id={}", pool_id, seg->segmentId());
    return stream;
}

void SegmentReadTaskPool::finishSegment(const SegmentPtr & seg)
{
    after_segment_read(dm_context, seg);
    bool pool_finished = false;
    {
        std::lock_guard lock(mutex);
        active_segment_ids.erase(seg->segmentId());
        pool_finished = active_segment_ids.empty() && tasks.empty();
    }
    LOG_FMT_DEBUG(log, "finishSegment pool_id={} segment_id={} pool_finished={}", pool_id, seg->segmentId(), pool_finished);
    if (pool_finished)
    {
        q.finish();
    }
}

SegmentReadTaskPtr SegmentReadTaskPool::getTask(uint64_t seg_id)
{
    std::lock_guard lock(mutex);
    // TODO(jinhelin): use unordered_map
    auto itr = std::find_if(tasks.begin(), tasks.end(), [seg_id](const SegmentReadTaskPtr & task) { return task->segment->segmentId() == seg_id; });
    if (itr == tasks.end())
    {
        throw Exception(fmt::format("{} pool_id={} segment_id={} not found", __PRETTY_FUNCTION__, pool_id, seg_id));
    }
    auto t = *(itr);
    tasks.erase(itr);
    active_segment_ids.insert(seg_id);
    return t;
}

// Choose a segment to read.
// Returns <segment_id, pool_ids>.
std::unordered_map<uint64_t, std::vector<uint64_t>>::const_iterator SegmentReadTaskPool::scheduleSegment(const std::unordered_map<uint64_t, std::vector<uint64_t>> & segments, uint64_t expected_merge_count)
{
    auto target = segments.end();
    std::lock_guard lock(mutex);
    if (getFreeActiveSegmentCountUnlock() <= 0)
    {
        return target;
    }
    for (const auto & task : tasks)
    {
        auto itr = segments.find(task->segment->segmentId());
        if (itr == segments.end())
        {
            throw DB::Exception(fmt::format("segment_id {} not found from merging segments", task->segment->segmentId()));
        }
        if (std::find(itr->second.begin(), itr->second.end(), poolId()) == itr->second.end())
        {
            throw DB::Exception(fmt::format("pool_id={} not found from merging segment {}=>{}", poolId(), itr->first, itr->second));
        }
        if (target == segments.end() || itr->second.size() > target->second.size())
        {
            target = itr;
        }
        if (target->second.size() >= expected_merge_count)
        {
            break;
        }
    }
    return target;
}

bool SegmentReadTaskPool::readOneBlock(BlockInputStreamPtr & stream, const SegmentPtr & seg)
{
    MemoryTrackerSetter setter(true, mem_tracker.get());
    auto block = stream->read();
    if (block)
    {
        pushBlock(std::move(block));
        return true;
    }
    else
    {
        finishSegment(seg);
        return false;
    }
}

void SegmentReadTaskPool::popBlock(Block & block)
{
    q.pop(block);
    blk_stat.pop(block);
    global_blk_stat.pop(block);
    if (exceptionHappened())
    {
        throw exception;
    }
}

void SegmentReadTaskPool::pushBlock(Block && block)
{
    blk_stat.push(block);
    global_blk_stat.push(block);
    q.push(std::move(block), nullptr);
}

int64_t SegmentReadTaskPool::increaseUnorderedInputStreamRefCount()
{
    return unordered_input_stream_ref_count.fetch_add(1, std::memory_order_relaxed);
}
int64_t SegmentReadTaskPool::decreaseUnorderedInputStreamRefCount()
{
    return unordered_input_stream_ref_count.fetch_sub(1, std::memory_order_relaxed);
}

int64_t SegmentReadTaskPool::getFreeBlockSlots() const
{
    auto block_slots = unordered_input_stream_ref_count.load(std::memory_order_relaxed);
    if (block_slots < 3)
    {
        block_slots = 3;
    }
    return block_slots - blk_stat.pendingCount();
}

int64_t SegmentReadTaskPool::getFreeActiveSegmentCountUnlock()
{
    auto active_segment_limit = unordered_input_stream_ref_count.load(std::memory_order_relaxed);
    if (active_segment_limit < 2)
    {
        active_segment_limit = 2;
    }
    return active_segment_limit - static_cast<int64_t>(active_segment_ids.size());
}

bool SegmentReadTaskPool::exceptionHappened() const
{
    return exception_happened.load(std::memory_order_relaxed);
}

bool SegmentReadTaskPool::valid() const
{
    return !exceptionHappened() && unordered_input_stream_ref_count.load(std::memory_order_relaxed) > 0;
}
void SegmentReadTaskPool::setException(const DB::Exception & e)
{
    std::lock_guard lock(mutex);
    if (!exceptionHappened())
    {
        exception = e;
        exception_happened.store(true, std::memory_order_relaxed);
        q.finish();
    }
}

} // namespace DB::DM
