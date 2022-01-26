#include <Flash/Coprocessor/DAGContext.h>

namespace DB
{

namespace ErrorCodes
{
extern const int TRUNCATE_ERROR;
extern const int OVERFLOW_ERROR;
extern const int DIVIDED_BY_ZERO;
extern const int INVALID_TIME;
} // namespace ErrorCodes

<<<<<<< HEAD
namespace
{
enum Flag
{
    IGNORE_TRUNCATE = 1,
    TRUNCATE_AS_WARNING = 1u << 1u,
    PAD_CHAR_TO_FULL_LENGTH = 1u << 2u,
    IN_INSERT_STMT = 1u << 3u,
    IN_UPDATE_OR_DELETE_STMT = 1u << 4u,
    IN_SELECT_STMT = 1u << 5u,
    OVERFLOW_AS_WARNING = 1u << 6u,
    IGNORE_ZERO_IN_DATE = 1u << 7u,
    DIVIDED_BY_ZERO_AS_WARNING = 1u << 8u,
    IN_LOAD_DATA_STMT = 1u << 10u,
};

enum SqlMode
{
    REAL_AS_FLOAT = 1ul,
    PIPES_AS_CONCAT = 1ul << 1ul,
    ANSI_QUOTES = 1ul << 2ul,
    IGNORE_SPACE = 1ul << 3ul,
    NOT_USED = 1ul << 4ul,
    ONLY_FULL_GROUP_BY = 1ul << 5ul,
    NO_UNSIGNED_SUBTRACTION = 1ul << 6ul,
    NO_DIR_IN_CREATE = 1ul << 7ul,
    POSTGRESQL = 1ul << 8ul,
    ORACLE = 1ul << 9ul,
    MSSQL = 1ul << 10ul,
    DB2 = 1ul << 11ul,
    MAXDB = 1ul << 12ul,
    NO_KEY_OPTIONS = 1ul << 13ul,
    NO_TABLE_OPTIONS = 1ul << 14ul,
    NO_FIELD_OPTIONS = 1ul << 15ul,
    MYSQL323 = 1ul << 16ul,
    MYSQL40 = 1ul << 17ul,
    ANSI = 1ul << 18ul,
    NO_AUTO_VALUE_ON_ZERO = 1ul << 19ul,
    NO_BACK_SLASH_ESCAPES = 1ul << 20ul,
    STRICT_TRANS_TABLES = 1ul << 21ul,
    STRICT_ALL_TABLES = 1ul << 22ul,
    NO_ZERO_IN_DATE = 1ul << 23ul,
    NO_ZERO_DATE = 1ul << 24ul,
    INVALID_DATES = 1ul << 25ul,
    ERROR_FOR_DIVISION_BY_ZERO = 1ul << 26ul,
    TRADITIONAL = 1ul << 27ul,
    NO_AUTO_CREATE_USER = 1ul << 28ul,
    HIGH_NOT_PRECEDENCE = 1ul << 29ul,
    NO_ENGINE_SUBSTITUTION = 1ul << 30ul,

    // Duplicated with Flag::PAD_CHAR_TO_FULL_LENGTH
    // PAD_CHAR_TO_FULL_LENGTH = 1ul << 31ul,

