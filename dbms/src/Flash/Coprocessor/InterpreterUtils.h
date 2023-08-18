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

#pragma once

#include <Common/Logger.h>
#include <Flash/Coprocessor/DAGPipeline.h>
#include <Interpreters/ExpressionActions.h>

namespace DB
{
class Context;

<<<<<<< HEAD
void restoreConcurrency(
    DAGPipeline & pipeline,
    size_t concurrency,
    const LoggerPtr & log);
=======
class PipelineExecutorContext;
class PipelineExecGroupBuilder;

void restoreConcurrency(DAGPipeline & pipeline, size_t concurrency, Int64 max_buffered_bytes, const LoggerPtr & log);
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))

BlockInputStreamPtr combinedNonJoinedDataStream(
    DAGPipeline & pipeline,
    size_t max_threads,
    const LoggerPtr & log,
    bool ignore_block = false);

void executeUnion(
    DAGPipeline & pipeline,
    size_t max_streams,
    const LoggerPtr & log,
    bool ignore_block = false);

ExpressionActionsPtr generateProjectExpressionActions(
    const BlockInputStreamPtr & stream,
    const Context & context,
    const NamesWithAliases & project_cols);

<<<<<<< HEAD
=======
void executeExpression(
    DAGPipeline & pipeline,
    const ExpressionActionsPtr & expr_actions,
    const LoggerPtr & log,
    const String & extra_info = "");

void executeExpression(
    PipelineExecutorContext & exec_context,
    PipelineExecGroupBuilder & group_builder,
    const ExpressionActionsPtr & expr_actions,
    const LoggerPtr & log);

void orderStreams(
    DAGPipeline & pipeline,
    size_t max_streams,
    const SortDescription & order_descr,
    Int64 limit,
    bool enable_fine_grained_shuffle,
    const Context & context,
    const LoggerPtr & log);

void executeLocalSort(
    PipelineExecutorContext & exec_context,
    PipelineExecGroupBuilder & group_builder,
    const SortDescription & order_descr,
    std::optional<size_t> limit,
    const Context & context,
    const LoggerPtr & log);

void executeFinalSort(
    PipelineExecutorContext & exec_context,
    PipelineExecGroupBuilder & group_builder,
    const SortDescription & order_descr,
    std::optional<size_t> limit,
    const Context & context,
    const LoggerPtr & log);

void executeCreatingSets(DAGPipeline & pipeline, const Context & context, size_t max_streams, const LoggerPtr & log);

std::tuple<ExpressionActionsPtr, String, ExpressionActionsPtr> buildPushDownFilter(
    const google::protobuf::RepeatedPtrField<tipb::Expr> & conditions,
    DAGExpressionAnalyzer & analyzer);

void executePushedDownFilter(
    const FilterConditions & filter_conditions,
    DAGExpressionAnalyzer & analyzer,
    LoggerPtr log,
    DAGPipeline & pipeline);

void executePushedDownFilter(
    PipelineExecutorContext & exec_context,
    PipelineExecGroupBuilder & group_builder,
    const FilterConditions & filter_conditions,
    DAGExpressionAnalyzer & analyzer,
    LoggerPtr log);

>>>>>>> 6638f2067b (Fix license and format coding style (#7962))
void executeGeneratedColumnPlaceholder(
    size_t remote_read_streams_start_index,
    const std::vector<std::tuple<UInt64, String, DataTypePtr>> & generated_column_infos,
    LoggerPtr log,
    DAGPipeline & pipeline);
} // namespace DB
