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

#include <Interpreters/Join.h>
#include <Operators/Operator.h>

namespace DB
{
class HashJoinProbeTransformOp : public TransformOp
{
public:
    HashJoinProbeTransformOp(
        PipelineExecutorContext & exec_context_,
        const String & req_id,
        const JoinPtr & join_,
        size_t op_index_,
        size_t max_block_size,
        const Block & input_header);

    String getName() const override
    {
        return "HashJoinProbeTransformOp";
    }

protected:
    OperatorStatus transformImpl(Block & block) override;

    OperatorStatus tryOutputImpl(Block & block) override;

    OperatorStatus awaitImpl() override;

    void transformHeaderImpl(Block & header_) override;

    void operateSuffixImpl() override;

private:
    OperatorStatus onOutput(Block & block);

private:
    JoinPtr join;

    ProbeProcessInfo probe_process_info;

    size_t op_index;

    BlockInputStreamPtr scan_hash_map_after_probe_stream;

    size_t joined_rows = 0;
    size_t scan_hash_map_rows = 0;

    enum class ProbeStatus
    {
        PROBE, /// probe data
        WAIT_PROBE_FINISH, /// wait probe finish
        READ_SCAN_HASH_MAP_DATA, /// output scan hash map after probe data
        FINISHED, /// the final state
    };
    ProbeStatus status{ProbeStatus::PROBE};
};
} // namespace DB
