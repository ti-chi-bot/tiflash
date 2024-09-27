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

#include <Debug/MockSchemaNameMapper.h>
#include <Parsers/ASTCreateQuery.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/ParserCreateQuery.h>
#include <Parsers/parseQuery.h>
#include <Poco/Logger.h>
#include <Storages/KVStore/StorageEngineType.h>
#include <Storages/registerStorages.h>
#include <TestUtils/TiFlashTestBasic.h>
#include <TiDB/Decode/TypeMapping.h>
#include <TiDB/Schema/SchemaSyncer.h>
#include <TiDB/Schema/TiDB.h>


using TableInfo = TiDB::TableInfo;
using DBInfo = TiDB::DBInfo;


namespace DB
{

String createTableStmt(
    KeyspaceID keyspace_id,
    DatabaseID database_id,
    const TableInfo & table_info,
    const SchemaNameMapper & name_mapper,
    UInt64 tombstone,
    const LoggerPtr & log);

namespace tests
{

struct ParseCase
{
    String table_info_json;
    std::function<void(const TableInfo & table_info)> check;
};

TEST(TiDBTableInfoTest, ParseFromJSON)
try
{
    auto cases
        = {
            // Test for backward compatibility
            ParseCase{
                R"json({"cols":[{"comment":"","default":null,"default_bit":null,"id":1,"name":{"L":"t","O":"t"},"offset":0,"origin_default":null,"state":5,"type":{"Charset":"binary","Collate":"binary","Decimal":0,"Elems":null,"Flag":0,"Flen":11,"Tp":3}}],"comment":"","id":45,"name":{"L":"t","O":"t"},"partition":null,"pk_is_handle":false,"schema_version":23,"state":5,"update_timestamp":418700409204899851})json",
                [](const TableInfo & table_info) {
                    ASSERT_EQ(table_info.name, "t");
                    ASSERT_EQ(table_info.id, 45L);
                }},
            // Test with tiflash_replica information
            ParseCase{
                R"json({"id":45,"name":{"O":"t","L":"t"},"charset":"utf8mb4","collate":"utf8mb4_bin","cols":[{"id":1,"name":{"O":"t","L":"t"},"offset":0,"origin_default":null,"origin_default_bit":null,"default":null,"default_bit":null,"default_is_expr":false,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":3,"Flag":0,"Flen":11,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null},"state":5,"comment":"","hidden":false,"change_state_info":null,"version":2}],"index_info":null,"constraint_info":null,"fk_info":null,"state":5,"pk_is_handle":false,"is_common_handle":false,"comment":"","auto_inc_id":0,"auto_id_cache":0,"auto_rand_id":0,"max_col_id":1,"max_idx_id":0,"max_cst_id":0,"update_timestamp":418683341902184450,"ShardRowIDBits":0,"max_shard_row_id_bits":0,"auto_random_bits":0,"pre_split_regions":0,"partition":null,"compression":"","view":null,"sequence":null,"Lock":null,"version":3,"tiflash_replica":{"Count":1,"LocationLabels":[],"Available":false,"AvailablePartitionIDs":null}})json",
                [](const TableInfo & table_info) {
                    ASSERT_EQ(table_info.name, "t");
                    ASSERT_EQ(table_info.id, 45L);
                }},
            // Test binary default value not trimmed by leading zero bytes and padded with trailing zero bytes.
            ParseCase{
                R"json({"id":45,"name":{"O":"t","L":"t"},"charset":"utf8mb4","collate":"utf8mb4_bin","cols":[{"id":1,"name":{"O":"t","L":"t"},"offset":0,"origin_default":"\u0000\u00124","origin_default_bit":null,"default":null,"default_bit":null,"default_is_expr":false,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":254,"Flag":129,"Flen":4,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null},"state":5,"comment":"","hidden":false,"change_state_info":null,"version":2}],"index_info":null,"constraint_info":null,"fk_info":null,"state":5,"pk_is_handle":false,"is_common_handle":false,"comment":"","auto_inc_id":0,"auto_id_cache":0,"auto_rand_id":0,"max_col_id":1,"max_idx_id":0,"max_cst_id":0,"update_timestamp":418683341902184450,"ShardRowIDBits":0,"max_shard_row_id_bits":0,"auto_random_bits":0,"pre_split_regions":0,"partition":null,"compression":"","view":null,"sequence":null,"Lock":null,"version":3,"tiflash_replica":{"Count":1,"LocationLabels":[],"Available":false,"AvailablePartitionIDs":null}})json",
                [](const TableInfo & table_info) {
                    ASSERT_EQ(
                        table_info.columns[0].defaultValueToField().get<String>(),
                        Field(String(
                                  "\0\x12"
                                  "4\0",
                                  4))
                            .get<String>());
                }},
            // Test binary default value with exact length having the full content.
            ParseCase{
                R"json({"id":45,"name":{"O":"t","L":"t"},"charset":"utf8mb4","collate":"utf8mb4_bin","cols":[{"id":1,"name":{"O":"t","L":"t"},"offset":0,"origin_default":"\u0000\u00124","origin_default_bit":null,"default":null,"default_bit":null,"default_is_expr":false,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":254,"Flag":129,"Flen":3,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null},"state":5,"comment":"","hidden":false,"change_state_info":null,"version":2}],"index_info":null,"constraint_info":null,"fk_info":null,"state":5,"pk_is_handle":false,"is_common_handle":false,"comment":"","auto_inc_id":0,"auto_id_cache":0,"auto_rand_id":0,"max_col_id":1,"max_idx_id":0,"max_cst_id":0,"update_timestamp":418683341902184450,"ShardRowIDBits":0,"max_shard_row_id_bits":0,"auto_random_bits":0,"pre_split_regions":0,"partition":null,"compression":"","view":null,"sequence":null,"Lock":null,"version":3,"tiflash_replica":{"Count":1,"LocationLabels":[],"Available":false,"AvailablePartitionIDs":null}})json",
                [](const TableInfo & table_info) {
                    ASSERT_EQ(
                        table_info.columns[0].defaultValueToField().get<String>(),
                        Field(String(
                                  "\0\x12"
                                  "4",
                                  3))
                            .get<String>());
                }},
            ParseCase{
                R"json({"cols":[{"comment":"","default":null,"default_bit":null,"id":1,"name":{"L":"column_1","O":"column_1"},"offset":0,"origin_default":null,"state":5,"type":{"Charset":"utf8mb4","Collate":"utf8mb4_bin","Decimal":0,"Elems":null,"Flag":0,"Flen":512,"Tp":15}},{"comment":"","default":null,"default_bit":null,"id":2,"name":{"L":"column_2","O":"column_2"},"offset":1,"origin_default":null,"state":5,"type":{"Charset":"utf8mb4","Collate":"utf8mb4_bin","Decimal":0,"Elems":null,"Flag":0,"Flen":512,"Tp":15}},{"comment":"","default":null,"default_bit":null,"id":3,"name":{"L":"column_3","O":"column_3"},"offset":2,"origin_default":null,"state":5,"type":{"Charset":"utf8mb4","Collate":"utf8mb4_bin","Decimal":0,"Elems":null,"Flag":0,"Flen":512,"Tp":15}},{"comment":"","default":null,"default_bit":null,"id":4,"name":{"L":"column_4","O":"column_4"},"offset":3,"origin_default":null,"state":5,"type":{"Charset":"utf8mb4","Collate":"utf8mb4_bin","Decimal":0,"Elems":null,"Flag":0,"Flen":512,"Tp":15}},{"comment":"","default":null,"default_bit":null,"id":5,"name":{"L":"column_5","O":"column_5"},"offset":4,"origin_default":null,"state":5,"type":{"Charset":"utf8mb4","Collate":"utf8mb4_bin","Decimal":0,"Elems":null,"Flag":0,"Flen":512,"Tp":15}},{"comment":"","default":null,"default_bit":null,"id":6,"name":{"L":"column_6","O":"column_6"},"offset":5,"origin_default":null,"state":5,"type":{"Charset":"utf8mb4","Collate":"utf8mb4_bin","Decimal":0,"Elems":null,"Flag":0,"Flen":512,"Tp":15}},{"comment":"","default":null,"default_bit":null,"id":7,"name":{"L":"column_7","O":"column_7"},"offset":6,"origin_default":null,"state":5,"type":{"Charset":"utf8mb4","Collate":"utf8mb4_bin","Decimal":0,"Elems":null,"Flag":0,"Flen":512,"Tp":15}},{"comment":"","default":null,"default_bit":null,"id":8,"name":{"L":"column_8","O":"column_8"},"offset":7,"origin_default":null,"state":5,"type":{"Charset":"utf8mb4","Collate":"utf8mb4_bin","Decimal":0,"Elems":null,"Flag":0,"Flen":512,"Tp":15}}],"comment":"","id":86,"index_info":[],"is_common_handle":false,"keyspace_id":6367,"name":{"L":"test_local1_table","O":"test_local1_table"},"partition":null,"pk_is_handle":false,"schema_version":83,"state":5,"tiflash_replica":{"Count":0},"update_timestamp":442666801340350471})json",
                [](const TableInfo & table_info) {
                    ASSERT_EQ(table_info.getColumnID("column_1"), 1);
                    ASSERT_EQ(table_info.getColumnID("column_2"), 2);
                    ASSERT_EQ(table_info.getColumnID("column_3"), 3);
                    ASSERT_EQ(table_info.getColumnID("column_4"), 4);
                    ASSERT_EQ(table_info.getColumnID("column_5"), 5);
                    ASSERT_EQ(table_info.getColumnID("column_6"), 6);
                    ASSERT_EQ(table_info.getColumnID("column_7"), 7);
                    ASSERT_EQ(table_info.getColumnID("column_8"), 8);
                }},
            ParseCase{
                R"json({"cols": [{"comment": "","default": null,"default_bit": null,"id": 1,"name": {"L": "ol_o_id","O": "ol_o_id"},"offset": 0,"origin_default": null,"state": 5,"type": {"Charset": "binary","Collate": "binary","Decimal": 0,"Elems": null,"Flag": 4099,"Flen": 11,"Tp": 3}}, {"comment": "","default": null,"default_bit": null,"id": 2,"name": {"L": "ol_d_id","O": "ol_d_id"},"offset": 1,"origin_default": null,"state": 5,"type": {"Charset": "binary","Collate": "binary","Decimal": 0,"Elems": null,"Flag": 4099,"Flen": 11,"Tp": 3}}, {"comment": "","default": null,"default_bit": null,"id": 3,"name": {"L": "ol_w_id","O": "ol_w_id"},"offset": 2,"origin_default": null,"state": 5,"type": {"Charset": "binary","Collate": "binary","Decimal": 0,"Elems": null,"Flag": 4099,"Flen": 11,"Tp": 3}}, {"comment": "","default": null,"default_bit": null,"id": 4,"name": {"L": "ol_number","O": "ol_number"},"offset": 3,"origin_default": null,"state": 5,"type": {"Charset": "binary","Collate": "binary","Decimal": 0,"Elems": null,"Flag": 4099,"Flen": 11,"Tp": 3}}, {"comment": "","default": null,"default_bit": null,"id": 5,"name": {"L": "ol_i_id","O": "ol_i_id"},"offset": 4,"origin_default": null,"state": 5,"type": {"Charset": "binary","Collate": "binary","Decimal": 0,"Elems": null,"Flag": 4097,"Flen": 11,"Tp": 3}}, {"comment": "","default": null,"default_bit": null,"id": 6,"name": {"L": "ol_supply_w_id","O": "ol_supply_w_id"},"offset": 5,"origin_default": null,"state": 5,"type": {"Charset": "binary","Collate": "binary","Decimal": 0,"Elems": null,"Flag": 0,"Flen": 11,"Tp": 3}}, {"comment": "","default": null,"default_bit": null,"id": 7,"name": {"L": "ol_delivery_d","O": "ol_delivery_d"},"offset": 6,"origin_default": null,"state": 5,"type": {"Charset": "binary","Collate": "binary","Decimal": 0,"Elems": null,"Flag": 128,"Flen": 19,"Tp": 12}}, {"comment": "","default": null,"default_bit": null,"id": 8,"name": {"L": "ol_quantity","O": "ol_quantity"},"offset": 7,"origin_default": null,"state": 5,"type": {"Charset": "binary","Collate": "binary","Decimal": 0,"Elems": null,"Flag": 0,"Flen": 11,"Tp": 3}}, {"comment": "","default": null,"default_bit": null,"id": 9,"name": {"L": "ol_amount","O": "ol_amount"},"offset": 8,"origin_default": null,"state": 5,"type": {"Charset": "binary","Collate": "binary","Decimal": 2,"Elems": null,"Flag": 0,"Flen": 6,"Tp": 246}}, {"comment": "","default": null,"default_bit": null,"id": 10,"name": {"L": "ol_dist_info","O": "ol_dist_info"},"offset": 9,"origin_default": null,"state": 5,"type": {"Charset": "utf8mb4","Collate": "utf8mb4_bin","Decimal": 0,"Elems": null,"Flag": 0,"Flen": 24,"Tp": 254}}],"comment": "","id": 122,"index_info": [],"is_common_handle": false,"keyspace_id": 9936,"name": {"L": "order_line","O": "order_line"},"partition": null,"pk_is_handle": false,"schema_version": -1,"state": 5,"tiflash_replica": {"Available": true,"Count": 2},"update_timestamp": 443420630548480022})json",
                [](const TableInfo & table_info) {
                    for (const auto & ci : table_info.columns)
                    {
                        getDataTypeByColumnInfo(ci);
                    }
                }},
            ParseCase{
                R"json({"cols": [{"comment": "","default": null,"default_bit": null,"id": 1,"name": {"L": "help_topic_id","O": "help_topic_id"},"offset": 0,"origin_default": null,"state": 5,"type": {"Charset": "binary","Collate": "binary","Decimal": 0,"Elems": null,"Flag": 4131,"Flen": 10,"Tp": 3}}, {"comment": "","default": null,"default_bit": null,"id": 2,"name": {"L": "name","O": "name"},"offset": 1,"origin_default": null,"state": 5,"type": {"Charset": "utf8","Collate": "utf8_bin","Decimal": 0,"Elems": null,"Flag": 4101,"Flen": 64,"Tp": 254}}, {"comment": "","default": null,"default_bit": null,"id": 3,"name": {"L": "help_category_id","O": "help_category_id"},"offset": 2,"origin_default": null,"state": 5,"type": {"Charset": "binary","Collate": "binary","Decimal": 0,"Elems": null,"Flag": 4129,"Flen": 5,"Tp": 2}}, {"comment": "","default": null,"default_bit": null,"id": 4,"name": {"L": "description","O": "description"},"offset": 3,"origin_default": null,"state": 5,"type": {"Charset": "utf8","Collate": "utf8_bin","Decimal": 0,"Elems": null,"Flag": 4097,"Flen": 65535,"Tp": 252}}, {"comment": "","default": null,"default_bit": null,"id": 5,"name": {"L": "example","O": "example"},"offset": 4,"origin_default": null,"state": 5,"type": {"Charset": "utf8","Collate": "utf8_bin","Decimal": 0,"Elems": null,"Flag": 4097,"Flen": 65535,"Tp": 252}}, {"comment": "","default": null,"default_bit": null,"id": 6,"name": {"L": "url","O": "url"},"offset": 5,"origin_default": null,"state": 5,"type": {"Charset": "utf8","Collate": "utf8_bin","Decimal": 0,"Elems": null,"Flag": 4097,"Flen": 65535,"Tp": 252}}],"comment": "help topics","id": 20,"index_info": [],"is_common_handle": false,"keyspace_id": 9936,"name": {"L": "help_topic","O": "help_topic"},"partition": null,"pk_is_handle": true,"schema_version": -1,"state": 5,"tiflash_replica": {"Count": 0},"update_timestamp": 443411710574854188})json",
                [](const TableInfo & table_info) {
                    for (const auto & ci : table_info.columns)
                    {
                        getDataTypeByColumnInfo(ci);
                    }
                }},
    };

    for (const auto & c : cases)
    {
        TableInfo table_info(c.table_info_json, NullspaceID);
        c.check(table_info);
    }
}
CATCH

