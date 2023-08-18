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

#include <Debug/astToExecutor.h>
#include <Interpreters/Context.h>
#include <Parsers/ASTFunction.h>
#include <tipb/executor.pb.h>

#include <initializer_list>
#include <unordered_map>

namespace DB::tests
{
using MockColumnInfo = std::pair<String, TiDB::TP>;
using MockColumnInfos = std::vector<MockColumnInfo>;
using MockColumnInfoList = std::initializer_list<MockColumnInfo>;
using MockTableName = std::pair<String, String>;
using MockOrderByItem = std::pair<String, bool>;
<<<<<<< HEAD
using MockOrderByItems = std::initializer_list<MockOrderByItem>;
using MockColumnNames = std::initializer_list<String>;
using MockAsts = std::initializer_list<ASTPtr>;
=======
using MockOrderByItemVec = std::vector<MockOrderByItem>;
using MockPartitionByItem = std::pair<String, bool>;
using MockPartitionByItemVec = std::vector<MockPartitionByItem>;
using MockColumnNameVec = std::vector<String>;
using MockVecColumnNameVec
    = std::vector<MockColumnNameVec>; // for grouping set (every groupingExpr element inside is slice of column)
using MockVVecColumnNameVec = std::vector<MockVecColumnNameVec>; // for grouping sets
using MockAstVec = std::vector<ASTPtr>;
using MockWindowFrame = mock::MockWindowFrame;
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))

class MockDAGRequestContext;

/** Responsible for Hand write tipb::DAGRequest
  * Use this class to mock DAGRequest, then feed the DAGRequest into 
  * the Interpreter for test purpose.
  * The mockTable() method must called first in order to generate the table schema.
  * After construct all necessary operators in DAGRequest, call build() to generate DAGRequest。
  */
class DAGRequestBuilder
{
public:
    size_t & executor_index;

    size_t & getExecutorIndex() const { return executor_index; }

    explicit DAGRequestBuilder(size_t & index)
        : executor_index(index)
    {
    }

<<<<<<< HEAD
    ExecutorPtr getRoot()
    {
        return root;
    }

    std::shared_ptr<tipb::DAGRequest> build(MockDAGRequestContext & mock_context);
=======
    mock::ExecutorBinderPtr getRoot() { return root; }

    std::shared_ptr<tipb::DAGRequest> build(
        MockDAGRequestContext & mock_context,
        DAGRequestType type = DAGRequestType::tree);
    QueryTasks buildMPPTasks(MockDAGRequestContext & mock_context);
    QueryTasks buildMPPTasks(MockDAGRequestContext & mock_context, const DAGProperties & properties);
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))

    DAGRequestBuilder & mockTable(const String & db, const String & table, const MockColumnInfos & columns);
    DAGRequestBuilder & mockTable(const MockTableName & name, const MockColumnInfos & columns);
    DAGRequestBuilder & mockTable(const MockTableName & name, const MockColumnInfoList & columns);

<<<<<<< HEAD
    DAGRequestBuilder & exchangeReceiver(const MockColumnInfos & columns);
    DAGRequestBuilder & exchangeReceiver(const MockColumnInfoList & columns);
=======
    DAGRequestBuilder & exchangeReceiver(
        const String & exchange_name,
        const MockColumnInfoVec & columns,
        uint64_t fine_grained_shuffle_stream_count = 0);
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))

    DAGRequestBuilder & filter(ASTPtr filter_expr);

    DAGRequestBuilder & limit(int limit);
    DAGRequestBuilder & limit(ASTPtr limit_expr);

    DAGRequestBuilder & topN(ASTPtr order_exprs, ASTPtr limit_expr);
    DAGRequestBuilder & topN(const String & col_name, bool desc, int limit);
    DAGRequestBuilder & topN(MockOrderByItems order_by_items, int limit);
    DAGRequestBuilder & topN(MockOrderByItems order_by_items, ASTPtr limit_expr);

<<<<<<< HEAD
    DAGRequestBuilder & project(const String & col_name);
    DAGRequestBuilder & project(MockAsts expr);
    DAGRequestBuilder & project(MockColumnNames col_names);

    DAGRequestBuilder & exchangeSender(tipb::ExchangeType exchange_type);