    ALLOW_INVALID_DATES = 1ul << 32ul,
};
} // namespace

bool strictSqlMode(UInt64 sql_mode) { return sql_mode & SqlMode::STRICT_ALL_TABLES || sql_mode & SqlMode::STRICT_TRANS_TABLES; }

bool DAGContext::allowZeroInDate() const { return flags & Flag::IGNORE_ZERO_IN_DATE; }

bool DAGContext::allowInvalidDate() const { return sql_mode & SqlMode::ALLOW_INVALID_DATES; }
=======
bool strictSqlMode(UInt64 sql_mode)
{
    return sql_mode & TiDBSQLMode::STRICT_ALL_TABLES || sql_mode & TiDBSQLMode::STRICT_TRANS_TABLES;
}

bool DAGContext::allowZeroInDate() const
{
    return flags & TiDBSQLFlags::IGNORE_ZERO_IN_DATE;
}

bool DAGContext::allowInvalidDate() const
{
    return sql_mode & TiDBSQLMode::ALLOW_INVALID_DATES;
}
>>>>>>> 6ea6c80198 (Fix cast to decimal overflow bug (#3922))

std::map<String, ProfileStreamsInfo> & DAGContext::getProfileStreamsMap() { return profile_streams_map; }

std::unordered_map<String, BlockInputStreams> & DAGContext::getProfileStreamsMapForJoinBuildSide()
{
    return profile_streams_map_for_join_build_side;
}

std::unordered_map<UInt32, std::vector<String>> & DAGContext::getQBIdToJoinAliasMap() { return qb_id_to_join_alias_map; }

void DAGContext::handleTruncateError(const String & msg)
{
    if (!(flags & TiDBSQLFlags::IGNORE_TRUNCATE || flags & TiDBSQLFlags::TRUNCATE_AS_WARNING))
    {
        throw TiFlashException("Truncate error " + msg, Errors::Types::Truncated);
    }
    tipb::Error warning;
    warning.set_code(0);
    warning.set_msg(msg);
    appendWarning(warning);
}

void DAGContext::handleOverflowError(const String & msg, const TiFlashError & error)
{
    if (!(flags & TiDBSQLFlags::OVERFLOW_AS_WARNING))
    {
        throw TiFlashException("Overflow error: " + msg, error);
    }
    tipb::Error warning;
    warning.set_code(0);
    warning.set_msg("Overflow error: " + msg);
    appendWarning(warning);
}

void DAGContext::handleDivisionByZero()
{
    if (flags & TiDBSQLFlags::IN_INSERT_STMT || flags & TiDBSQLFlags::IN_UPDATE_OR_DELETE_STMT)
    {
        if (!(sql_mode & TiDBSQLMode::ERROR_FOR_DIVISION_BY_ZERO))
            return;
        if (strictSqlMode(sql_mode) && !(flags & TiDBSQLFlags::DIVIDED_BY_ZERO_AS_WARNING))
        {
            throw TiFlashException("Division by 0", Errors::Expression::DivisionByZero);
        }
    }
    tipb::Error warning;
    warning.set_code(0);
    warning.set_msg("Division by 0");
    appendWarning(warning);
}

void DAGContext::handleInvalidTime(const String & msg, const TiFlashError & error)
{
    if (!(error.is(Errors::Types::WrongValue) || error.is(Errors::Types::Truncated)))
    {
        throw TiFlashException(msg, error);
    }
    handleTruncateError(msg);
    if (strictSqlMode(sql_mode) && (flags & TiDBSQLFlags::IN_INSERT_STMT || flags & TiDBSQLFlags::IN_UPDATE_OR_DELETE_STMT))
    {
        throw TiFlashException(msg, error);
    }
}

<<<<<<< HEAD
bool DAGContext::shouldClipToZero() { return flags & Flag::IN_INSERT_STMT || flags & Flag::IN_LOAD_DATA_STMT; }
=======
void DAGContext::appendWarning(const String & msg, int32_t code)
{
    tipb::Error warning;
    warning.set_code(code);
    warning.set_msg(msg);
    appendWarning(warning);
}

bool DAGContext::shouldClipToZero() const
{
    return flags & TiDBSQLFlags::IN_INSERT_STMT || flags & TiDBSQLFlags::IN_LOAD_DATA_STMT;
}

std::pair<bool, double> DAGContext::getTableScanThroughput()
{
    if (table_scan_executor_id.empty())
        return std::make_pair(false, 0.0);

    // collect table scan metrics
    UInt64 time_processed_ns = 0;
    UInt64 num_produced_bytes = 0;
    for (auto & p : getProfileStreamsMap())
    {
        if (p.first == table_scan_executor_id)
        {
            for (auto & stream_ptr : p.second.input_streams)
            {
                if (auto * p_stream = dynamic_cast<IProfilingBlockInputStream *>(stream_ptr.get()))
                {
                    time_processed_ns = std::max(time_processed_ns, p_stream->getProfileInfo().execution_time);
                    num_produced_bytes += p_stream->getProfileInfo().bytes;
                }
            }
            break;
        }
    }

    // convert to bytes per second
    return std::make_pair(true, num_produced_bytes / (static_cast<double>(time_processed_ns) / 1000000000ULL));
}

void DAGContext::attachBlockIO(const BlockIO & io_)
{
    io = io_;
}
void DAGContext::initExchangeReceiverIfMPP(Context & context, size_t max_streams)
{
    if (isMPPTask())
    {
        if (mpp_exchange_receiver_map_inited)
            throw TiFlashException("Repeatedly initialize mpp_exchange_receiver_map", Errors::Coprocessor::Internal);
        traverseExecutors(dag_request, [&](const tipb::Executor & executor) {
            if (executor.tp() == tipb::ExecType::TypeExchangeReceiver)
            {
                assert(executor.has_executor_id());
                const auto & executor_id = executor.executor_id();
                mpp_exchange_receiver_map[executor_id] = std::make_shared<ExchangeReceiver>(
                    std::make_shared<GRPCReceiverContext>(
                        executor.exchange_receiver(),
                        getMPPTaskMeta(),
                        context.getTMTContext().getKVCluster(),
                        context.getTMTContext().getMPPTaskManager(),
                        context.getSettingsRef().enable_local_tunnel),
                    executor.exchange_receiver().encoded_task_meta_size(),
                    max_streams,
                    log);
            }
        });
        mpp_exchange_receiver_map_inited = true;
    }
}

const std::unordered_map<String, std::shared_ptr<ExchangeReceiver>> & DAGContext::getMPPExchangeReceiverMap() const
{
    if (!isMPPTask())
        throw TiFlashException("mpp_exchange_receiver_map is used in mpp only", Errors::Coprocessor::Internal);
    if (!mpp_exchange_receiver_map_inited)
        throw TiFlashException("mpp_exchange_receiver_map has not been initialized", Errors::Coprocessor::Internal);
    return mpp_exchange_receiver_map;
}
>>>>>>> 6ea6c80198 (Fix cast to decimal overflow bug (#3922))

} // namespace DB