struct StmtCase
{
    TableID table_or_partition_id;
    UInt64 tombstone;
    String db_info_json;
    String table_info_json;
    String create_stmt_dm;

    void verifyTableInfo() const
    {
        DBInfo db_info(db_info_json, NullspaceID);
        TableInfo table_info(table_info_json, NullspaceID);
        if (table_info.is_partition_table)
            table_info = *table_info.producePartitionTableInfo(table_or_partition_id, MockSchemaNameMapper());
        auto json1 = table_info.serialize();
        TableInfo table_info2(json1, NullspaceID);
        auto json2 = table_info2.serialize();
        EXPECT_EQ(json1, json2) << "Table info unescaped serde mismatch:\n" + json1 + "\n" + json2;

        // generate create statement with db_info and table_info
        table_info.engine_type = TiDB::StorageEngine::DT;
        String stmt = createTableStmt(
            db_info.keyspace_id,
            db_info.id,
            table_info,
            MockSchemaNameMapper(),
            tombstone,
            Logger::get());
        EXPECT_EQ(stmt, create_stmt_dm) << "Table info create statement mismatch:\n" + stmt + "\n" + create_stmt_dm;

        json1 = extractTableInfoFromCreateStatement(stmt, table_info.name);
        table_info.deserialize(json1);
        json2 = table_info.serialize();
        EXPECT_EQ(json1, json2) << "Table info escaped serde mismatch:\n" + json1 + "\n" + json2;
    }

private:
    static String extractTableInfoFromCreateStatement(const String & stmt, const String & tbl_name)
    {
        ParserCreateQuery parser;
        ASTPtr ast = parseQuery(parser, stmt.data(), stmt.data() + stmt.size(), "from verifyTableInfo " + tbl_name, 0);
        ASTCreateQuery & ast_create_query = typeid_cast<ASTCreateQuery &>(*ast);
        auto & ast_arguments = typeid_cast<ASTExpressionList &>(*(ast_create_query.storage->engine->arguments));
        ASTLiteral & ast_literal = typeid_cast<ASTLiteral &>(*(ast_arguments.children[1]));
        return safeGet<String>(ast_literal.value);
    }
};

