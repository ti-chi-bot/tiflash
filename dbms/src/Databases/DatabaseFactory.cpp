#include <Common/typeid_cast.h>
#include <Databases/DatabaseDictionary.h>
#include <Databases/DatabaseFactory.h>
#include <Databases/DatabaseMemory.h>
#include <Databases/DatabaseOrdinary.h>
#include <Databases/DatabaseTiFlash.h>
#include <Parsers/ASTCreateQuery.h>
#include <Parsers/ASTLiteral.h>
#include <Poco/File.h>
#include <Storages/Transaction/TiDB.h>

namespace DB
{

namespace ErrorCodes
{
extern const int BAD_ARGUMENTS;
extern const int UNKNOWN_DATABASE_ENGINE;
} // namespace ErrorCodes

template <typename ValueType>
static inline ValueType safeGetLiteralValue(const ASTPtr & ast, const String & engine_name, size_t index)
{
    if (!ast || !typeid_cast<const ASTLiteral *>(ast.get()))
        throw Exception(
            "Database engine " + engine_name + " requested literal argument at index " + DB::toString(index), ErrorCodes::BAD_ARGUMENTS);

    return typeid_cast<const ASTLiteral *>(ast.get())->value.safeGet<ValueType>();
}

DatabasePtr DatabaseFactory::get(
    const String & database_name, const String & metadata_path, const ASTStorage * engine_define, Context & context)
{
    String engine_name = engine_define->engine->name;
    if (engine_name == "TiFlash")
    {
        TiDB::DBInfo db_info;
        UInt64 version = DatabaseTiFlash::CURRENT_VERSION;
        Timestamp tombstone = 0;

        // ENGINE=TiFlash('{JSON format database info}', version)
        const ASTFunction * engine = engine_define->engine;
        if (engine && engine->arguments)
        {
            const auto & arguments = engine->arguments->children;
            if (arguments.size() >= 1)
            {
                const auto db_info_json = safeGetLiteralValue<String>(arguments[0], engine_name, 0);
                if (!db_info_json.empty())
                {
                    db_info.deserialize(db_info_json);
                }
            }
            if (arguments.size() >= 2)
            {
                version = safeGetLiteralValue<UInt64>(arguments[1], engine_name, 1);
            }
            if (arguments.size() >= 3)
            {
                tombstone = safeGetLiteralValue<UInt64>(arguments[2], engine_name, 2);
            }
        }

        return std::make_shared<DatabaseTiFlash>(database_name, metadata_path, db_info, version, tombstone, context);
    }
    else if (engine_name == "Ordinary")
        return std::make_shared<DatabaseOrdinary>(database_name, metadata_path, context);
    else if (engine_name == "Memory")
        return std::make_shared<DatabaseMemory>(database_name);
    else if (engine_name == "Dictionary")
        return std::make_shared<DatabaseDictionary>(database_name, context);

    throw Exception("Unknown database engine: " + engine_name, ErrorCodes::UNKNOWN_DATABASE_ENGINE);
}

} // namespace DB