=======
    DAGRequestBuilder & project(std::initializer_list<ASTPtr> exprs) { return project(MockAstVec{exprs}); }
    DAGRequestBuilder & project(MockAstVec exprs);

    DAGRequestBuilder & project(std::initializer_list<String> exprs) { return project(MockColumnNameVec{exprs}); }
    DAGRequestBuilder & project(MockColumnNameVec col_names);

    DAGRequestBuilder & exchangeSender(
        tipb::ExchangeType exchange_type,
        MockColumnNameVec part_keys = {},
        uint64_t fine_grained_shuffle_stream_count = 0);

    /// User should prefer using other simplified join buidler API instead of this one unless he/she have to test
    /// join conditional expressions and knows how TiDB translates sql's `join on` clause to conditional expressions.
    /// Note that since our framework does not support qualified column name yet, while building column reference
    /// the first column with the matching name in correspoding table(s) will be used.
    /// todo: support qualified name column reference in expression
    ///
    /// @param join_col_exprs matching columns for the joined table, which must have the same name
    /// @param left_conds conditional expressions which only reference left table and the join type is left kind
    /// @param right_conds conditional expressions which only reference right table and the join type is right kind
    /// @param other_conds other conditional expressions
    /// @param other_eq_conds_from_in equality expressions within in subquery whose join type should be AntiSemiJoin, AntiLeftOuterSemiJoin or LeftOuterSemiJoin
    /// @param fine_grained_shuffle_stream_count decide the generated tipb executor's find_grained_shuffle_stream_count
    /// @param is_null_aware_semi_join indicates whether to use null-aware semi join and join type should be AntiSemiJoin, AntiLeftOuterSemiJoin or LeftOuterSemiJoin
    /// @param inner_index indicates use which side to build hash table
    DAGRequestBuilder & join(
        const DAGRequestBuilder & right,
        tipb::JoinType tp,
        MockAstVec join_col_exprs,
        MockAstVec left_conds,
        MockAstVec right_conds,
        MockAstVec other_conds,
        MockAstVec other_eq_conds_from_in,
        uint64_t fine_grained_shuffle_stream_count = 0,
        bool is_null_aware_semi_join = false,
        int64_t inner_index = 1);
    DAGRequestBuilder & join(
        const DAGRequestBuilder & right,
        tipb::JoinType tp,
        MockAstVec join_col_exprs,
        uint64_t fine_grained_shuffle_stream_count = 0)
    {
        return join(right, tp, join_col_exprs, {}, {}, {}, {}, fine_grained_shuffle_stream_count);
    }
    DAGRequestBuilder & join(
        const DAGRequestBuilder & right,
        tipb::JoinType tp,
        MockAstVec join_col_exprs,
        mock::MockRuntimeFilter & rf,
        uint64_t fine_grained_shuffle_stream_count = 0)
    {
        return join(right, tp, join_col_exprs, {}, {}, {}, {}, fine_grained_shuffle_stream_count)
            .appendRuntimeFilter(rf);
    }
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))

    // Currentlt only support inner join, left join and right join.
    // TODO support more types of join.
    DAGRequestBuilder & join(const DAGRequestBuilder & right, MockAsts exprs);
    DAGRequestBuilder & join(const DAGRequestBuilder & right, MockAsts exprs, ASTTableJoin::Kind kind);

    // aggregation
<<<<<<< HEAD
    DAGRequestBuilder & aggregation(ASTPtr agg_func, ASTPtr group_by_expr);
    DAGRequestBuilder & aggregation(MockAsts agg_funcs, MockAsts group_by_exprs);

private:
    void initDAGRequest(tipb::DAGRequest & dag_request);
    DAGRequestBuilder & buildAggregation(ASTPtr agg_funcs, ASTPtr group_by_exprs);
    DAGRequestBuilder & buildExchangeReceiver(const MockColumnInfos & columns);
