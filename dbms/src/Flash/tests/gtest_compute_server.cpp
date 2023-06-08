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

#include <TestUtils/MPPTaskTestUtils.h>

namespace DB
{
namespace FailPoints
{
extern const char exception_before_mpp_register_non_root_mpp_task[];
extern const char exception_before_mpp_register_root_mpp_task[];
extern const char exception_before_mpp_register_tunnel_for_non_root_mpp_task[];
extern const char exception_before_mpp_register_tunnel_for_root_mpp_task[];
extern const char exception_during_mpp_register_tunnel_for_non_root_mpp_task[];
extern const char exception_before_mpp_non_root_task_run[];
extern const char exception_before_mpp_root_task_run[];
extern const char exception_during_mpp_non_root_task_run[];
extern const char exception_during_mpp_root_task_run[];
} // namespace FailPoints
namespace tests
{

LoggerPtr MPPTaskTestUtils::log_ptr = nullptr;
size_t MPPTaskTestUtils::server_num = 0;
MPPTestMeta MPPTaskTestUtils::test_meta = {};

class ComputeServerRunner : public DB::tests::MPPTaskTestUtils
{
public:
    void initializeContext() override
    {
        ExecutorTest::initializeContext();
        /// for agg
        context.addMockTable(
            {"test_db", "test_table_1"},
            {{"s1", TiDB::TP::TypeLong}, {"s2", TiDB::TP::TypeString}, {"s3", TiDB::TP::TypeString}},
            {toNullableVec<Int32>("s1", {1, {}, 10000000, 10000000}), toNullableVec<String>("s2", {"apple", {}, "banana", "test"}), toNullableVec<String>("s3", {"apple", {}, "banana", "test"})});

        /// for join
        context.addMockTable(
            {"test_db", "l_table"},
            {{"s", TiDB::TP::TypeString}, {"join_c", TiDB::TP::TypeString}},
            {toNullableVec<String>("s", {"banana", {}, "banana"}), toNullableVec<String>("join_c", {"apple", {}, "banana"})});
        context.addMockTable(
            {"test_db", "r_table"},
            {{"s", TiDB::TP::TypeString}, {"join_c", TiDB::TP::TypeString}},
            {toNullableVec<String>("s", {"banana", {}, "banana"}), toNullableVec<String>("join_c", {"apple", {}, "banana"})});
    }
};

TEST_F(ComputeServerRunner, runAggTasks)
try
{
    startServers(4);
    {
        std::vector<String> expected_strings = {
            R"(exchange_sender_5 | type:Hash, {<0, Long>, <1, String>, <2, String>}
 aggregation_4 | group_by: {<1, String>, <2, String>}, agg_func: {max(<0, Long>)}
  table_scan_0 | {<0, Long>, <1, String>, <2, String>}
)",
            R"(exchange_sender_5 | type:Hash, {<0, Long>, <1, String>, <2, String>}
 aggregation_4 | group_by: {<1, String>, <2, String>}, agg_func: {max(<0, Long>)}
  table_scan_0 | {<0, Long>, <1, String>, <2, String>}
)",
            R"(exchange_sender_5 | type:Hash, {<0, Long>, <1, String>, <2, String>}
 aggregation_4 | group_by: {<1, String>, <2, String>}, agg_func: {max(<0, Long>)}
  table_scan_0 | {<0, Long>, <1, String>, <2, String>}
)",
            R"(exchange_sender_5 | type:Hash, {<0, Long>, <1, String>, <2, String>}
 aggregation_4 | group_by: {<1, String>, <2, String>}, agg_func: {max(<0, Long>)}
  table_scan_0 | {<0, Long>, <1, String>, <2, String>}
)",
            R"(exchange_sender_3 | type:PassThrough, {<0, Long>}
 project_2 | {<0, Long>}
  aggregation_1 | group_by: {<1, String>, <2, String>}, agg_func: {max(<0, Long>)}
   exchange_receiver_6 | type:PassThrough, {<0, Long>, <1, String>, <2, String>}
)",
            R"(exchange_sender_3 | type:PassThrough, {<0, Long>}
 project_2 | {<0, Long>}
  aggregation_1 | group_by: {<1, String>, <2, String>}, agg_func: {max(<0, Long>)}
   exchange_receiver_6 | type:PassThrough, {<0, Long>, <1, String>, <2, String>}
)",
            R"(
