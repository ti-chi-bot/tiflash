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

#include <Operators/Operator.h>

namespace DB
{
class NullSourceOp : public SourceOp
{
public:
<<<<<<< HEAD
    NullSourceOp(
        PipelineExecutorStatus & exec_status_,
        const Block & header_,
        const String & req_id)
        : SourceOp(exec_status_, req_id)
=======
    NullSourceOp(PipelineExecutorContext & exec_context_, const Block & header_, const String & req_id)
        : SourceOp(exec_context_, req_id)
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))
    {
        setHeader(header_);
    }

    String getName() const override { return "NullSourceOp"; }

protected:
    OperatorStatus readImpl(Block & block) override
    {
        block = {};
        return OperatorStatus::HAS_OUTPUT;
    }
};
} // namespace DB