=======
    DAGRequestBuilder & aggregation(
        ASTPtr agg_func,
        ASTPtr group_by_expr,
        uint64_t fine_grained_shuffle_stream_count = 0);
    DAGRequestBuilder & aggregation(
        MockAstVec agg_funcs,
        MockAstVec group_by_exprs,
        uint64_t fine_grained_shuffle_stream_count = 0);

    // window
    DAGRequestBuilder & window(
        ASTPtr window_func,
        MockOrderByItem order_by,
        MockPartitionByItem partition_by,
        MockWindowFrame frame,
        uint64_t fine_grained_shuffle_stream_count = 0);
    DAGRequestBuilder & window(
        MockAstVec window_funcs,
        MockOrderByItemVec order_by_vec,
        MockPartitionByItemVec partition_by_vec,
        MockWindowFrame frame,
        uint64_t fine_grained_shuffle_stream_count = 0);
    DAGRequestBuilder & window(
        ASTPtr window_func,
        MockOrderByItemVec order_by_vec,
        MockPartitionByItemVec partition_by_vec,
        MockWindowFrame frame,
        uint64_t fine_grained_shuffle_stream_count = 0);
    DAGRequestBuilder & sort(
        MockOrderByItem order_by,
        bool is_partial_sort,
        uint64_t fine_grained_shuffle_stream_count = 0);
    DAGRequestBuilder & sort(
        MockOrderByItemVec order_by_vec,
        bool is_partial_sort,
        uint64_t fine_grained_shuffle_stream_count = 0);

    // expand
    DAGRequestBuilder & expand(MockVVecColumnNameVec grouping_set_columns);
    DAGRequestBuilder & expand2(
        std::vector<MockAstVec> level_projection_expressions,
        std::vector<String> output_names,
        std::vector<tipb::FieldType> fps);

    // runtime filter
    DAGRequestBuilder & appendRuntimeFilter(mock::MockRuntimeFilter & rf);

    void setCollation(Int32 collator_) { properties.collator = convertToTiDBCollation(collator_); }
    Int32 getCollation() const { return abs(properties.collator); }

private:
    void initDAGRequest(tipb::DAGRequest & dag_request);
    DAGRequestBuilder & buildAggregation(
        ASTPtr agg_funcs,
        ASTPtr group_by_exprs,
        uint64_t fine_grained_shuffle_stream_count = 0);
    DAGRequestBuilder & buildExchangeReceiver(
        const String & exchange_name,
        const MockColumnInfoVec & columns,
        uint64_t fine_grained_shuffle_stream_count = 0);
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))

    ExecutorPtr root;
    DAGProperties properties;
};

/** Responsible for storing necessary arguments in order to Mock DAGRequest
  * index: used in DAGRequestBuilder to identify executors
  * mock_tables: DAGRequestBuilder uses it to mock TableScan executors
  */
class MockDAGRequestContext
{
public:
<<<<<<< HEAD
    explicit MockDAGRequestContext(Context context_)
        : context(context_)
    {
        index = 0;
    }

    DAGRequestBuilder createDAGRequestBuilder()
    {
        return DAGRequestBuilder(index);
    }

    void addMockTable(const MockTableName & name, const MockColumnInfoList & columns);
    void addMockTable(const String & db, const String & table, const MockColumnInfos & columns);
    void addMockTable(const MockTableName & name, const MockColumnInfos & columns);
    void addExchangeRelationSchema(String name, const MockColumnInfos & columns);
    void addExchangeRelationSchema(String name, const MockColumnInfoList & columns);
    DAGRequestBuilder scan(String db_name, String table_name);
    DAGRequestBuilder receive(String exchange_name);
=======
    explicit MockDAGRequestContext(ContextPtr context_, Int32 collation_ = TiDB::ITiDBCollator::UTF8MB4_BIN)
        : index(0)
        , context(context_)
        , collation(convertToTiDBCollation(collation_))
    {}

    DAGRequestBuilder createDAGRequestBuilder() { return DAGRequestBuilder(index); }
    /// mock column table scan
    void addMockTable(
        const String & db,
        const String & table,
        const MockColumnInfoVec & columnInfos,
        size_t concurrency_hint = 0);
    void addMockTable(const MockTableName & name, const MockColumnInfoVec & columnInfos, size_t concurrency_hint = 0);
    void addMockTable(
        const String & db,
        const String & table,
        const MockColumnInfoVec & columnInfos,
        ColumnsWithTypeAndName columns,
        size_t concurrency_hint = 0);
    void addMockTable(
        const MockTableName & name,
        const MockColumnInfoVec & columnInfos,
        ColumnsWithTypeAndName columns,
        size_t concurrency_hint = 0);

    void updateMockTableColumnData(const String & db, const String & table, ColumnsWithTypeAndName columns)
    {
        addMockTableColumnData(db, table, columns);
    }