exchange_sender_3 | type:PassThrough, {<0, Long>}
 project_2 | {<0, Long>}
  aggregation_1 | group_by: {<1, String>, <2, String>}, agg_func: {max(<0, Long>)}
   exchange_receiver_6 | type:PassThrough, {<0, Long>, <1, String>, <2, String>}
)",
            R"(exchange_sender_3 | type:PassThrough, {<0, Long>}
 project_2 | {<0, Long>}
  aggregation_1 | group_by: {<1, String>, <2, String>}, agg_func: {max(<0, Long>)}
   exchange_receiver_6 | type:PassThrough, {<0, Long>, <1, String>, <2, String>}
)"};
        auto expected_cols = {toNullableVec<Int32>({1, {}, 10000000, 10000000})};

        ASSERT_MPPTASK_EQUAL_PLAN_AND_RESULT(
            context
                .scan("test_db", "test_table_1")
                .aggregation({Max(col("s1"))}, {col("s2"), col("s3")})
                .project({"max(s1)"}),
            expected_strings,
            expected_cols);
    }

    {
        auto properties = getDAGPropertiesForTest(1);
        auto tasks = context
                         .scan("test_db", "test_table_1")
                         .aggregation({Count(col("s1"))}, {})
                         .project({"count(s1)"})
                         .buildMPPTasks(context, properties);
        std::vector<String> expected_strings = {
            R"(exchange_sender_5 | type:PassThrough, {<0, Longlong>}
 aggregation_4 | group_by: {}, agg_func: {count(<0, Long>)}
  table_scan_0 | {<0, Long>}
            )",
            R"(exchange_sender_3 | type:PassThrough, {<0, Longlong>}
 project_2 | {<0, Longlong>}
  aggregation_1 | group_by: {}, agg_func: {sum(<0, Longlong>)}
   exchange_receiver_6 | type:PassThrough, {<0, Longlong>})"};

        size_t task_size = tasks.size();
        for (size_t i = 0; i < task_size; ++i)
        {
            ASSERT_DAGREQUEST_EQAUL(expected_strings[i], tasks[i].dag_request);
        }
    }
}
CATCH