TEST(TiDBTableInfoTest, GenCreateTableStatement)
try
{
    // clang-format off
    auto cases = {
        StmtCase{
            1145, //
            0,
            R"json({"id":1939,"db_name":{"O":"customer","L":"customer"},"charset":"utf8mb4","collate":"utf8mb4_bin","state":5})json", //
            R"json({"id":1145,"name":{"O":"customerdebt","L":"customerdebt"},"cols":[{"id":1,"name":{"O":"id","L":"id"},"offset":0,"origin_default":null,"default":null,"default_bit":null,"type":{"Tp":8,"Flag":515,"Flen":20,"Decimal":0},"state":5,"comment":"i\"d"}],"state":5,"pk_is_handle":true,"schema_version":-1,"comment":"负债信息","partition":null})json", //
            R"stmt(CREATE TABLE `db_1939`.`t_1145`(`id` Int64) Engine = DeltaMerge((`id`), '{"cols":[{"comment":"i\\"d","default":null,"default_bit":null,"id":1,"name":{"L":"id","O":"id"},"offset":0,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":null,"Collate":null,"Decimal":0,"Elems":null,"Flag":515,"Flen":20,"Tp":8}}],"comment":"负债信息","id":1145,"index_info":[],"is_common_handle":false,"keyspace_id":4294967295,"name":{"L":"customerdebt","O":"customerdebt"},"partition":null,"pk_is_handle":true,"schema_version":-1,"state":5,"tiflash_replica":{"Count":0},"update_timestamp":0}', 0))stmt", //
        },
        StmtCase{
            2049, //
            0,
            R"json({"id":1939,"db_name":{"O":"customer","L":"customer"},"charset":"utf8mb4","collate":"utf8mb4_bin","state":5})json", //
            R"json({"id":2049,"name":{"O":"customerdebt","L":"customerdebt"},"cols":[{"id":1,"name":{"O":"id","L":"id"},"offset":0,"origin_default":null,"default":null,"default_bit":null,"type":{"Tp":8,"Flag":515,"Flen":20,"Decimal":0},"state":5,"comment":"i\"d"}],"state":5,"pk_is_handle":true,"schema_version":-1,"comment":"负债信息","update_timestamp":404545295996944390,"partition":null})json", //
            R"stmt(CREATE TABLE `db_1939`.`t_2049`(`id` Int64) Engine = DeltaMerge((`id`), '{"cols":[{"comment":"i\\"d","default":null,"default_bit":null,"id":1,"name":{"L":"id","O":"id"},"offset":0,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":null,"Collate":null,"Decimal":0,"Elems":null,"Flag":515,"Flen":20,"Tp":8}}],"comment":"负债信息","id":2049,"index_info":[],"is_common_handle":false,"keyspace_id":4294967295,"name":{"L":"customerdebt","O":"customerdebt"},"partition":null,"pk_is_handle":true,"schema_version":-1,"state":5,"tiflash_replica":{"Count":0},"update_timestamp":404545295996944390}', 0))stmt", //
        },
        StmtCase{
            31, //
            0,
            R"json({"id":1,"db_name":{"O":"db1","L":"db1"},"charset":"utf8mb4","collate":"utf8mb4_bin","state":5})json", //
            R"json({"id":31,"name":{"O":"simple_t","L":"simple_t"},"charset":"","collate":"","cols":[{"id":1,"name":{"O":"i","L":"i"},"offset":0,"origin_default":null,"default":null,"default_bit":null,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":3,"Flag":0,"Flen":11,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null},"state":5,"comment":""}],"index_info":null,"fk_info":null,"state":5,"pk_is_handle":false,"schema_version":-1,"comment":"","auto_inc_id":0,"max_col_id":1,"max_idx_id":0,"update_timestamp":404545295996944390,"ShardRowIDBits":0,"partition":null})json", //
            R"stmt(CREATE TABLE `db_1`.`t_31`(`i` Nullable(Int32), `_tidb_rowid` Int64) Engine = DeltaMerge((`_tidb_rowid`), '{"cols":[{"comment":"","default":null,"default_bit":null,"id":1,"name":{"L":"i","O":"i"},"offset":0,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":"binary","Collate":"binary","Decimal":0,"Elems":null,"Flag":0,"Flen":11,"Tp":3}}],"comment":"","id":31,"index_info":[],"is_common_handle":false,"keyspace_id":4294967295,"name":{"L":"simple_t","O":"simple_t"},"partition":null,"pk_is_handle":false,"schema_version":-1,"state":5,"tiflash_replica":{"Count":0},"update_timestamp":404545295996944390}', 0))stmt", //
        },
        StmtCase{
            33, //
            0,
            R"json({"id":2,"db_name":{"O":"db2","L":"db2"},"charset":"utf8mb4","collate":"utf8mb4_bin","state":5})json", //
            R"json({"id":33,"name":{"O":"pk_t","L":"pk_t"},"charset":"","collate":"","cols":[{"id":1,"name":{"O":"i","L":"i"},"offset":0,"origin_default":null,"default":null,"default_bit":null,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":3,"Flag":3,"Flen":11,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null},"state":5,"comment":""}],"index_info":null,"fk_info":null,"state":5,"pk_is_handle":true,"schema_version":-1,"comment":"","auto_inc_id":0,"max_col_id":1,"max_idx_id":0,"update_timestamp":404545312978108418,"ShardRowIDBits":0,"partition":null})json", //
            R"stmt(CREATE TABLE `db_2`.`t_33`(`i` Int32) Engine = DeltaMerge((`i`), '{"cols":[{"comment":"","default":null,"default_bit":null,"id":1,"name":{"L":"i","O":"i"},"offset":0,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":"binary","Collate":"binary","Decimal":0,"Elems":null,"Flag":3,"Flen":11,"Tp":3}}],"comment":"","id":33,"index_info":[],"is_common_handle":false,"keyspace_id":4294967295,"name":{"L":"pk_t","O":"pk_t"},"partition":null,"pk_is_handle":true,"schema_version":-1,"state":5,"tiflash_replica":{"Count":0},"update_timestamp":404545312978108418}', 0))stmt", //
        },
        StmtCase{
            35, //
            0,
            R"json({"id":1,"db_name":{"O":"db1","L":"db1"},"charset":"utf8mb4","collate":"utf8mb4_bin","state":5})json", //
            R"json({"id":35,"name":{"O":"not_null_t","L":"not_null_t"},"charset":"","collate":"","cols":[{"id":1,"name":{"O":"i","L":"i"},"offset":0,"origin_default":null,"default":null,"default_bit":null,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":3,"Flag":4097,"Flen":11,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null},"state":5,"comment":""}],"index_info":null,"fk_info":null,"state":5,"pk_is_handle":false,"schema_version":-1,"comment":"","auto_inc_id":0,"max_col_id":1,"max_idx_id":0,"update_timestamp":404545324922961926,"ShardRowIDBits":0,"partition":null})json", //
            R"stmt(CREATE TABLE `db_1`.`t_35`(`i` Int32, `_tidb_rowid` Int64) Engine = DeltaMerge((`_tidb_rowid`), '{"cols":[{"comment":"","default":null,"default_bit":null,"id":1,"name":{"L":"i","O":"i"},"offset":0,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":"binary","Collate":"binary","Decimal":0,"Elems":null,"Flag":4097,"Flen":11,"Tp":3}}],"comment":"","id":35,"index_info":[],"is_common_handle":false,"keyspace_id":4294967295,"name":{"L":"not_null_t","O":"not_null_t"},"partition":null,"pk_is_handle":false,"schema_version":-1,"state":5,"tiflash_replica":{"Count":0},"update_timestamp":404545324922961926}', 0))stmt", //
        },
        StmtCase{
            37, //
            0,
            R"json({"id":2,"db_name":{"O":"db2","L":"db2"},"charset":"utf8mb4","collate":"utf8mb4_bin","state":5})json",
            R"json({"id":37,"name":{"O":"mytable","L":"mytable"},"charset":"","collate":"","cols":[{"id":1,"name":{"O":"mycol","L":"mycol"},"offset":0,"origin_default":null,"default":null,"default_bit":null,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":15,"Flag":4099,"Flen":256,"Decimal":0,"Charset":"utf8","Collate":"utf8_bin","Elems":null},"state":5,"comment":""}],"index_info":[{"id":1,"idx_name":{"O":"PRIMARY","L":"primary"},"tbl_name":{"O":"","L":""},"idx_cols":[{"name":{"O":"mycol","L":"mycol"},"offset":0,"length":-1}],"is_unique":true,"is_primary":true,"state":5,"comment":"","index_type":1}],"fk_info":null,"state":5,"pk_is_handle":true,"schema_version":-1,"comment":"","auto_inc_id":0,"max_col_id":1,"max_idx_id":1,"update_timestamp":404566455285710853,"ShardRowIDBits":0,"partition":null})json", //
            R"stmt(CREATE TABLE `db_2`.`t_37`(`mycol` String) Engine = DeltaMerge((`mycol`), '{"cols":[{"comment":"","default":null,"default_bit":null,"id":1,"name":{"L":"mycol","O":"mycol"},"offset":0,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":"utf8","Collate":"utf8_bin","Decimal":0,"Elems":null,"Flag":4099,"Flen":256,"Tp":15}}],"comment":"","id":37,"index_info":[],"is_common_handle":false,"keyspace_id":4294967295,"name":{"L":"mytable","O":"mytable"},"partition":null,"pk_is_handle":true,"schema_version":-1,"state":5,"tiflash_replica":{"Count":0},"update_timestamp":404566455285710853}', 0))stmt", //
        },
        StmtCase{
            32, //
            0,
            R"json({"id":1,"db_name":{"O":"test","L":"test"},"charset":"utf8mb4","collate":"utf8mb4_bin","state":5})json", //
            R"json({"id":31,"name":{"O":"range_part_t","L":"range_part_t"},"charset":"utf8mb4","collate":"utf8mb4_bin","cols":[{"id":1,"name":{"O":"i","L":"i"},"offset":0,"origin_default":null,"default":null,"default_bit":null,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":3,"Flag":0,"Flen":11,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null},"state":5,"comment":"","version":0}],"index_info":null,"fk_info":null,"state":5,"pk_is_handle":false,"schema_version":-1,"comment":"","auto_inc_id":0,"max_col_id":1,"max_idx_id":0,"update_timestamp":407445773801488390,"ShardRowIDBits":0,"partition":{"type":1,"expr":"`i`","columns":null,"enable":true,"definitions":[{"id":32,"name":{"O":"p0","L":"p0"},"less_than":["0"]},{"id":33,"name":{"O":"p1","L":"p1"},"less_than":["100"]}],"num":0},"compression":"","version":1})json", //
            R"stmt(CREATE TABLE `db_1`.`t_32`(`i` Nullable(Int32), `_tidb_rowid` Int64) Engine = DeltaMerge((`_tidb_rowid`), '{"belonging_table_id":31,"cols":[{"comment":"","default":null,"default_bit":null,"id":1,"name":{"L":"i","O":"i"},"offset":0,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":"binary","Collate":"binary","Decimal":0,"Elems":null,"Flag":0,"Flen":11,"Tp":3}}],"comment":"","id":32,"index_info":[],"is_common_handle":false,"is_partition_sub_table":true,"keyspace_id":4294967295,"name":{"L":"range_part_t_32","O":"range_part_t_32"},"partition":null,"pk_is_handle":false,"schema_version":-1,"state":5,"tiflash_replica":{"Count":0},"update_timestamp":407445773801488390}', 0))stmt", //
        },
        StmtCase{
            32, //
            1700815239,
            R"json({"id":1,"db_name":{"O":"test","L":"test"},"charset":"utf8mb4","collate":"utf8mb4_bin","state":5})json", //
            R"json({"id":31,"name":{"O":"range_part_t","L":"range_part_t"},"charset":"utf8mb4","collate":"utf8mb4_bin","cols":[{"id":1,"name":{"O":"i","L":"i"},"offset":0,"origin_default":null,"default":null,"default_bit":null,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":3,"Flag":0,"Flen":11,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null},"state":5,"comment":"","version":0}],"index_info":null,"fk_info":null,"state":5,"pk_is_handle":false,"schema_version":-1,"comment":"","auto_inc_id":0,"max_col_id":1,"max_idx_id":0,"update_timestamp":407445773801488390,"ShardRowIDBits":0,"partition":{"type":1,"expr":"`i`","columns":null,"enable":true,"definitions":[{"id":32,"name":{"O":"p0","L":"p0"},"less_than":["0"]},{"id":33,"name":{"O":"p1","L":"p1"},"less_than":["100"]}],"num":0},"compression":"","version":1})json", //
            R"stmt(CREATE TABLE `db_1`.`t_32`(`i` Nullable(Int32), `_tidb_rowid` Int64) Engine = DeltaMerge((`_tidb_rowid`), '{"belonging_table_id":31,"cols":[{"comment":"","default":null,"default_bit":null,"id":1,"name":{"L":"i","O":"i"},"offset":0,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":"binary","Collate":"binary","Decimal":0,"Elems":null,"Flag":0,"Flen":11,"Tp":3}}],"comment":"","id":32,"index_info":[],"is_common_handle":false,"is_partition_sub_table":true,"keyspace_id":4294967295,"name":{"L":"range_part_t_32","O":"range_part_t_32"},"partition":null,"pk_is_handle":false,"schema_version":-1,"state":5,"tiflash_replica":{"Count":0},"update_timestamp":407445773801488390}', 1700815239))stmt", //
        },
        StmtCase{
            546, //
            0,
            R"json({"id":2,"db_name":{"O":"test","L":"test"},"charset":"utf8mb4","collate":"utf8mb4_bin","state":5})json", //
            R"json({"id":546,"name":{"O":"tcfc7825f","L":"tcfc7825f"},"charset":"utf8mb4","collate":"utf8mb4_general_ci","cols":[{"id":1,"name":{"O":"col_86","L":"col_86"},"offset":0,"origin_default":null,"origin_default_bit":null,"default":null,"default_bit":null,"default_is_expr":false,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":252,"Flag":128,"Flen":65535,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null,"ElemsIsBinaryLit":null,"Array":false},"state":5,"comment":"","hidden":false,"change_state_info":null,"version":2},{"id":2,"name":{"O":"col_87","L":"col_87"},"offset":1,"origin_default":null,"origin_default_bit":null,"default":"1994-05-0600:00:00","default_bit":null,"default_is_expr":false,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":12,"Flag":129,"Flen":19,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null,"ElemsIsBinaryLit":null,"Array":false},"state":5,"comment":"","hidden":false,"change_state_info":null,"version":2},{"id":3,"name":{"O":"col_88","L":"col_88"},"offset":2,"origin_default":null,"origin_default_bit":null,"default":null,"default_bit":null,"default_is_expr":false,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":16,"Flag":32,"Flen":42,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null,"ElemsIsBinaryLit":null,"Array":false},"state":5,"comment":"","hidden":false,"change_state_info":null,"version":2},{"id":4,"name":{"O":"col_89","L":"col_89"},"offset":3,"origin_default":null,"origin_default_bit":null,"default":"\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000","default_bit":null,"default_is_expr":false,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":254,"Flag":129,"Flen":21,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null,"ElemsIsBinaryLit":null,"Array":false},"state":5,"comment":"","hidden":false,"change_state_info":null,"version":2},{"id":5,"name":{"O":"col_90","L":"col_90"},"offset":4,"origin_default":null,"origin_default_bit":null,"default":null,"default_bit":null,"default_is_expr":false,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":1,"Flag":4129,"Flen":3,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null,"ElemsIsBinaryLit":null,"Array":false},"state":5,"comment":"","hidden":false,"change_state_info":null,"version":2},{"id":6,"name":{"O":"col_91","L":"col_91"},"offset":5,"origin_default":null,"origin_default_bit":null,"default":"\u0007\u0007","default_bit":"Bwc=","default_is_expr":false,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":16,"Flag":32,"Flen":12,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null,"ElemsIsBinaryLit":null,"Array":false},"state":5,"comment":"","hidden":false,"change_state_info":null,"version":2},{"id":7,"name":{"O":"col_92","L":"col_92"},"offset":6,"origin_default":null,"origin_default_bit":null,"default":"kY~6to6H4ut*QAPrj@\u0026","default_bit":null,"default_is_expr":false,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":15,"Flag":129,"Flen":343,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null,"ElemsIsBinaryLit":null,"Array":false},"state":5,"comment":"","hidden":false,"change_state_info":null,"version":2},{"id":8,"name":{"O":"col_93","L":"col_93"},"offset":7,"origin_default":null,"origin_default_bit":null,"default":null,"default_bit":null,"default_is_expr":false,"generated_expr_string":"","generated_stored":false,"dependences":null,"type":{"Tp":245,"Flag":128,"Flen":4294967295,"Decimal":0,"Charset":"binary","Collate":"binary","Elems":null,"ElemsIsBinaryLit":null,"Array":false},"state":5,"comment":"","hidden":false,"change_state_info":null,"version":2}],"index_info":null,"constraint_info":null,"fk_info":null,"state":5,"pk_is_handle":false,"is_common_handle":false,"common_handle_version":0,"comment":"","auto_inc_id":0,"auto_id_cache":0,"auto_rand_id":0,"max_col_id":8,"max_idx_id":0,"max_fk_id":0,"max_cst_id":0,"update_timestamp":452653255976550448,"ShardRowIDBits":0,"max_shard_row_id_bits":0,"auto_random_bits":0,"auto_random_range_bits":0,"pre_split_regions":0,"partition":null,"compression":"","view":null,"sequence":null,"Lock":null,"version":5,"tiflash_replica":{"Count":1,"LocationLabels":[],"Available":false,"AvailablePartitionIDs":null},"is_columnar":false,"temp_table_type":0,"cache_table_status":0,"policy_ref_info":null,"stats_options":null,"exchange_partition_info":null,"ttl_info":null,"revision":1})json", //
            R"stmt(CREATE TABLE `db_2`.`t_546`(`col_86` Nullable(String), `col_87` MyDateTime(0), `col_88` Nullable(UInt64), `col_89` String, `col_90` UInt8, `col_91` Nullable(UInt64), `col_92` String, `col_93` Nullable(String), `_tidb_rowid` Int64) Engine = DeltaMerge((`_tidb_rowid`), '{"cols":[{"comment":"","default":null,"default_bit":null,"id":1,"name":{"L":"col_86","O":"col_86"},"offset":0,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":"binary","Collate":"binary","Decimal":0,"Elems":null,"Flag":128,"Flen":65535,"Tp":252}},{"comment":"","default":"1994-05-0600:00:00","default_bit":null,"id":2,"name":{"L":"col_87","O":"col_87"},"offset":1,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":"binary","Collate":"binary","Decimal":0,"Elems":null,"Flag":129,"Flen":19,"Tp":12}},{"comment":"","default":null,"default_bit":null,"id":3,"name":{"L":"col_88","O":"col_88"},"offset":2,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":"binary","Collate":"binary","Decimal":0,"Elems":null,"Flag":32,"Flen":42,"Tp":16}},{"comment":"","default":"\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000\\u0000","default_bit":null,"id":4,"name":{"L":"col_89","O":"col_89"},"offset":3,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":"binary","Collate":"binary","Decimal":0,"Elems":null,"Flag":129,"Flen":21,"Tp":254}},{"comment":"","default":null,"default_bit":null,"id":5,"name":{"L":"col_90","O":"col_90"},"offset":4,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":"binary","Collate":"binary","Decimal":0,"Elems":null,"Flag":4129,"Flen":3,"Tp":1}},{"comment":"","default":"\\u0007\\u0007","default_bit":"Bwc=","id":6,"name":{"L":"col_91","O":"col_91"},"offset":5,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":"binary","Collate":"binary","Decimal":0,"Elems":null,"Flag":32,"Flen":12,"Tp":16}},{"comment":"","default":"kY~6to6H4ut*QAPrj@&","default_bit":null,"id":7,"name":{"L":"col_92","O":"col_92"},"offset":6,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":"binary","Collate":"binary","Decimal":0,"Elems":null,"Flag":129,"Flen":343,"Tp":15}},{"comment":"","default":null,"default_bit":null,"id":8,"name":{"L":"col_93","O":"col_93"},"offset":7,"origin_default":null,"origin_default_bit":null,"state":5,"type":{"Charset":"binary","Collate":"binary","Decimal":0,"Elems":null,"Flag":128,"Flen":-1,"Tp":245}}],"comment":"","id":546,"index_info":[],"is_common_handle":false,"keyspace_id":4294967295,"name":{"L":"tcfc7825f","O":"tcfc7825f"},"partition":null,"pk_is_handle":false,"schema_version":-1,"state":5,"tiflash_replica":{"Available":false,"Count":1},"update_timestamp":452653255976550448}', 0))stmt", //
        },
    };
    // clang-format on

    for (const auto & c : cases)
    {
        c.verifyTableInfo();
    }
}
CATCH

} // namespace tests
} // namespace DB