    /// mock DeltaMerge table scan
    void addMockDeltaMerge(
        const MockTableName & name,
        const MockColumnInfoVec & columnInfos,
        ColumnsWithTypeAndName columns);
    void addMockDeltaMerge(
        const MockTableName & name,
        const MockColumnInfoVec & columnInfos,
        ColumnsWithTypeAndName columns,
        size_t concurrency_hint);
    void addMockDeltaMerge(
        const String & db,
        const String & table,
        const MockColumnInfoVec & columnInfos,
        ColumnsWithTypeAndName columns);

    void addMockDeltaMergeSchema(const String & db, const String & table, const MockColumnInfoVec & columnInfos);
    void addMockDeltaMergeData(const String & db, const String & table, ColumnsWithTypeAndName columns);

    void addMockDeltaMergeTableConcurrencyHint(const MockTableName & name, size_t concurrency_hint);

    /// mock column exchange receiver
    void addExchangeReceiver(
        const String & name,
        const MockColumnInfoVec & columnInfos,
        const ColumnsWithTypeAndName & columns,
        size_t fine_grained_stream_count = 0,
        const MockColumnInfoVec & partition_column_infos = {});
    void addExchangeReceiver(
        const String & name,
        const MockColumnInfoVec & columnInfos,
        size_t fine_grained_stream_count = 0,
        const MockColumnInfoVec & partition_column_infos = {});

    DAGRequestBuilder scan(const String & db_name, const String & table_name, bool keep_order = false);

    DAGRequestBuilder scan(const String & db_name, const String & table_name, const std::vector<int> & rf_ids);

    DAGRequestBuilder receive(const String & exchange_name, uint64_t fine_grained_shuffle_stream_count = 0);

    void setCollation(Int32 collation_) { collation = convertToTiDBCollation(collation_); }
    Int32 getCollation() const { return abs(collation); }

    MockStorage * mockStorage() { return mock_storage.get(); }
    void initMockStorage();

private:
    static void assertMockInput(const MockColumnInfoVec & columnInfos, ColumnsWithTypeAndName columns);
    void addExchangeReceiverColumnData(const String & name, ColumnsWithTypeAndName columns);
    void addExchangeRelationSchema(String name, const MockColumnInfoVec & columnInfos);
    void addMockTableSchema(const String & db, const String & table, const MockColumnInfoVec & columnInfos);
    void addMockTableSchema(const MockTableName & name, const MockColumnInfoVec & columnInfos);
    void addMockTableConcurrencyHint(const String & db, const String & table, size_t concurrency_hint);
    void addMockTableConcurrencyHint(const MockTableName & name, size_t concurrency_hint);
    void addMockTableColumnData(const String & db, const String & table, ColumnsWithTypeAndName columns);
    void addMockTableColumnData(const MockTableName & name, ColumnsWithTypeAndName columns);
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))

private:
    size_t index;
    std::unordered_map<String, MockColumnInfos> mock_tables;
    std::unordered_map<String, MockColumnInfos> exchange_schemas;

public:
    // Currently don't support task_id, so the following to structure is useless,
    // but we need it to contruct the TaskMeta.
    // In TiFlash, we use task_id to identify an Mpp Task.
    std::unordered_map<String, std::vector<Int64>> receiver_source_task_ids_map;
    Context context;
};

ASTPtr buildColumn(const String & column_name);
ASTPtr buildLiteral(const Field & field);
ASTPtr buildFunction(MockAsts exprs, const String & name);
ASTPtr buildOrderByItemList(MockOrderByItems order_by_items);

#define col(name) buildColumn((name))
#define lit(field) buildLiteral((field))
#define eq(expr1, expr2) makeASTFunction("equals", (expr1), (expr2))
#define Not_eq(expr1, expr2) makeASTFunction("notEquals", (expr1), (expr2))
#define lt(expr1, expr2) makeASTFunction("less", (expr1), (expr2))
#define gt(expr1, expr2) makeASTFunction("greater", (expr1), (expr2))
#define And(expr1, expr2) makeASTFunction("and", (expr1), (expr2))
#define Or(expr1, expr2) makeASTFunction("or", (expr1), (expr2))
#define NOT(expr) makeASTFunction("not", (expr1), (expr2))
#define Max(expr) makeASTFunction("max", expr)

} // namespace DB::tests