TEST_F(ComputeServerRunner, runJoinTasks)
try
{
    startServers(3);
    {
        auto expected_cols = {
            toNullableVec<String>({{}, "banana", "banana"}),
            toNullableVec<String>({{}, "apple", "banana"}),
            toNullableVec<String>({{}, "banana", "banana"}),
            toNullableVec<String>({{}, "apple", "banana"})};

        std::vector<String> expected_strings = {
            R"(exchange_sender_5 | type:Hash, {<0, String>, <1, String>}
 table_scan_1 | {<0, String>, <1, String>})",
            R"(exchange_sender_5 | type:Hash, {<0, String>, <1, String>}
 table_scan_1 | {<0, String>, <1, String>})",
            R"(exchange_sender_5 | type:Hash, {<0, String>, <1, String>}
 table_scan_1 | {<0, String>, <1, String>})",
            R"(exchange_sender_4 | type:Hash, {<0, String>, <1, String>}
 table_scan_0 | {<0, String>, <1, String>})",
            R"(exchange_sender_4 | type:Hash, {<0, String>, <1, String>}
 table_scan_0 | {<0, String>, <1, String>})",
            R"(exchange_sender_4 | type:Hash, {<0, String>, <1, String>}
 table_scan_0 | {<0, String>, <1, String>})",
            R"(exchange_sender_3 | type:PassThrough, {<0, String>, <1, String>, <2, String>, <3, String>}
 Join_2 | LeftOuterJoin, HashJoin. left_join_keys: {<0, String>}, right_join_keys: {<0, String>}
  exchange_receiver_6 | type:PassThrough, {<0, String>, <1, String>}
  exchange_receiver_7 | type:PassThrough, {<0, String>, <1, String>})",
            R"(exchange_sender_3 | type:PassThrough, {<0, String>, <1, String>, <2, String>, <3, String>}
 Join_2 | LeftOuterJoin, HashJoin. left_join_keys: {<0, String>}, right_join_keys: {<0, String>}
  exchange_receiver_6 | type:PassThrough, {<0, String>, <1, String>}
  exchange_receiver_7 | type:PassThrough, {<0, String>, <1, String>})",
            R"(exchange_sender_3 | type:PassThrough, {<0, String>, <1, String>, <2, String>, <3, String>}
 Join_2 | LeftOuterJoin, HashJoin. left_join_keys: {<0, String>}, right_join_keys: {<0, String>}
  exchange_receiver_6 | type:PassThrough, {<0, String>, <1, String>}
  exchange_receiver_7 | type:PassThrough, {<0, String>, <1, String>})"};

        ASSERT_MPPTASK_EQUAL_PLAN_AND_RESULT(context
                                                 .scan("test_db", "l_table")
                                                 .join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")}),
                                             expected_strings,
                                             expect_cols);
    }

    {
        auto properties = getDAGPropertiesForTest(1);
        auto tasks = context
                         .scan("test_db", "l_table")
                         .join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")})
                         .buildMPPTasks(context, properties);

        std::vector<String> expected_strings = {
            R"(exchange_sender_5 | type:Hash, {<0, String>, <1, String>}
 table_scan_1 | {<0, String>, <1, String>})",
            R"(exchange_sender_4 | type:Hash, {<0, String>, <1, String>}
 table_scan_0 | {<0, String>, <1, String>})",
            R"(exchange_sender_3 | type:PassThrough, {<0, String>, <1, String>, <2, String>, <3, String>}
 Join_2 | LeftOuterJoin, HashJoin. left_join_keys: {<0, String>}, right_join_keys: {<0, String>}
  exchange_receiver_6 | type:PassThrough, {<0, String>, <1, String>}
  exchange_receiver_7 | type:PassThrough, {<0, String>, <1, String>})"};

        size_t task_size = tasks.size();
        for (size_t i = 0; i < task_size; ++i)
        {
            ASSERT_DAGREQUEST_EQAUL(expected_strings[i], tasks[i].dag_request);
        }
    }
}
CATCH

