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

#include <Operators/AggregateBuildSinkOp.h>

namespace DB
{
OperatorStatus AggregateBuildSinkOp::writeImpl(Block && block)
{
    if (unlikely(!block))
    {
        return OperatorStatus::FINISHED;
    }
    agg_context->buildOnBlock(index, block);
    total_rows += block.rows();
    block.clear();
<<<<<<< HEAD
    return OperatorStatus::NEED_INPUT;
}

void AggregateBuildSinkOp::operateSuffix()
=======
    return agg_context->needSpill(index) ? OperatorStatus::IO_OUT : OperatorStatus::NEED_INPUT;
}

OperatorStatus AggregateBuildSinkOp::executeIOImpl()
{
    agg_context->spillData(index);
    return is_final_spill ? OperatorStatus::FINISHED : OperatorStatus::NEED_INPUT;
}

void AggregateBuildSinkOp::operateSuffixImpl()
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))
{
    LOG_DEBUG(log, "finish build with {} rows", total_rows);
}

} // namespace DB
