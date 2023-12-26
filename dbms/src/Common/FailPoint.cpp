#include <Common/FailPoint.h>

#include <boost/core/noncopyable.hpp>
#include <condition_variable>
#include <mutex>

namespace DB
{
std::unordered_map<String, std::shared_ptr<FailPointChannel>> FailPointHelper::fail_point_wait_channels;

#define APPLY_FOR_FAILPOINTS_ONCE(M)                              \
    M(exception_between_drop_meta_and_data)                       \
    M(exception_between_alter_data_and_meta)                      \
    M(exception_drop_table_during_remove_meta)                    \
    M(exception_between_rename_table_data_and_metadata)           \
    M(exception_between_create_database_meta_and_directory)       \
    M(exception_before_rename_table_old_meta_removed)             \
    M(exception_after_step_1_in_exchange_partition)               \
    M(exception_before_step_2_rename_in_exchange_partition)       \
    M(exception_after_step_2_in_exchange_partition)               \
    M(exception_before_step_3_rename_in_exchange_partition)       \
    M(exception_after_step_3_in_exchange_partition)               \
    M(region_exception_after_read_from_storage_some_error)        \
    M(region_exception_after_read_from_storage_all_error)         \
    M(exception_before_dmfile_remove_encryption)                  \
    M(exception_before_dmfile_remove_from_disk)                   \
    M(force_enable_region_persister_compatible_mode)              \
    M(force_disable_region_persister_compatible_mode)             \
    M(force_triggle_background_merge_delta)                       \
    M(force_triggle_foreground_flush)                             \
    M(exception_before_mpp_register_non_root_mpp_task)            \
    M(exception_before_mpp_register_tunnel_for_non_root_mpp_task) \
    M(exception_during_mpp_register_tunnel_for_non_root_mpp_task) \
    M(exception_before_mpp_non_root_task_run)                     \
    M(exception_during_mpp_non_root_task_run)                     \
    M(exception_before_mpp_register_root_mpp_task)                \
    M(exception_before_mpp_register_tunnel_for_root_mpp_task)     \
    M(exception_before_mpp_root_task_run)                         \
    M(exception_during_mpp_root_task_run)                         \
    M(exception_during_mpp_write_err_to_tunnel)                   \
    M(exception_during_mpp_close_tunnel)                          \
    M(exception_during_write_to_storage)                          \
    M(force_set_sst_to_dtfile_block_size)                         \
    M(force_set_sst_decode_rand)                                  \
    M(exception_before_page_file_write_sync)                      \
    M(force_set_segment_ingest_packs_fail)                        \
    M(segment_merge_after_ingest_packs)                           \
    M(force_formal_page_file_not_exists)                          \
    M(force_legacy_or_checkpoint_page_file_exists)                \
    M(exception_in_creating_set_input_stream)                     \
    M(exception_when_read_from_log)                               \
    M(exception_mpp_hash_build)                                   \
    M(exception_between_schema_change_in_the_same_diff)

#define APPLY_FOR_FAILPOINTS(M)                              \
    M(force_set_page_file_write_errno)                       \
    M(force_split_io_size_4k)                                \
    M(minimum_block_size_for_cross_join)                     \
    M(random_exception_after_dt_write_done)                  \
    M(random_slow_page_storage_write)                        \
    M(random_exception_after_page_storage_sequence_acquired) \
    M(random_slow_page_storage_remove_expired_snapshots)     \
    M(random_slow_page_storage_list_all_live_files)          \
    M(force_set_safepoint_when_decode_block)                 \
    M(force_set_page_data_compact_batch)                     \
    M(force_set_dtfile_exist_when_acquire_id)                \
    M(force_no_local_region_for_mpp_task)                    \
    M(force_remote_read_for_batch_cop)                       \
    M(force_context_path)

#define APPLY_FOR_FAILPOINTS_ONCE_WITH_CHANNEL(M) \
    M(pause_after_learner_read)                   \
    M(hang_in_execution)                          \
    M(pause_before_dt_background_delta_merge)     \
    M(pause_until_dt_background_delta_merge)      \
    M(pause_before_apply_raft_cmd)                \
    M(pause_before_apply_raft_snapshot)           \
    M(pause_until_apply_raft_snapshot)

<<<<<<< HEAD
#define APPLY_FOR_FAILPOINTS_WITH_CHANNEL(M) \
    M(pause_when_reading_from_dt_stream)     \
    M(pause_when_writing_to_dt_store)        \
    M(pause_when_ingesting_to_dt_store)      \
    M(pause_when_altering_dt_store)          \
    M(pause_after_copr_streams_acquired)
=======
#define APPLY_FOR_PAUSEABLE_FAILPOINTS(M) \
    M(pause_when_reading_from_dt_stream)  \
    M(pause_when_writing_to_dt_store)     \
    M(pause_when_ingesting_to_dt_store)   \
    M(pause_when_altering_dt_store)       \
    M(pause_after_copr_streams_acquired)  \
    M(pause_query_init)                   \
    M(pause_before_prehandle_snapshot)    \
    M(pause_before_prehandle_subtask)     \
    M(pause_when_persist_region)          \
    M(pause_before_wn_establish_task)     \
    M(pause_passive_flush_before_persist_region)

#define APPLY_FOR_RANDOM_FAILPOINTS(M)                       \
    M(random_tunnel_wait_timeout_failpoint)                  \
    M(random_tunnel_write_failpoint)                         \
    M(random_tunnel_init_rpc_failure_failpoint)              \
    M(random_receiver_local_msg_push_failure_failpoint)      \
    M(random_receiver_sync_msg_push_failure_failpoint)       \
    M(random_receiver_async_msg_push_failure_failpoint)      \
    M(random_limit_check_failpoint)                          \
    M(random_join_build_failpoint)                           \
    M(random_join_prob_failpoint)                            \
    M(random_aggregate_create_state_failpoint)               \
    M(random_aggregate_merge_failpoint)                      \
    M(random_sharedquery_failpoint)                          \
    M(random_interpreter_failpoint)                          \
    M(random_task_manager_find_task_failure_failpoint)       \
    M(random_min_tso_scheduler_failpoint)                    \
    M(random_pipeline_model_task_run_failpoint)              \
    M(random_pipeline_model_task_construct_failpoint)        \
    M(random_pipeline_model_event_schedule_failpoint)        \
    M(random_pipeline_model_event_finish_failpoint)          \
    M(random_pipeline_model_operator_run_failpoint)          \
    M(random_pipeline_model_cancel_failpoint)                \
    M(random_pipeline_model_execute_prefix_failpoint)        \
    M(random_pipeline_model_execute_suffix_failpoint)        \
    M(random_spill_to_disk_failpoint)                        \
    M(random_region_persister_latency_failpoint)             \
    M(random_restore_from_disk_failpoint)                    \
    M(random_exception_when_connect_local_tunnel)            \
    M(random_exception_when_construct_async_request_handler) \
    M(random_fail_in_resize_callback)                        \
    M(random_marked_for_auto_spill)                          \
    M(random_trigger_remote_read)                            \
    M(random_cop_send_failure_failpoint)
>>>>>>> 0329ed40a4 (KVStore: Reduce lock contention in `RegionPersister::doPersist` (#8584))

namespace FailPoints
{
#define M(NAME) extern const char(NAME)[] = #NAME "";
APPLY_FOR_FAILPOINTS_ONCE(M)
APPLY_FOR_FAILPOINTS(M)
APPLY_FOR_FAILPOINTS_ONCE_WITH_CHANNEL(M)
APPLY_FOR_FAILPOINTS_WITH_CHANNEL(M)
#undef M
} // namespace FailPoints

#ifdef FIU_ENABLE
class FailPointChannel : private boost::noncopyable
{
public:
    // wake up all waiting threads when destroy
    ~FailPointChannel() { notifyAll(); }

    void wait()
    {
        std::unique_lock lock(m);
        cv.wait(lock);
    }

    void notifyAll()
    {
        std::unique_lock lock(m);
        cv.notify_all();
    }

private:
    std::mutex m;
    std::condition_variable cv;
};

void FailPointHelper::enableFailPoint(const String & fail_point_name)
{
#define SUB_M(NAME, flags)                                                                                  \
    if (fail_point_name == FailPoints::NAME)                                                                \
    {                                                                                                       \
        /* FIU_ONETIME -- Only fail once; the point of failure will be automatically disabled afterwards.*/ \
        fiu_enable(FailPoints::NAME, 1, nullptr, flags);                                                    \
        return;                                                                                             \
    }

#define M(NAME) SUB_M(NAME, FIU_ONETIME)
    APPLY_FOR_FAILPOINTS_ONCE(M)
#undef M
#define M(NAME) SUB_M(NAME, 0)
    APPLY_FOR_FAILPOINTS(M)
#undef M
#undef SUB_M

#define SUB_M(NAME, flags)                                                                                  \
    if (fail_point_name == FailPoints::NAME)                                                                \
    {                                                                                                       \
        /* FIU_ONETIME -- Only fail once; the point of failure will be automatically disabled afterwards.*/ \
        fiu_enable(FailPoints::NAME, 1, nullptr, flags);                                                    \
        fail_point_wait_channels.try_emplace(FailPoints::NAME, std::make_shared<FailPointChannel>());       \
        return;                                                                                             \
    }

#define M(NAME) SUB_M(NAME, FIU_ONETIME)
    APPLY_FOR_FAILPOINTS_ONCE_WITH_CHANNEL(M)
#undef M

#define M(NAME) SUB_M(NAME, 0)
    APPLY_FOR_FAILPOINTS_WITH_CHANNEL(M)
#undef M
#undef SUB_M

    throw Exception("Cannot find fail point " + fail_point_name, ErrorCodes::FAIL_POINT_ERROR);
}

void FailPointHelper::disableFailPoint(const String & fail_point_name)
{
    if (auto iter = fail_point_wait_channels.find(fail_point_name); iter != fail_point_wait_channels.end())
    {
        /// can not rely on deconstruction to do the notify_all things, because
        /// if someone wait on this, the deconstruct will never be called.
        iter->second->notifyAll();
        fail_point_wait_channels.erase(iter);
    }
    fiu_disable(fail_point_name.c_str());
}

void FailPointHelper::wait(const String & fail_point_name)
{
    if (auto iter = fail_point_wait_channels.find(fail_point_name); iter == fail_point_wait_channels.end())
        throw Exception("Can not find channel for fail point " + fail_point_name);
    else
    {
        auto ptr = iter->second;
        ptr->wait();
    }
}
#else
class FailPointChannel
{
};

void FailPointHelper::enableFailPoint(const String &) {}

void FailPointHelper::disableFailPoint(const String &) {}

void FailPointHelper::wait(const String &) {}
#endif

} // namespace DB