TEST_F(ComputeServerRunner, runJoinThenAggTasks)
try
{
    startServers(3);
    {
        std::vector<String> expected_strings = {
            R"(exchange_sender_10 | type:Hash, {<0, String>}
 table_scan_1 | {<0, String>})",
            R"(exchange_sender_10 | type:Hash, {<0, String>}
 table_scan_1 | {<0, String>})",
            R"(exchange_sender_10 | type:Hash, {<0, String>}
 table_scan_1 | {<0, String>})",
            R"(exchange_sender_9 | type:Hash, {<0, String>, <1, String>}
 table_scan_0 | {<0, String>, <1, String>})",
            R"(exchange_sender_9 | type:Hash, {<0, String>, <1, String>}
 table_scan_0 | {<0, String>, <1, String>})",
            R"(exchange_sender_9 | type:Hash, {<0, String>, <1, String>}
 table_scan_0 | {<0, String>, <1, String>})",
            R"(exchange_sender_7 | type:Hash, {<0, String>, <1, String>}
 aggregation_6 | group_by: {<0, String>}, agg_func: {max(<0, String>)}
  Join_2 | LeftOuterJoin, HashJoin. left_join_keys: {<0, String>}, right_join_keys: {<0, String>}
   exchange_receiver_11 | type:PassThrough, {<0, String>, <1, String>}
   exchange_receiver_12 | type:PassThrough, {<0, String>})",
            R"(exchange_sender_7 | type:Hash, {<0, String>, <1, String>}
 aggregation_6 | group_by: {<0, String>}, agg_func: {max(<0, String>)}
  Join_2 | LeftOuterJoin, HashJoin. left_join_keys: {<0, String>}, right_join_keys: {<0, String>}
   exchange_receiver_11 | type:PassThrough, {<0, String>, <1, String>}
   exchange_receiver_12 | type:PassThrough, {<0, String>})",
            R"(exchange_sender_7 | type:Hash, {<0, String>, <1, String>}
 aggregation_6 | group_by: {<0, String>}, agg_func: {max(<0, String>)}
  Join_2 | LeftOuterJoin, HashJoin. left_join_keys: {<0, String>}, right_join_keys: {<0, String>}
   exchange_receiver_11 | type:PassThrough, {<0, String>, <1, String>}
   exchange_receiver_12 | type:PassThrough, {<0, String>})",
            R"(exchange_sender_5 | type:PassThrough, {<0, String>, <1, String>}
 project_4 | {<0, String>, <1, String>}
  aggregation_3 | group_by: {<1, String>}, agg_func: {max(<0, String>)}
   exchange_receiver_8 | type:PassThrough, {<0, String>, <1, String>})",
            R"(exchange_sender_5 | type:PassThrough, {<0, String>, <1, String>}
 project_4 | {<0, String>, <1, String>}
  aggregation_3 | group_by: {<1, String>}, agg_func: {max(<0, String>)}
   exchange_receiver_8 | type:PassThrough, {<0, String>, <1, String>})",
            R"(exchange_sender_5 | type:PassThrough, {<0, String>, <1, String>}
 project_4 | {<0, String>, <1, String>}
  aggregation_3 | group_by: {<1, String>}, agg_func: {max(<0, String>)}
   exchange_receiver_8 | type:PassThrough, {<0, String>, <1, String>})"};

        auto expected_cols = {
            toNullableVec<String>({{}, "banana"}),
            toNullableVec<String>({{}, "banana"})};

        ASSERT_MPPTASK_EQUAL_PLAN_AND_RESULT(
            context
                .scan("test_db", "l_table")
                .join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")})
                .aggregation({Max(col("l_table.s"))}, {col("l_table.s")})
                .project({col("max(l_table.s)"), col("l_table.s")}),
            expected_strings,
            expect_cols);
    }

    {
        auto properties = getDAGPropertiesForTest(1);
        auto tasks = context
                         .scan("test_db", "l_table")
                         .join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")})
                         .aggregation({Max(col("l_table.s"))}, {col("l_table.s")})
                         .project({col("max(l_table.s)"), col("l_table.s")})
                         .buildMPPTasks(context, properties);

        std::vector<String> expected_strings = {
            R"(exchange_sender_10 | type:Hash, {<0, String>}
 table_scan_1 | {<0, String>})",
            R"(exchange_sender_9 | type:Hash, {<0, String>, <1, String>}
 table_scan_0 | {<0, String>, <1, String>})",
            R"(exchange_sender_7 | type:Hash, {<0, String>, <1, String>}
 aggregation_6 | group_by: {<0, String>}, agg_func: {max(<0, String>)}
  Join_2 | LeftOuterJoin, HashJoin. left_join_keys: {<0, String>}, right_join_keys: {<0, String>}
   exchange_receiver_11 | type:PassThrough, {<0, String>, <1, String>}
   exchange_receiver_12 | type:PassThrough, {<0, String>})",
            R"(exchange_sender_5 | type:PassThrough, {<0, String>, <1, String>}
 project_4 | {<0, String>, <1, String>}
  aggregation_3 | group_by: {<1, String>}, agg_func: {max(<0, String>)}
   exchange_receiver_8 | type:PassThrough, {<0, String>, <1, String>})",
        };

        size_t task_size = tasks.size();
        for (size_t i = 0; i < task_size; ++i)
        {
            ASSERT_DAGREQUEST_EQAUL(expected_strings[i], tasks[i].dag_request);
        }
    }
}
CATCH

