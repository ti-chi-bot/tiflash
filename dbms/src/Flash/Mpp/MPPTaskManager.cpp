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

#include <Common/FailPoint.h>
#include <Common/FmtUtils.h>
#include <Common/TiFlashMetrics.h>
#include <Flash/Mpp/MPPTaskManager.h>
#include <fmt/core.h>

#include <magic_enum.hpp>
#include <string>
#include <unordered_map>

namespace DB
{
namespace FailPoints
{
extern const char random_task_manager_find_task_failure_failpoint[];
extern const char pause_before_register_non_root_mpp_task[];
} // namespace FailPoints

namespace
{
String getAbortedMessage(MPPQueryTaskSetPtr & query)
{
    if (query == nullptr || query->error_message.empty())
        return "query is aborted";
    return query->error_message;
}
} // namespace

MPPTaskManager::MPPTaskManager(MPPTaskSchedulerPtr scheduler_)
    : scheduler(std::move(scheduler_))
    , aborted_query_gather_cache(1000)
    , log(Logger::get())
{}

MPPQueryTaskSetPtr MPPTaskManager::addMPPQueryTaskSet(UInt64 query_id)
{
    auto ptr = std::make_shared<MPPQueryTaskSet>();
    mpp_query_map.insert({query_id, ptr});
    GET_METRIC(tiflash_mpp_task_manager, type_mpp_query_count).Set(mpp_query_map.size());
    return ptr;
}

void MPPTaskManager::removeMPPQueryTaskSet(UInt64 query_id, bool on_abort)
{
    scheduler->deleteQuery(query_id, *this, on_abort);
    mpp_query_map.erase(query_id);
    GET_METRIC(tiflash_mpp_task_manager, type_mpp_query_count).Set(mpp_query_map.size());
}

std::pair<MPPTunnelPtr, String> MPPTaskManager::findAsyncTunnel(const ::mpp::EstablishMPPConnectionRequest * request, EstablishCallData * call_data, grpc::CompletionQueue * cq)
{
    const auto & meta = request->sender_meta();
    MPPTaskId id{meta.start_ts(), meta.task_id()};
    Int64 sender_task_id = meta.task_id();
    Int64 receiver_task_id = request->receiver_meta().task_id();

    std::unique_lock lock(mu);
<<<<<<< HEAD
    auto query_it = mpp_query_map.find(id.start_ts);
    if (query_it != mpp_query_map.end() && !query_it->second->isInNormalState())
=======
    auto [query_set, already_aborted] = getQueryTaskSetWithoutLock(id.query_id);
    if (already_aborted)
>>>>>>> 12435a7c05 (Fix "lost cancel" for mpp query (#7589))
    {
        /// if the query is aborted, return the error message
        LOG_WARNING(log, fmt::format("Query {} is aborted, all its tasks are invalid.", id.start_ts));
        /// meet error
        return {nullptr, getAbortedMessage(query_set)};
    }

    if (query_set == nullptr || query_set->task_map.find(id) == query_set->task_map.end())
    {
        /// task not found
        if (!call_data->isWaitingTunnelState())
        {
            /// if call_data is in new_request state, put it to waiting tunnel state
<<<<<<< HEAD
            auto query_set = query_it == mpp_query_map.end() ? addMPPQueryTaskSet(id.start_ts) : query_it->second;
=======
            if (query_set == nullptr)
                query_set = addMPPQueryTaskSet(id.query_id);
>>>>>>> 12435a7c05 (Fix "lost cancel" for mpp query (#7589))
            auto & alarm = query_set->alarms[sender_task_id][receiver_task_id];
            call_data->setToWaitingTunnelState();
            alarm.Set(cq, Clock::now() + std::chrono::seconds(10), call_data);
            return {nullptr, ""};
        }
        else
        {
            /// if call_data is already in WaitingTunnelState, then remove the alarm and return tunnel not found error
            if (query_set != nullptr)
            {
                auto task_alarm_map_it = query_set->alarms.find(sender_task_id);
                if (task_alarm_map_it != query_set->alarms.end())
                {
                    task_alarm_map_it->second.erase(receiver_task_id);
                    if (task_alarm_map_it->second.empty())
                        query_set->alarms.erase(task_alarm_map_it);
                }
                if (query_set->alarms.empty() && query_set->task_map.empty())
                {
                    /// if the query task set has no mpp task, it has to be removed if there is no alarms left,
                    /// otherwise the query task set itself may be left in MPPTaskManager forever
                    removeMPPQueryTaskSet(id.start_ts, false);
                    cv.notify_all();
                }
            }
            return {nullptr, fmt::format("Can't find task [{},{}] within 10 s.", id.start_ts, id.task_id)};
        }
    }
    /// don't need to delete the alarm here because registerMPPTask will delete all the related alarm

    auto it = query_set->task_map.find(id);
    return it->second->getTunnel(request);
}

std::pair<MPPTunnelPtr, String> MPPTaskManager::findTunnelWithTimeout(const ::mpp::EstablishMPPConnectionRequest * request, std::chrono::seconds timeout)
{
    const auto & meta = request->sender_meta();
    MPPTaskId id{meta.start_ts(), meta.task_id()};
    std::unordered_map<MPPTaskId, MPPTaskPtr>::iterator it;
    bool cancelled = false;
    String error_message;
    std::unique_lock lock(mu);
    auto ret = cv.wait_for(lock, timeout, [&] {
<<<<<<< HEAD
        auto query_it = mpp_query_map.find(id.start_ts);
        // TODO: how about the query has been cancelled in advance?
        if (query_it == mpp_query_map.end())
        {
            return false;
        }
        else if (!query_it->second->isInNormalState())
=======
        auto [query_set, already_aborted] = getQueryTaskSetWithoutLock(id.query_id);
        if (already_aborted)
>>>>>>> 12435a7c05 (Fix "lost cancel" for mpp query (#7589))
        {
            /// if the query is aborted, return true to stop waiting timeout.
            LOG_WARNING(log, fmt::format("Query {} is aborted, all its tasks are invalid.", id.start_ts));
            cancelled = true;
            error_message = getAbortedMessage(query_set);
            return true;
        }
        if (query_set == nullptr)
        {
            return false;
        }
        it = query_set->task_map.find(id);
        return it != query_set->task_map.end();
    });
    fiu_do_on(FailPoints::random_task_manager_find_task_failure_failpoint, ret = false;);
    if (cancelled)
    {
        return {nullptr, fmt::format("Task [{},{}] has been aborted, error message: {}", meta.start_ts(), meta.task_id(), error_message)};
    }
    else if (!ret)
    {
        return {nullptr, fmt::format("Can't find task [{},{}] within {} s.", meta.start_ts(), meta.task_id(), timeout.count())};
    }
    return it->second->getTunnel(request);
}

void MPPTaskManager::abortMPPQuery(UInt64 query_id, const String & reason, AbortType abort_type)
{
    LOG_WARNING(log, fmt::format("Begin to abort query: {}, abort type: {}, reason: {}", query_id, magic_enum::enum_name(abort_type), reason));
    MPPQueryTaskSetPtr task_set;
    {
        /// abort task may take a long time, so first
        /// set a flag, so we can abort task one by
        /// one without holding the lock
        std::lock_guard lock(mu);
        /// gather_id is not set by TiDB, so use 0 instead
        aborted_query_gather_cache.add(MPPGatherId(0, query_id));
        auto it = mpp_query_map.find(query_id);
        if (it == mpp_query_map.end())
        {
            LOG_WARNING(log, fmt::format("{} does not found in task manager, skip abort", query_id));
            return;
        }
        else if (!it->second->isInNormalState())
        {
            LOG_WARNING(log, fmt::format("{} already in abort process, skip abort", query_id));
            return;
        }
        it->second->state = MPPQueryTaskSet::Aborting;
        it->second->error_message = reason;
        /// cancel all the alarms
        for (auto & alarms_per_task : it->second->alarms)
        {
            for (auto & alarm : alarms_per_task.second)
                alarm.second.Cancel();
        }
        it->second->alarms.clear();
        if (it->second->task_map.empty())
        {
            LOG_INFO(log, fmt::format("There is no mpp task for {}, finish abort", query_id));
            removeMPPQueryTaskSet(query_id, true);
            cv.notify_all();
            return;
        }
        task_set = it->second;
        scheduler->deleteQuery(query_id, *this, true);
        cv.notify_all();
    }

    FmtBuffer fmt_buf;
    fmt_buf.fmtAppend("Remaining task in query {} are: ", query_id);
    for (auto & it : task_set->task_map)
        fmt_buf.fmtAppend("{} ", it.first.toString());
    LOG_WARNING(log, fmt_buf.toString());

    for (auto & it : task_set->task_map)
        it.second->abort(reason, abort_type);

    {
        std::lock_guard lock(mu);
        auto it = mpp_query_map.find(query_id);
        RUNTIME_ASSERT(it != mpp_query_map.end(), log, "MPPTaskQuerySet {} should remaining in MPPTaskManager", query_id);
        it->second->state = MPPQueryTaskSet::Aborted;
        cv.notify_all();
    }
    LOG_WARNING(log, "Finish abort query: " + std::to_string(query_id));
}

std::pair<bool, String> MPPTaskManager::registerTask(MPPTaskPtr task)
{
    if (!task->isRootMPPTask())
    {
        FAIL_POINT_PAUSE(FailPoints::pause_before_register_non_root_mpp_task);
    }
    std::unique_lock lock(mu);
<<<<<<< HEAD
    const auto & it = mpp_query_map.find(task->id.start_ts);
    if (it != mpp_query_map.end() && !it->second->isInNormalState())
=======
    auto [query_set, already_aborted] = getQueryTaskSetWithoutLock(task->id.query_id);
    if (already_aborted)
>>>>>>> 12435a7c05 (Fix "lost cancel" for mpp query (#7589))
    {
        return {false, fmt::format("query is being aborted, error message = {}", getAbortedMessage(query_set))};
    }
    if (query_set != nullptr && query_set->task_map.find(task->id) != query_set->task_map.end())
    {
        return {false, "task has been registered"};
    }
    if (query_set == nullptr) /// the first one
    {
        query_set = addMPPQueryTaskSet(task->id.start_ts);
    }
    query_set->task_map.emplace(task->id, task);
    /// cancel all the alarm waiting on this task
    auto alarm_it = query_set->alarms.find(task->id.task_id);
    if (alarm_it != query_set->alarms.end())
    {
        for (auto & alarm : alarm_it->second)
            alarm.second.Cancel();
        query_set->alarms.erase(alarm_it);
    }
    task->registered = true;
    cv.notify_all();
    return {true, ""};
}

std::pair<bool, String> MPPTaskManager::unregisterTask(const MPPTaskId & id)
{
    std::unique_lock lock(mu);
    auto it = mpp_query_map.end();
    cv.wait(lock, [&] {
        it = mpp_query_map.find(id.start_ts);
        return it == mpp_query_map.end() || it->second->allowUnregisterTask();
    });
    if (it != mpp_query_map.end())
    {
        auto task_it = it->second->task_map.find(id);
        if (task_it != it->second->task_map.end())
        {
            it->second->task_map.erase(task_it);
            if (it->second->task_map.empty() && it->second->alarms.empty())
                removeMPPQueryTaskSet(id.start_ts, false);
            cv.notify_all();
            return {true, ""};
        }
    }
    cv.notify_all();
    return {false, "task can not be found, maybe not registered yet"};
}

String MPPTaskManager::toString()
{
    std::lock_guard lock(mu);
    String res("(");
    for (auto & query_it : mpp_query_map)
    {
        for (auto & it : query_it.second->task_map)
            res += it.first.toString() + ", ";
    }
    return res + ")";
}

<<<<<<< HEAD
MPPQueryTaskSetPtr MPPTaskManager::getQueryTaskSetWithoutLock(UInt64 query_id)
=======
std::pair<MPPQueryTaskSetPtr, bool> MPPTaskManager::getQueryTaskSetWithoutLock(const MPPQueryId & query_id)
>>>>>>> 12435a7c05 (Fix "lost cancel" for mpp query (#7589))
{
    auto it = mpp_query_map.find(query_id);
    /// gather_id is not set by TiDB, so use 0 instead
    bool already_aborted = aborted_query_gather_cache.exists(MPPGatherId(0, query_id));
    if (it != mpp_query_map.end())
    {
        already_aborted |= !it->second->isInNormalState();
        return std::make_pair(it->second, already_aborted);
    }
    else
    {
        return std::make_pair(nullptr, already_aborted);
    }
}

<<<<<<< HEAD
MPPQueryTaskSetPtr MPPTaskManager::getQueryTaskSet(UInt64 query_id)
=======
std::pair<MPPQueryTaskSetPtr, bool> MPPTaskManager::getQueryTaskSet(const MPPQueryId & query_id)
>>>>>>> 12435a7c05 (Fix "lost cancel" for mpp query (#7589))
{
    std::lock_guard lock(mu);
    return getQueryTaskSetWithoutLock(query_id);
}

bool MPPTaskManager::tryToScheduleTask(MPPTaskScheduleEntry & schedule_entry)
{
    std::lock_guard lock(mu);
    return scheduler->tryToSchedule(schedule_entry, *this);
}

void MPPTaskManager::releaseThreadsFromScheduler(const int needed_threads)
{
    std::lock_guard lock(mu);
    scheduler->releaseThreadsThenSchedule(needed_threads, *this);
}

} // namespace DB
