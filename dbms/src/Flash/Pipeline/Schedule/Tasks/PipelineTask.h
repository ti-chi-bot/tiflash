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

#include <Flash/Pipeline/Exec/PipelineExec.h>
#include <Flash/Pipeline/Schedule/Tasks/EventTask.h>

namespace DB
{
<<<<<<< HEAD
class PipelineTask : public EventTask
=======
class PipelineTask
    : public EventTask
    , public PipelineTaskBase
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))
{
public:
    PipelineTask(
        MemoryTrackerPtr mem_tracker_,
        const String & req_id,
        PipelineExecutorStatus & exec_status_,
        const EventPtr & event_,
<<<<<<< HEAD
        PipelineExecPtr && pipeline_exec_);

protected:
    ExecTaskStatus doExecuteImpl() override;

    ExecTaskStatus doExecuteIOImpl() override;

    ExecTaskStatus doAwaitImpl() override;
=======
        PipelineExecPtr && pipeline_exec_)
        : EventTask(exec_context_, req_id, event_, ExecTaskStatus::RUNNING)
        , PipelineTaskBase(std::move(pipeline_exec_))
    {}

protected:
    ExecTaskStatus executeImpl() override { return runExecute(); }

    ExecTaskStatus executeIOImpl() override { return runExecuteIO(); }

    ExecTaskStatus awaitImpl() override { return runAwait(); }
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))

    void finalizeImpl() override;

private:
    PipelineExecPtr pipeline_exec;
};
} // namespace DB