TEST_F(ComputeServerRunner, cancelAggTasks)
try
{
    startServers(4);
    {
<<<<<<< HEAD
        auto [start_ts, res] = prepareMPPStreams(context
                                                     .scan("test_db", "test_table_1")
                                                     .aggregation({Max(col("s1"))}, {col("s2"), col("s3")})
                                                     .project({"max(s1)"}));
        EXPECT_TRUE(assertQueryActive(start_ts));
        MockComputeServerManager::instance().cancelQuery(start_ts);
        EXPECT_TRUE(assertQueryCancelled(start_ts));
    }
=======
        /// case 1, cancel after dispatch MPPTasks
        auto properties = DB::tests::getDAGPropertiesForTest(serverNum());
        MPPQueryId query_id(properties.query_ts, properties.local_query_id, properties.server_id, properties.start_ts);
        auto res = prepareMPPStreams(context
                                         .scan("test_db", "test_table_1")
                                         .aggregation({Max(col("s1"))}, {col("s2"), col("s3")})
                                         .project({"max(s1)"}),
                                     properties);
        EXPECT_TRUE(assertQueryActive(query_id));
        MockComputeServerManager::instance().cancelQuery(query_id);
        EXPECT_TRUE(assertQueryCancelled(query_id));
    }
    {
        /// case 2, cancel before dispatch MPPTasks
        auto properties = DB::tests::getDAGPropertiesForTest(serverNum());
        MPPQueryId query_id(properties.query_ts, properties.local_query_id, properties.server_id, properties.start_ts);
        auto tasks = prepareMPPTasks(context
                                         .scan("test_db", "test_table_1")
                                         .aggregation({Max(col("s1"))}, {col("s2"), col("s3")})
                                         .project({"max(s1)"}),
                                     properties);
        EXPECT_TRUE(!assertQueryActive(query_id));
        MockComputeServerManager::instance().cancelQuery(query_id);
        try
        {
            executeMPPTasks(tasks, properties);
        }
        catch (...)
        {
        }
        EXPECT_TRUE(assertQueryCancelled(query_id));
    }
    WRAP_FOR_SERVER_TEST_END
