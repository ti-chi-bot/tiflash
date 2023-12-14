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

#include <Common/FailPoint.h>
#include <DataStreams/IProfilingBlockInputStream.h>
#include <DataStreams/SegmentReadTransformAction.h>
#include <Storages/DeltaMerge/ReadThread/SegmentReadTaskScheduler.h>
#include <Storages/DeltaMerge/SegmentReadTaskPool.h>

namespace DB::FailPoints
{
extern const char pause_when_reading_from_dt_stream[];
}

namespace DB::DM
{
class UnorderedInputStream : public IProfilingBlockInputStream
{
    static constexpr auto NAME = "UnorderedInputStream";

public:
    UnorderedInputStream(
        const SegmentReadTaskPoolPtr & task_pool_,
        const ColumnDefines & columns_to_read_,
        const int extra_table_id_index,
        const TableID physical_table_id,
        const String & req_id)
        : task_pool(task_pool_)
        , header(toEmptyBlock(columns_to_read_))
        , action(header, extra_table_id_index, physical_table_id)
        , log(Logger::get(req_id))
        , ref_no(0)
        , task_pool_added(false)

    {
        if (extra_table_id_index != InvalidColumnID)
        {
            const auto & extra_table_id_col_define = getExtraTableIDColumnDefine();
            ColumnWithTypeAndName col{extra_table_id_col_define.type->createColumn(), extra_table_id_col_define.type, extra_table_id_col_define.name, extra_table_id_col_define.id, extra_table_id_col_define.default_value};
            header.insert(extra_table_id_index, col);
        }
        ref_no = task_pool->increaseUnorderedInputStreamRefCount();
        LOG_DEBUG(log, "Created, pool_id={} ref_no={}", task_pool->poolId(), ref_no);
    }

<<<<<<< HEAD
    ~UnorderedInputStream() override
    {
        task_pool->decreaseUnorderedInputStreamRefCount();
        LOG_DEBUG(log, "Destroy, pool_id={} ref_no={}", task_pool->poolId(), ref_no);
    }
=======
    void cancel(bool /*kill*/) override { decreaseRefCount(true); }

    ~UnorderedInputStream() override { decreaseRefCount(false); }
>>>>>>> d344d9a872 (Process streams of partition tables one by one in MultiplexInputStream (#8507))

    String getName() const override { return NAME; }

    Block getHeader() const override { return header; }

protected:
<<<<<<< HEAD
=======
    void decreaseRefCount(bool is_cancel)
    {
        bool ori = false;
        if (is_stopped.compare_exchange_strong(ori, true))
        {
            task_pool->decreaseUnorderedInputStreamRefCount();
            LOG_DEBUG(log, "{}, pool_id={} ref_no={}", is_cancel ? "Cancel" : "Destroy", task_pool->pool_id, ref_no);
        }
    }

>>>>>>> d344d9a872 (Process streams of partition tables one by one in MultiplexInputStream (#8507))
    Block readImpl() override
    {
        FilterPtr filter_ignored;
        return readImpl(filter_ignored, false);
    }

    // Currently, res_filter and return_filter is unused.
    Block readImpl(FilterPtr & /*res_filter*/, bool /*return_filter*/) override
    {
        if (done)
        {
            return {};
        }
        addReadTaskPoolToScheduler();
        while (true)
        {
            FAIL_POINT_PAUSE(FailPoints::pause_when_reading_from_dt_stream);
            Block res;
            task_pool->popBlock(res);
            if (res)
            {
                if (action.transform(res))
                {
                    return res;
                }
                else
                {
                    continue;
                }
            }
            else
            {
                done = true;
                return {};
            }
        }
    }

    void readSuffixImpl() override
    {
        LOG_DEBUG(log, "Finish read from storage, pool_id={} ref_no={} rows={}", task_pool->poolId(), ref_no, action.totalRows());
    }

    void addReadTaskPoolToScheduler()
    {
        if (likely(task_pool_added))
        {
            return;
        }
        std::call_once(task_pool->addToSchedulerFlag(), [&]() { SegmentReadTaskScheduler::instance().add(task_pool); });
        task_pool_added = true;
    }

private:
    SegmentReadTaskPoolPtr task_pool;
    Block header;
    SegmentReadTransformAction action;

    bool done = false;
    LoggerPtr log;
    int64_t ref_no;
    bool task_pool_added;
};
} // namespace DB::DM