>>>>>>> 306d6b785e (Fix unstable tests and add more ut (#7613))
}
CATCH

TEST_F(ComputeServerRunner, cancelJoinTasks)
try
{
    startServers(4);
    {
<<<<<<< HEAD
        auto [start_ts, res] = prepareMPPStreams(context
                                                     .scan("test_db", "l_table")
                                                     .join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")}));
        EXPECT_TRUE(assertQueryActive(start_ts));
        MockComputeServerManager::instance().cancelQuery(start_ts);
        EXPECT_TRUE(assertQueryCancelled(start_ts));
=======
        setCancelTest();
        auto properties = DB::tests::getDAGPropertiesForTest(serverNum());
        MPPQueryId query_id(properties.query_ts, properties.local_query_id, properties.server_id, properties.start_ts);
        auto res = prepareMPPStreams(context
                                         .scan("test_db", "l_table")
                                         .join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")}),
                                     properties);
        EXPECT_TRUE(assertQueryActive(query_id));
        MockComputeServerManager::instance().cancelQuery(query_id);
        EXPECT_TRUE(assertQueryCancelled(query_id));
>>>>>>> 306d6b785e (Fix unstable tests and add more ut (#7613))
    }
}
CATCH

TEST_F(ComputeServerRunner, cancelJoinThenAggTasks)
try
{
    startServers(4);
    {
<<<<<<< HEAD
        auto [start_ts, _] = prepareMPPStreams(context
                                                   .scan("test_db", "l_table")
                                                   .join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")})
                                                   .aggregation({Max(col("l_table.s"))}, {col("l_table.s")})
                                                   .project({col("max(l_table.s)"), col("l_table.s")}));
        EXPECT_TRUE(assertQueryActive(start_ts));
        MockComputeServerManager::instance().cancelQuery(start_ts);
        EXPECT_TRUE(assertQueryCancelled(start_ts));
=======
        setCancelTest();
        auto properties = DB::tests::getDAGPropertiesForTest(serverNum());
        MPPQueryId query_id(properties.query_ts, properties.local_query_id, properties.server_id, properties.start_ts);
        auto stream = prepareMPPStreams(context
                                            .scan("test_db", "l_table")
                                            .join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")})
                                            .aggregation({Max(col("l_table.s"))}, {col("l_table.s")})
                                            .project({col("max(l_table.s)"), col("l_table.s")}),
                                        properties);
        EXPECT_TRUE(assertQueryActive(query_id));
        MockComputeServerManager::instance().cancelQuery(query_id);
        EXPECT_TRUE(assertQueryCancelled(query_id));
>>>>>>> 306d6b785e (Fix unstable tests and add more ut (#7613))
    }
}
CATCH

TEST_F(ComputeServerRunner, multipleQuery)
try
{
    startServers(4);
    {
<<<<<<< HEAD
        auto [start_ts1, res1] = prepareMPPStreams(context
                                                       .scan("test_db", "l_table")
                                                       .join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")}));
        auto [start_ts2, res2] = prepareMPPStreams(context
                                                       .scan("test_db", "l_table")
                                                       .join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")})
                                                       .aggregation({Max(col("l_table.s"))}, {col("l_table.s")})
                                                       .project({col("max(l_table.s)"), col("l_table.s")}));
=======
        auto properties1 = DB::tests::getDAGPropertiesForTest(serverNum());
        MPPQueryId query_id1(properties1.query_ts, properties1.local_query_id, properties1.server_id, properties1.start_ts);
        auto res1 = prepareMPPStreams(context
                                          .scan("test_db", "l_table")
                                          .join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")}),
                                      properties1);
        auto properties2 = DB::tests::getDAGPropertiesForTest(serverNum());
        MPPQueryId query_id2(properties2.query_ts, properties2.local_query_id, properties2.server_id, properties2.start_ts);
        auto res2 = prepareMPPStreams(context
                                          .scan("test_db", "l_table")
                                          .join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")})
                                          .aggregation({Max(col("l_table.s"))}, {col("l_table.s")})
                                          .project({col("max(l_table.s)"), col("l_table.s")}),
                                      properties2);
>>>>>>> 306d6b785e (Fix unstable tests and add more ut (#7613))

        EXPECT_TRUE(assertQueryActive(start_ts1));
        MockComputeServerManager::instance().cancelQuery(start_ts1);
        EXPECT_TRUE(assertQueryCancelled(start_ts1));

        EXPECT_TRUE(assertQueryActive(start_ts2));
        MockComputeServerManager::instance().cancelQuery(start_ts2);
        EXPECT_TRUE(assertQueryCancelled(start_ts2));
    }

    // start 10 queries
    {
<<<<<<< HEAD
        std::vector<std::tuple<size_t, std::vector<BlockInputStreamPtr>>> queries;
        for (size_t i = 0; i < 10; ++i)
        {
            queries.push_back(prepareMPPStreams(context
                                                    .scan("test_db", "l_table")
                                                    .join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")})));
=======
        std::vector<std::tuple<MPPQueryId, BlockInputStreamPtr>> queries;
        for (size_t i = 0; i < 10; ++i)
        {
            auto properties = DB::tests::getDAGPropertiesForTest(serverNum());
            MPPQueryId query_id(properties.query_ts, properties.local_query_id, properties.server_id, properties.start_ts);
            queries.push_back(std::make_tuple(query_id, prepareMPPStreams(context.scan("test_db", "l_table").join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")}), properties)));
>>>>>>> 306d6b785e (Fix unstable tests and add more ut (#7613))
        }
        for (size_t i = 0; i < 10; ++i)
        {
            auto start_ts = std::get<0>(queries[i]);
            EXPECT_TRUE(assertQueryActive(start_ts));
            MockComputeServerManager::instance().cancelQuery(start_ts);
            EXPECT_TRUE(assertQueryCancelled(start_ts));
        }
    }
}
CATCH

TEST_F(ComputeServerRunner, runCoprocessor)
try
{
    // In coprocessor test, we only need to start 1 server.
    startServers(1);
    {
        auto request = context
                           .scan("test_db", "l_table")
                           .build(context);

        auto expected_cols = {
            toNullableVec<String>({{"banana", {}, "banana"}}),
            toNullableVec<String>({{"apple", {}, "banana"}})};
        ASSERT_COLUMNS_EQ_UR(expected_cols, executeCoprocessorTask(request));
    }
}
CATCH
<<<<<<< HEAD
=======

TEST_F(ComputeServerRunner, runFineGrainedShuffleJoinTest)
try
{
    WRAP_FOR_SERVER_TEST_BEGIN
    startServers(3);
    constexpr size_t join_type_num = 7;
    constexpr tipb::JoinType join_types[join_type_num] = {
        tipb::JoinType::TypeInnerJoin,
        tipb::JoinType::TypeLeftOuterJoin,
        tipb::JoinType::TypeRightOuterJoin,
        tipb::JoinType::TypeSemiJoin,
        tipb::JoinType::TypeAntiSemiJoin,
        tipb::JoinType::TypeLeftOuterSemiJoin,
        tipb::JoinType::TypeAntiLeftOuterSemiJoin,
    };
    // fine-grained shuffle is enabled.
    constexpr uint64_t enable = 8;
    constexpr uint64_t disable = 0;

    for (auto join_type : join_types)
    {
        auto properties = DB::tests::getDAGPropertiesForTest(serverNum());
        auto request = context
                           .scan("test_db", "l_table_2")
                           .join(context.scan("test_db", "r_table_2"), join_type, {col("s1"), col("s2")}, disable)
                           .project({col("l_table_2.s1"), col("l_table_2.s2"), col("l_table_2.s3")});
        const auto expected_cols = buildAndExecuteMPPTasks(request);

        auto request2 = context
                            .scan("test_db", "l_table_2")
                            .join(context.scan("test_db", "r_table_2"), join_type, {col("s1"), col("s2")}, enable)
                            .project({col("l_table_2.s1"), col("l_table_2.s2"), col("l_table_2.s3")});
        auto tasks = request2.buildMPPTasks(context, properties);
        const auto actual_cols = executeMPPTasks(tasks, properties);
        ASSERT_COLUMNS_EQ_UR(expected_cols, actual_cols);
    }
    WRAP_FOR_SERVER_TEST_END
}
CATCH

TEST_F(ComputeServerRunner, runFineGrainedShuffleAggTest)
try
{
    WRAP_FOR_SERVER_TEST_BEGIN
    startServers(3);
    // fine-grained shuffle is enabled.
    constexpr uint64_t enable = 8;
    constexpr uint64_t disable = 0;
    {
        auto properties = DB::tests::getDAGPropertiesForTest(serverNum());
        auto request = context
                           .scan("test_db", "test_table_2")
                           .aggregation({Max(col("s3"))}, {col("s1"), col("s2")}, disable);
        const auto expected_cols = buildAndExecuteMPPTasks(request);

        auto request2 = context
                            .scan("test_db", "test_table_2")
                            .aggregation({Max(col("s3"))}, {col("s1"), col("s2")}, enable);
        auto tasks = request2.buildMPPTasks(context, properties);
        const auto actual_cols = executeMPPTasks(tasks, properties);
        ASSERT_COLUMNS_EQ_UR(expected_cols, actual_cols);
    }
    WRAP_FOR_SERVER_TEST_END
}
CATCH

TEST_F(ComputeServerRunner, randomFailpointForPipeline)
try
{
    enablePipeline(true);
    startServers(3);
    std::vector<String> failpoints{
        "random_pipeline_model_task_run_failpoint-0.8",
        "random_pipeline_model_task_construct_failpoint-1.0",
        "random_pipeline_model_event_schedule_failpoint-1.0",
        // Because the mock table scan will always output data, there will be no event triggering onEventFinish, so the query will not terminate.
        // "random_pipeline_model_event_finish_failpoint-0.99",
        "random_pipeline_model_operator_run_failpoint-0.8",
        "random_pipeline_model_cancel_failpoint-0.8",
        "random_pipeline_model_execute_prefix_failpoint-1.0",
        "random_pipeline_model_execute_suffix_failpoint-1.0"};
    for (const auto & failpoint : failpoints)
    {
        auto config_str = fmt::format("[flash]\nrandom_fail_points = \"{}\"", failpoint);
        initRandomFailPoint(config_str);
        auto properties = DB::tests::getDAGPropertiesForTest(serverNum());
        MPPQueryId query_id(properties.query_ts, properties.local_query_id, properties.server_id, properties.start_ts);
        try
        {
            BlockInputStreamPtr tmp = prepareMPPStreams(context
                                                            .scan("test_db", "l_table")
                                                            .join(context.scan("test_db", "r_table"), tipb::JoinType::TypeLeftOuterJoin, {col("join_c")})
                                                            .aggregation({Max(col("l_table.s"))}, {col("l_table.s")})
                                                            .project({col("max(l_table.s)"), col("l_table.s")}),
                                                        properties);
        }
        catch (...)
        {
            // Only consider whether a crash occurs
            ::DB::tryLogCurrentException(__PRETTY_FUNCTION__);
        }
        // Check if the query is stuck
        EXPECT_TRUE(assertQueryCancelled(query_id)) << "fail in " << failpoint;
        disableRandomFailPoint(config_str);
    }
}
CATCH

TEST_F(ComputeServerRunner, testErrorMessage)
try
{
    startServers(3);
    setCancelTest();
    std::vector<String> failpoint_names{
        FailPoints::exception_before_mpp_register_non_root_mpp_task,
        FailPoints::exception_before_mpp_register_root_mpp_task,
        FailPoints::exception_before_mpp_register_tunnel_for_non_root_mpp_task,
        FailPoints::exception_before_mpp_register_tunnel_for_root_mpp_task,
        FailPoints::exception_during_mpp_register_tunnel_for_non_root_mpp_task,
        FailPoints::exception_before_mpp_non_root_task_run,
        FailPoints::exception_before_mpp_root_task_run,
        FailPoints::exception_during_mpp_non_root_task_run,
        FailPoints::exception_during_mpp_root_task_run,
    };
    size_t query_index = 0;
    for (const auto & failpoint : failpoint_names)
    {
        query_index++;
        for (size_t i = 0; i < 5; ++i)
        {
            auto properties = DB::tests::getDAGPropertiesForTest(serverNum(), query_index, i);
            MPPQueryId query_id(properties.query_ts, properties.local_query_id, properties.server_id, properties.start_ts);
            /// currently all the failpoints are automatically disabled after triggered once, so have to enable it before every run
            FailPointHelper::enableFailPoint(failpoint);
            try
            {
                auto tasks = prepareMPPTasks(context
                                                 .scan("test_db", "l_table")
                                                 .aggregation({Max(col("l_table.s"))}, {col("l_table.s")})
                                                 .project({col("max(l_table.s)"), col("l_table.s")}),
                                             properties);
                executeMPPTasks(tasks, properties);
            }
            catch (...)
            {
                auto error_message = getCurrentExceptionMessage(false);
                ASSERT_TRUE(error_message.find(failpoint) != std::string::npos) << " error message is " << error_message << " failpoint is " << failpoint;
                MockComputeServerManager::instance().cancelQuery(query_id);
                EXPECT_TRUE(assertQueryCancelled(query_id)) << "fail in " << failpoint;
                FailPointHelper::disableFailPoint(failpoint);
                continue;
            }
            GTEST_FAIL();
        }
    }
}
CATCH

#undef WRAP_FOR_SERVER_TEST_BEGIN
#undef WRAP_FOR_SERVER_TEST_END

>>>>>>> 306d6b785e (Fix unstable tests and add more ut (#7613))
} // namespace tests
} // namespace DB
