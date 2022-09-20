#include <Common/CurrentMetrics.h>
#include <DataStreams/OneBlockInputStream.h>
#include <Storages/DeltaMerge/DMContext.h>
#include <Storages/DeltaMerge/DeltaMergeStore.h>
#include <Storages/DeltaMerge/Segment.h>
#include <TestUtils/TiFlashTestBasic.h>
#include <gtest/gtest.h>

#include <ctime>
#include <memory>

#include "dm_basic_include.h"

namespace CurrentMetrics
{
extern const Metric DT_SnapshotOfRead;
extern const Metric DT_SnapshotOfReadRaw;
extern const Metric DT_SnapshotOfSegmentSplit;
extern const Metric DT_SnapshotOfSegmentMerge;
extern const Metric DT_SnapshotOfDeltaMerge;
extern const Metric DT_SnapshotOfPlaceIndex;
} // namespace CurrentMetrics

namespace DB
{
namespace DM
{
namespace tests
{

class Segment_test : public ::testing::Test
{
public:
    Segment_test() : name("tmp"), storage_pool() {}

protected:
    void dropDataOnDisk()
    {
        // drop former-gen table's data in disk
        if (Poco::File file(DB::tests::TiFlashTestEnv::getTemporaryPath()); file.exists())
            file.remove(true);
    }

public:
    static void SetUpTestCase() {}

    virtual DB::Settings getSettings() { return DB::Settings(); }

    void SetUp() override
    {
        db_context        = std::make_unique<Context>(DMTestEnv::getContext(getSettings()));
        storage_path_pool = std::make_unique<StoragePathPool>(db_context->getPathPool().withTable("test", "t1", false));
        storage_path_pool->drop(true);
        table_columns_ = std::make_shared<ColumnDefines>();
        dropDataOnDisk();

        segment = reload();
        ASSERT_EQ(segment->segmentId(), DELTA_MERGE_FIRST_SEGMENT_ID);
    }

protected:
    SegmentPtr reload(const ColumnDefinesPtr & pre_define_columns = {}, DB::Settings && db_settings = DB::Settings())
    {
        *db_context       = DMTestEnv::getContext(db_settings);
        storage_path_pool = std::make_unique<StoragePathPool>(db_context->getPathPool().withTable("test", "t1", false));
        storage_pool      = std::make_unique<StoragePool>("test.t1", *storage_path_pool, *db_context, db_context->getSettingsRef());
        storage_pool->restore();
        ColumnDefinesPtr cols = (!pre_define_columns) ? DMTestEnv::getDefaultColumns() : pre_define_columns;
        setColumns(cols);

        auto segment_id = storage_pool->newMetaPageId();
        return Segment::newSegment(*dm_context_, table_columns_, RowKeyRange::newAll(false, 1), segment_id, 0);
    }

    // setColumns should update dm_context at the same time
    void setColumns(const ColumnDefinesPtr & columns)
    {
        *table_columns_ = *columns;

        dm_context_ = std::make_unique<DMContext>(*db_context,
                                                  *storage_path_pool,
                                                  *storage_pool,
                                                  /*min_version_*/ 0,
                                                  settings.not_compress_columns,
                                                  false,
                                                  1,
                                                  db_context->getSettingsRef());
    }

    const ColumnDefinesPtr & tableColumns() const { return table_columns_; }

    DMContext & dmContext() { return *dm_context_; }

protected:
    std::unique_ptr<Context> db_context;
    // the table name
    String name;
    /// all these var lives as ref in dm_context
    std::unique_ptr<StoragePathPool> storage_path_pool;
    std::unique_ptr<StoragePool>     storage_pool;
    ColumnDefinesPtr                 table_columns_;
    DM::DeltaMergeStore::Settings    settings;
    /// dm_context
    std::unique_ptr<DMContext> dm_context_;

    // the segment we are going to test
    SegmentPtr segment;
};

TEST_F(Segment_test, WriteRead)
try
{
    const size_t num_rows_write = 100;
    {

        Block block = DMTestEnv::prepareSimpleWriteBlock(0, num_rows_write, false);
        // write to segment
        segment->write(dmContext(), block);
        // estimate segment
        auto estimatedRows = segment->getEstimatedRows();
        ASSERT_EQ(estimatedRows, block.rows());

        auto estimatedBytes = segment->getEstimatedBytes();
        ASSERT_EQ(estimatedBytes, block.bytes());
    }

    {
        // check segment
        segment->check(dmContext(), "test");
    }

    { // Round 1
        {
            // read written data (only in delta)
            auto   in            = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
            size_t num_rows_read = 0;
            in->readPrefix();
            while (Block block = in->read())
            {
                num_rows_read += block.rows();
            }
            in->readSuffix();
            ASSERT_EQ(num_rows_read, num_rows_write);
        }

        {
            // flush segment
            segment = segment->mergeDelta(dmContext(), tableColumns());
            ;
        }

        {
            // read written data (only in stable)
            auto   in            = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
            size_t num_rows_read = 0;
            in->readPrefix();
            while (Block block = in->read())
            {
                num_rows_read += block.rows();
            }
            in->readSuffix();
            ASSERT_EQ(num_rows_read, num_rows_write);
        }
    }

    const size_t num_rows_write_2 = 55;

    {
        // write more rows to segment
        Block block = DMTestEnv::prepareSimpleWriteBlock(num_rows_write, num_rows_write + num_rows_write_2, false);
        segment->write(dmContext(), std::move(block));
    }

    { // Round 2
        {
            // read written data (both in delta and stable)
            auto   in            = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
            size_t num_rows_read = 0;
            in->readPrefix();
            while (Block block = in->read())
            {
                num_rows_read += block.rows();
            }
            in->readSuffix();
            ASSERT_EQ(num_rows_read, num_rows_write + num_rows_write_2);
        }

        {
            // flush segment
            segment = segment->mergeDelta(dmContext(), tableColumns());
            ;
        }

        {
            // read written data (only in stable)
            auto   in            = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
            size_t num_rows_read = 0;
            in->readPrefix();
            while (Block block = in->read())
            {
                num_rows_read += block.rows();
            }
            in->readSuffix();
            ASSERT_EQ(num_rows_read, num_rows_write + num_rows_write_2);
        }
    }
}
CATCH

TEST_F(Segment_test, WriteRead2)
try
{
    const size_t num_rows_write = dmContext().stable_pack_rows;
    {
        // write a block with rows all deleted
        Block block = DMTestEnv::prepareBlockWithIncreasingTso(2, 100, 100 + num_rows_write, true);
        segment->write(dmContext(), block);
        // write not deleted rows with larger pk
        Block block2 = DMTestEnv::prepareBlockWithIncreasingTso(3, 100, 100 + num_rows_write, false);
        segment->write(dmContext(), block2);

        // flush segment and make sure there is two packs in stable
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ASSERT_EQ(segment->getStable()->getPacks(), 2);
    }

    {
        Block block = DMTestEnv::prepareBlockWithIncreasingTso(1, 100, 100 + num_rows_write, false);
        segment->write(dmContext(), block);
    }

    {
        auto in = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        size_t num_rows_read = 0;
        in->readPrefix();
        while (Block block = in->read())
        {
            num_rows_read += block.rows();
        }
        in->readSuffix();
        // only write two visible pks
        ASSERT_EQ(num_rows_read, 2);
    }
}
CATCH

TEST_F(Segment_test, ReadWithMoreAdvacedDeltaIndex)
try
{
    // Test the case that reading rows with an advance DeltaIndex
    //  1. Thread A creates a delta snapshot with 100 rows.
    //  2. Thread B inserts 100 rows into the delta
    //  3. Thread B reads and place 200 rows to a new DeltaTree, and update the `shared_delta_index` to 200
    //  4. Thread A read with an DeltaTree that only placed 100 rows but `placed_rows` in `shared_delta_index` with 200
    //  5. Thread A use the DeltaIndex with placed_rows = 200 to do the merge in DeltaMergeBlockInputStream
    size_t offset     = 0;
    auto   write_rows = [&](size_t rows) {
        Block block = DMTestEnv::prepareSimpleWriteBlock(offset, offset + rows, false);
        offset += rows;
        // write to segment
        segment->write(dmContext(), block);
    };

    auto check_rows = [&](size_t expected_rows) {
        auto   in            = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        size_t num_rows_read = 0;
        in->readPrefix();
        while (Block block = in->read())
        {
            num_rows_read += block.rows();
        }
        in->readSuffix();
        ASSERT_EQ(num_rows_read, expected_rows);
    };

    {
        // check segment
        segment->check(dmContext(), "test");
    }

    // Thread A
    write_rows(100);
    check_rows(100);
    auto snap = segment->createSnapshot(dmContext(), false, CurrentMetrics::DT_SnapshotOfRead);

    // Thread B
    write_rows(100);
    check_rows(200);

    // Thread A
    {
        auto in = segment->getInputStream(
            dmContext(), *tableColumns(), snap, {RowKeyRange::newAll(false, 1)}, {}, MAX_UINT64, DEFAULT_BLOCK_SIZE);
        int num_rows_read = 0;
        in->readPrefix();
        while (Block block = in->read())
        {
            num_rows_read += block.rows();
        }
        in->readSuffix();
        ASSERT_EQ(num_rows_read, 100);
    }
}
CATCH

class SegmentDeletionRelevantPlace_test : public Segment_test, //
                                          public testing::WithParamInterface<bool>
{
    DB::Settings getSettings() override
    {
        DB::Settings settings;
        auto         enable_relevant_place = GetParam();

        if (enable_relevant_place)
            settings.set("dt_enable_relevant_place", "1");
        else
            settings.set("dt_enable_relevant_place", "0");
        return settings;
    }
};


TEST_P(SegmentDeletionRelevantPlace_test, ShareDelteRangeIndex)
try
{
    const size_t num_rows_write = 300;
    {
        // write to segment
        Block block = DMTestEnv::prepareSimpleWriteBlock(0, num_rows_write, false);
        segment->write(dmContext(), std::move(block));
    }

    auto get_rows = [&](const RowKeyRange & range) {
        auto in = segment->getInputStream(dmContext(), *tableColumns(), {range});
        in->readPrefix();
        size_t rows = 0;
        while (Block block = in->read())
        {
            rows += block.rows();
        }
        in->readSuffix();

        return rows;
    };

    // First place the block packs, so that we can only place DeleteRange below.
    get_rows(RowKeyRange::fromHandleRange(HandleRange::newAll()));

    {
        HandleRange remove(100, 200);
        segment->write(dmContext(), {RowKeyRange::fromHandleRange(remove)});
    }

    // The first call of get_rows below will place the DeleteRange into delta index.
    auto rows1 = get_rows(RowKeyRange::fromHandleRange(HandleRange(0, 150)));
    auto rows2 = get_rows(RowKeyRange::fromHandleRange(HandleRange(150, 300)));

    ASSERT_EQ(rows1, (size_t)100);
    ASSERT_EQ(rows2, (size_t)100);
}
CATCH

INSTANTIATE_TEST_CASE_P(WhetherEnableRelevantPlace, SegmentDeletionRelevantPlace_test, testing::Values(true, false));

class SegmentDeletion_test : public Segment_test, //
                             public testing::WithParamInterface<std::tuple<bool, bool>>
{
};

TEST_P(SegmentDeletion_test, DeleteDataInDelta)
try
{
    const size_t num_rows_write = 100;
    {
        // write to segment
        Block block = DMTestEnv::prepareSimpleWriteBlock(0, num_rows_write, false);
        segment->write(dmContext(), std::move(block));
    }

    auto [read_before_delete, merge_delta_after_delete] = GetParam();
    if (read_before_delete)
    {
        // read written data
        auto   in            = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        size_t num_rows_read = 0;
        in->readPrefix();
        while (Block block = in->read())
        {
            num_rows_read += block.rows();
        }
        in->readSuffix();
        ASSERT_EQ(num_rows_read, num_rows_write);
    }

    {
        // test delete range [1,99) for data in delta
        HandleRange remove(1, 99);
        segment->write(dmContext(), {RowKeyRange::fromHandleRange(remove)});
        // TODO test delete range partial overlap with segment
        // TODO test delete range not included by segment
    }

    if (merge_delta_after_delete)
    {
        // flush segment for apply delete range
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    {
        // read after delete range
        auto in = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        in->readPrefix();
        while (Block block = in->read())
        {
            ASSERT_EQ(block.rows(), 2UL);
            for (auto & iter : block)
            {
                auto c = iter.column;
                if (iter.name == DMTestEnv::pk_name)
                {
                    EXPECT_EQ(c->getInt(0), 0);
                    EXPECT_EQ(c->getInt(1), 99);
                }
            }
        }
        in->readSuffix();
    }
}
CATCH

TEST_P(SegmentDeletion_test, DeleteDataInStable)
try
{
    const size_t num_rows_write = 100;
    {
        // write to segment
        Block block = DMTestEnv::prepareSimpleWriteBlock(0, num_rows_write, false);
        segment->write(dmContext(), std::move(block));
    }

    auto [read_before_delete, merge_delta_after_delete] = GetParam();
    if (read_before_delete)
    {
        // read written data
        auto   in            = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        size_t num_rows_read = 0;
        in->readPrefix();
        while (Block block = in->read())
        {
            num_rows_read += block.rows();
        }
        in->readSuffix();
        ASSERT_EQ(num_rows_read, num_rows_write);
    }

    {
        // flush segment
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    {
        // test delete range [1,99) for data in stable
        HandleRange remove(1, 99);
        segment->write(dmContext(), {RowKeyRange::fromHandleRange(remove)});
        // TODO test delete range partial overlap with segment
        // TODO test delete range not included by segment

        // flush segment
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    if (merge_delta_after_delete)
    {
        // flush segment for apply delete range
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    {
        // read after delete range
        auto in = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        in->readPrefix();
        while (Block block = in->read())
        {
            ASSERT_EQ(block.rows(), 2UL);
            for (auto & iter : block)
            {
                auto c = iter.column;
                if (iter.name == DMTestEnv::pk_name)
                {
                    EXPECT_EQ(c->getInt(0), 0);
                    EXPECT_EQ(c->getInt(1), 99);
                }
            }
        }
        in->readSuffix();
    }
}
CATCH

TEST_P(SegmentDeletion_test, DeleteDataInStableAndDelta)
try
{
    const size_t num_rows_write = 100;
    {
        // write [0, 50) to segment
        Block block = DMTestEnv::prepareSimpleWriteBlock(0, num_rows_write / 2, false);
        segment->write(dmContext(), std::move(block));
        // flush [0, 50) to segment's stable
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    auto [read_before_delete, merge_delta_after_delete] = GetParam();

    {
        // write [50, 100) to segment's delta
        Block block = DMTestEnv::prepareSimpleWriteBlock(num_rows_write / 2, num_rows_write, false);
        segment->write(dmContext(), std::move(block));
    }

    if (read_before_delete)
    {
        // read written data
        auto   in            = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        size_t num_rows_read = 0;
        in->readPrefix();
        while (Block block = in->read())
        {
            num_rows_read += block.rows();
        }
        in->readSuffix();
        ASSERT_EQ(num_rows_read, num_rows_write);
    }

    {
        // test delete range [1,99) for data in stable and delta
        HandleRange remove(1, 99);
        segment->write(dmContext(), {RowKeyRange::fromHandleRange(remove)});
        // TODO test delete range partial overlap with segment
        // TODO test delete range not included by segment
    }

    if (merge_delta_after_delete)
    {
        // flush segment for apply delete range
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    {
        // read after delete range
        auto in = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        in->readPrefix();
        while (Block block = in->read())
        {
            ASSERT_EQ(block.rows(), 2UL);
            for (auto & iter : block)
            {
                auto c = iter.column;
                if (iter.name == DMTestEnv::pk_name)
                {
                    EXPECT_EQ(c->getInt(0), 0);
                    EXPECT_EQ(c->getInt(1), 99);
                }
            }
        }
        in->readSuffix();
    }
}
CATCH

INSTANTIATE_TEST_CASE_P(WhetherReadOrMergeDeltaBeforeDeleteRange, SegmentDeletion_test, testing::Combine(testing::Bool(), testing::Bool()));

TEST_F(Segment_test, DeleteRead)
try
{
    const size_t num_rows_write = 64;
    {
        Block block = DMTestEnv::prepareSimpleWriteBlock(0, num_rows_write, false);
        segment->write(dmContext(), std::move(block));
    }

    {
        // flush segment
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    {
        // Test delete range [70, 100)
        HandleRange del{70, 100};
        segment->write(dmContext(), {RowKeyRange::fromHandleRange(del)});
        // flush segment
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    {
        // Read after deletion
        // The deleted range has no overlap with current data, so there should be no change
        auto in = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        in->readPrefix();
        while (Block block = in->read())
        {
            ASSERT_EQ(block.rows(), num_rows_write);
            for (auto & iter : block)
            {
                auto c = iter.column;
                for (Int64 i = 0; i < Int64(c->size()); i++)
                {
                    if (iter.name == DMTestEnv::pk_name)
                    {
                        EXPECT_EQ(c->getInt(i), i);
                    }
                }
            }
        }
        in->readSuffix();
    }

    {
        // Test delete range [63, 70)
        HandleRange del{63, 70};
        segment->write(dmContext(), {RowKeyRange::fromHandleRange(del)});
        // flush segment
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    {
        // Read after deletion
        // The deleted range has overlap range [63, 64) with current data, so the record with Handle 63 should be deleted
        auto in = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        in->readPrefix();
        while (Block block = in->read())
        {
            ASSERT_EQ(block.rows(), num_rows_write - 1);
            for (auto & iter : block)
            {
                auto c = iter.column;
                if (iter.name == DMTestEnv::pk_name)
                {
                    EXPECT_EQ(c->getInt(0), 0);
                    EXPECT_EQ(c->getInt(62), 62);
                }
                EXPECT_EQ(c->size(), 63UL);
            }
        }
        in->readSuffix();
    }

    {
        // Test delete range [1, 32)
        HandleRange del{1, 32};
        segment->write(dmContext(), {RowKeyRange::fromHandleRange(del)});
        // flush segment
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    {
        // Read after deletion
        auto in = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        in->readPrefix();
        while (Block block = in->read())
        {
            ASSERT_EQ(block.rows(), num_rows_write - 32);
            for (auto & iter : block)
            {
                auto c = iter.column;
                if (iter.name == DMTestEnv::pk_name)
                {
                    EXPECT_EQ(c->getInt(0), 0);
                    EXPECT_EQ(c->getInt(1), 32);
                }
            }
        }
        in->readSuffix();
    }

    {
        // Test delete range [1, 32)
        // delete should be idempotent
        HandleRange del{1, 32};
        segment->write(dmContext(), {RowKeyRange::fromHandleRange(del)});
        // flush segment
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    {
        // Read after deletion
        auto in = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        in->readPrefix();
        while (Block block = in->read())
        {
            ASSERT_EQ(block.rows(), num_rows_write - 32);
            for (auto & iter : block)
            {
                auto c = iter.column;
                if (iter.name == DMTestEnv::pk_name)
                {
                    EXPECT_EQ(c->getInt(0), 0);
                    EXPECT_EQ(c->getInt(1), 32);
                }
            }
        }
        in->readSuffix();
    }

    {
        // Test delete range [0, 2)
        // There is an overlap range [0, 1)
        HandleRange del{0, 2};
        segment->write(dmContext(), {RowKeyRange::fromHandleRange(del)});
        // flush segment
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    {
        // Read after deletion
        auto in = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        in->readPrefix();
        while (Block block = in->read())
        {
            ASSERT_EQ(block.rows(), num_rows_write - 33);
            for (auto & iter : block)
            {
                auto c = iter.column;
                if (iter.name == DMTestEnv::pk_name)
                {
                    EXPECT_EQ(c->getInt(0), 32);
                }
            }
        }
        in->readSuffix();
    }
}
CATCH

TEST_F(Segment_test, Split)
try
{
    const size_t num_rows_write_per_batch = 100;
    const size_t num_rows_write = num_rows_write_per_batch * 2;
    {
        // write to segment and flush
        Block block = DMTestEnv::prepareSimpleWriteBlock(0, num_rows_write_per_batch, false);
        segment->write(dmContext(), std::move(block), true);
    }
    {
        // write to segment and don't flush
        Block block = DMTestEnv::prepareSimpleWriteBlock(num_rows_write_per_batch, 2 * num_rows_write_per_batch, false);
        segment->write(dmContext(), std::move(block), false);
    }

    {
        // read written data
        auto in = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});

        size_t num_rows_read = 0;
        in->readPrefix();
        while (Block block = in->read())
        {
            num_rows_read += block.rows();
        }
        in->readSuffix();
        ASSERT_EQ(num_rows_read, num_rows_write);
    }

    const auto old_range = segment->getRowKeyRange();

    SegmentPtr new_segment;
    // test split segment
    {
        std::tie(segment, new_segment) = segment->split(dmContext(), tableColumns());
    }
    // check segment range
    const auto s1_range = segment->getRowKeyRange();
    EXPECT_EQ(*s1_range.start.value, *old_range.start.value);
    const auto s2_range = new_segment->getRowKeyRange();
    EXPECT_EQ(*s2_range.start.value, *s1_range.end.value);
    EXPECT_EQ(*s2_range.end.value, *old_range.end.value);
    // TODO check segment epoch is increase

    size_t num_rows_seg1 = 0;
    size_t num_rows_seg2 = 0;
    {
        {
            auto in = segment->getInputStream(dmContext(), *tableColumns(), {segment->getRowKeyRange()});
            in->readPrefix();
            while (Block block = in->read())
            {
                num_rows_seg1 += block.rows();
            }
            in->readSuffix();
        }
        {
            auto in = new_segment->getInputStream(dmContext(), *tableColumns(), {new_segment->getRowKeyRange()});
            in->readPrefix();
            while (Block block = in->read())
            {
                num_rows_seg2 += block.rows();
            }
            in->readSuffix();
        }
        ASSERT_EQ(num_rows_seg1 + num_rows_seg2, num_rows_write);
    }

    // delete rows in the right segment
    {
        new_segment->write(dmContext(), /*delete_range*/ new_segment->getRowKeyRange());
        new_segment->flushCache(dmContext());
    }

    // merge segments
    {
        segment = Segment::merge(dmContext(), tableColumns(), segment, new_segment);
        {
            // check merged segment range
            const auto & merged_range = segment->getRowKeyRange();
            EXPECT_EQ(*merged_range.start.value, *s1_range.start.value);
            EXPECT_EQ(*merged_range.end.value, *s2_range.end.value);
            // TODO check segment epoch is increase
        }
        {
            size_t num_rows_read = 0;
            auto   in            = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
            in->readPrefix();
            while (Block block = in->read())
            {
                num_rows_read += block.rows();
            }
            in->readSuffix();
            EXPECT_EQ(num_rows_read, num_rows_seg1);
        }
    }
}
CATCH

TEST_F(Segment_test, SplitFail)
try
{
    const size_t num_rows_write = 100;
    {
        // write to segment
        Block block = DMTestEnv::prepareSimpleWriteBlock(0, num_rows_write, false);
        segment->write(dmContext(), std::move(block));
    }

    // Remove all data
    segment->write(dmContext(), RowKeyRange::fromHandleRange(HandleRange(0, 100)));
    segment->flushCache(dmContext());

    auto [a, b] = segment->split(dmContext(), tableColumns());
    EXPECT_EQ(a, SegmentPtr{});
    EXPECT_EQ(b, SegmentPtr{});
}
CATCH

TEST_F(Segment_test, Restore)
try
{
    // compare will compares the given segments.
    // If they are equal, result will be true, otherwise it will be false.
    auto compare = [&](const SegmentPtr & seg1, const SegmentPtr & seg2, bool & result) {
        result   = false;
        auto in1 = seg1->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        auto in2 = seg2->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        in1->readPrefix();
        in2->readPrefix();
        for (;;)
        {
            Block block1 = in1->read();
            Block block2 = in2->read();
            if (!block1)
            {
                ASSERT_TRUE(!block2);
                break;
            }

            ASSERT_EQ(block1.rows(), block2.rows());

            auto iter1 = block1.begin();
            auto iter2 = block2.begin();

            for (;;)
            {
                if (iter1 == block1.end())
                {
                    ASSERT_EQ(iter2, block2.end());
                    break;
                }

                auto c1 = iter1->column;
                auto c2 = iter2->column;

                ASSERT_EQ(c1->size(), c2->size());

                for (Int64 i = 0; i < Int64(c1->size()); i++)
                {
                    if (iter1->name == DMTestEnv::pk_name)
                    {
                        ASSERT_EQ(iter2->name, DMTestEnv::pk_name);
                        ASSERT_EQ(c1->getInt(i), c2->getInt(i));
                    }
                }

                // Call next
                iter1++;
                iter2++;
            }
        }
        in1->readSuffix();
        in2->readSuffix();

        result = true;
    };

    const size_t num_rows_write = 64;
    {
        Block block = DMTestEnv::prepareSimpleWriteBlock(0, num_rows_write, false);
        segment->write(dmContext(), std::move(block));
        // flush segment
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    SegmentPtr new_segment = Segment::restoreSegment(dmContext(), segment->segmentId());

    {
        // test compare
        bool result;
        compare(segment, new_segment, result);
        ASSERT_TRUE(result);
    }

    {
        // Do some update and restore again
        HandleRange del(0, 32);
        segment->write(dmContext(), {RowKeyRange::fromHandleRange(del)});
        new_segment = segment->restoreSegment(dmContext(), segment->segmentId());
    }

    {
        // test compare
        bool result;
        compare(new_segment, new_segment, result);
        ASSERT_TRUE(result);
    }
}
CATCH

TEST_F(Segment_test, MassiveSplit)
try
{
    Settings settings                    = dmContext().db_context.getSettings();
    settings.dt_segment_limit_rows       = 11;
    settings.dt_segment_delta_limit_rows = 7;

    segment = reload(DMTestEnv::getDefaultColumns(), std::move(settings));

    size_t       num_batches_written = 0;
    const size_t num_rows_per_write  = 5;

    const time_t start_time = std::time(nullptr);

    auto temp = std::vector<Int64>();
    for (;;)
    {
        {
            // Write to segment
            Block block = DMTestEnv::prepareSimpleWriteBlock( //
                num_batches_written * num_rows_per_write,     //
                num_batches_written * num_rows_per_write + num_rows_per_write,
                false);
            segment->write(dmContext(), std::move(block));
            num_batches_written += 1;
        }

        {
            // Delete some records so that the following condition can be satisfied:
            // if pk % 5 < 2, then the record would be deleted
            // if pk % 5 >= 2, then the record would be reserved
            HandleRange del{Int64((num_batches_written - 1) * num_rows_per_write),
                            Int64((num_batches_written - 1) * num_rows_per_write + 2)};
            segment->write(dmContext(), {RowKeyRange::fromHandleRange(del)});
        }

        {
            // flush segment
            segment = segment->mergeDelta(dmContext(), tableColumns());
            ;
        }

        for (size_t i = (num_batches_written - 1) * num_rows_per_write + 2; i < num_batches_written * num_rows_per_write; i++)
        {
            temp.push_back(Int64(i));
        }

        {
            // Read after writing
            auto   in            = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
            size_t num_rows_read = 0;
            in->readPrefix();
            while (Block block = in->read())
            {
                for (auto & iter : block)
                {
                    auto c = iter.column;
                    for (size_t i = 0; i < c->size(); i++)
                    {
                        if (iter.name == DMTestEnv::pk_name)
                        {
                            auto expect = temp.at(i + num_rows_read);
                            EXPECT_EQ(c->getInt(Int64(i)), expect);
                        }
                    }
                }
                num_rows_read += block.rows();
            }
            in->readSuffix();
            ASSERT_EQ(num_batches_written * (num_rows_per_write - 2), num_rows_read);
        }

        {
            // Run for long enough to make sure Split is robust.
            const time_t end_time = std::time(nullptr);
            // if ((end_time - start_time) / 60 > 10)
            if ((end_time - start_time) > 10)
            {
                return;
            }
        }
    }
}
CATCH

enum Segment_test_Mode
{
    V1_BlockOnly,
    V2_BlockOnly,
    V2_FileOnly,
};

String testModeToString(const ::testing::TestParamInfo<Segment_test_Mode> & info)
{
    const auto mode = info.param;
    switch (mode)
    {
    case Segment_test_Mode::V1_BlockOnly:
        return "V1_BlockOnly";
    case Segment_test_Mode::V2_BlockOnly:
        return "V2_BlockOnly";
    case Segment_test_Mode::V2_FileOnly:
        return "V2_FileOnly";
    default:
        return "Unknown";
    }
}

class Segment_test_2 : public Segment_test, public testing::WithParamInterface<Segment_test_Mode>
{
public:
    Segment_test_2() : Segment_test() {}

    void SetUp() override
    {
        mode = GetParam();
        switch (mode)
        {
        case Segment_test_Mode::V1_BlockOnly:
            setStorageFormat(1);
            break;
        case Segment_test_Mode::V2_BlockOnly:
        case Segment_test_Mode::V2_FileOnly:
            setStorageFormat(2);
            break;
        }

        Segment_test::SetUp();
    }

    std::pair<RowKeyRange, std::vector<PageId>> genDMFile(DMContext & context, const Block & block)
    {
        auto delegator    = context.path_pool.getStableDiskDelegator();
        auto file_id      = context.storage_pool.newDataPageIdForDTFile(delegator, __PRETTY_FUNCTION__);
        auto input_stream = std::make_shared<OneBlockInputStream>(block);
        auto store_path   = delegator.choosePath();

        auto dmfile
            = writeIntoNewDMFile(context, std::make_shared<ColumnDefines>(*tableColumns()), input_stream, file_id, store_path, false);

        delegator.addDTFile(file_id, dmfile->getBytesOnDisk(), store_path);

        auto &      pk_column = block.getByPosition(0).column;
        auto        min_pk    = pk_column->getInt(0);
        auto        max_pk    = pk_column->getInt(block.rows() - 1);
        HandleRange range(min_pk, max_pk + 1);

        return {RowKeyRange::fromHandleRange(range), {file_id}};
    }

    Segment_test_Mode mode;
};

TEST_P(Segment_test_2, FlushDuringSplitAndMerge)
try
{
    size_t row_offset     = 0;
    auto   write_100_rows = [&, this](const SegmentPtr & segment) {
        {
            // write to segment
            Block block = DMTestEnv::prepareSimpleWriteBlock(row_offset, row_offset + 100, false);
            row_offset += 100;
            switch (mode)
            {
            case Segment_test_Mode::V1_BlockOnly:
            case Segment_test_Mode::V2_BlockOnly:
                segment->write(dmContext(), std::move(block));
                break;
            case Segment_test_Mode::V2_FileOnly: {
                auto delegate                 = dmContext().path_pool.getStableDiskDelegator();
                auto file_provider            = dmContext().db_context.getFileProvider();
                auto [range, file_ids]        = genDMFile(dmContext(), block);
                auto         file_id          = file_ids[0];
                auto         file_parent_path = delegate.getDTFilePath(file_id);
                auto         file             = DMFile::restore(file_provider, file_id, file_id, file_parent_path);
                auto         pack             = std::make_shared<DeltaPackFile>(dmContext(), file, range);
                WriteBatches wbs(*storage_pool);
                wbs.data.putExternal(file_id, 0);
                wbs.writeLogAndData();

                segment->writeRegionSnapshot(dmContext(), range, {pack}, false);
                break;
            }
            default:
                throw Exception("Unsupported");
            }

            segment->flushCache(dmContext());
        }
    };

    auto read_rows = [&](const SegmentPtr & segment) {
        size_t rows = 0;
        auto   in   = segment->getInputStream(dmContext(), *tableColumns(), {RowKeyRange::newAll(false, 1)});
        in->readPrefix();
        while (Block block = in->read())
        {
            rows += block.rows();
        }
        in->readSuffix();
        return rows;
    };

    write_100_rows(segment);

    // Test split
    SegmentPtr other_segment;
    {
        WriteBatches wbs(dmContext().storage_pool);
        auto         segment_snap = segment->createSnapshot(dmContext(), true, CurrentMetrics::DT_SnapshotOfSegmentSplit);
        ASSERT_FALSE(!segment_snap);

        write_100_rows(segment);

        auto split_info = segment->prepareSplit(dmContext(), tableColumns(), segment_snap, wbs, false);

        wbs.writeLogAndData();
        split_info->my_stable->enableDMFilesGC();
        split_info->other_stable->enableDMFilesGC();

        auto lock                        = segment->mustGetUpdateLock();
        std::tie(segment, other_segment) = segment->applySplit(dmContext(), segment_snap, wbs, split_info.value());

        wbs.writeAll();
    }

    {
        SegmentPtr new_segment_1 = Segment::restoreSegment(dmContext(), segment->segmentId());
        SegmentPtr new_segment_2 = Segment::restoreSegment(dmContext(), other_segment->segmentId());
        auto       rows1         = read_rows(new_segment_1);
        auto       rows2         = read_rows(new_segment_2);
        ASSERT_EQ(rows1 + rows2, (size_t)200);
    }

    // Test merge
    {
        WriteBatches wbs(dmContext().storage_pool);

        auto left_snap  = segment->createSnapshot(dmContext(), true, CurrentMetrics::DT_SnapshotOfSegmentMerge);
        auto right_snap = other_segment->createSnapshot(dmContext(), true, CurrentMetrics::DT_SnapshotOfSegmentMerge);
        ASSERT_FALSE(!left_snap || !right_snap);

        write_100_rows(other_segment);
        segment->flushCache(dmContext());

        auto merged_stable = Segment::prepareMerge(dmContext(), tableColumns(), segment, left_snap, other_segment, right_snap, wbs, false);

        wbs.writeLogAndData();
        merged_stable->enableDMFilesGC();

        auto left_lock  = segment->mustGetUpdateLock();
        auto right_lock = other_segment->mustGetUpdateLock();

        segment = Segment::applyMerge(dmContext(), segment, left_snap, other_segment, right_snap, wbs, merged_stable);

        wbs.writeAll();
    }

    {
        SegmentPtr new_segment = Segment::restoreSegment(dmContext(), segment->segmentId());
        auto       rows        = read_rows(new_segment);
        ASSERT_EQ(rows, (size_t)300);
    }
}
CATCH

INSTANTIATE_TEST_CASE_P(Segment_test_Mode, //
                        Segment_test_2,
                        testing::Values(Segment_test_Mode::V1_BlockOnly, Segment_test_Mode::V2_BlockOnly, Segment_test_Mode::V2_FileOnly),
                        testModeToString);

enum class SegmentWriteType
{
    ToDisk,
    ToCache
};
class Segment_DDL_test : public Segment_test, //
                         public testing::WithParamInterface<std::tuple<SegmentWriteType, bool>>
{
};
String paramToString(const ::testing::TestParamInfo<Segment_DDL_test::ParamType> & info)
{
    const auto [write_type, flush_before_ddl] = info.param;

    String name = (write_type == SegmentWriteType::ToDisk) ? "ToDisk_" : "ToCache";
    name += (flush_before_ddl ? "_FlushCache" : "_NotFlushCache");
    return name;
}

/// Mock a col from i8 -> i32
TEST_P(Segment_DDL_test, AlterInt8ToInt32)
try
{
    const String       column_name_i8_to_i32 = "i8_to_i32";
    const ColumnID     column_id_i8_to_i32   = 4;
    const ColumnDefine column_i8_before_ddl(column_id_i8_to_i32, column_name_i8_to_i32, typeFromString("Int8"));
    const ColumnDefine column_i32_after_ddl(column_id_i8_to_i32, column_name_i8_to_i32, typeFromString("Int32"));

    const auto [write_type, flush_before_ddl] = GetParam();

    // Prepare some data before ddl
    const size_t num_rows_write = 100;
    {
        /// set columns before ddl
        auto columns_before_ddl = DMTestEnv::getDefaultColumns();
        columns_before_ddl->emplace_back(column_i8_before_ddl);
        DB::Settings db_settings;
        segment = reload(columns_before_ddl, std::move(db_settings));

        /// write to segment
        Block block = DMTestEnv::prepareSimpleWriteBlock(0, num_rows_write, false);
        // add int8_col and later read it as int32
        // (mock ddl change int8 -> int32)
        const size_t          num_rows = block.rows();
        ColumnWithTypeAndName int8_col(nullptr, column_i8_before_ddl.type, column_i8_before_ddl.name, column_id_i8_to_i32);
        {
            IColumn::MutablePtr m_col       = int8_col.type->createColumn();
            auto &              column_data = typeid_cast<ColumnVector<Int8> &>(*m_col).getData();
            column_data.resize(num_rows);
            for (size_t i = 0; i < num_rows; ++i)
            {
                column_data[i] = static_cast<int8_t>(-1 * (i % 2 ? 1 : -1) * i);
            }
            int8_col.column = std::move(m_col);
        }
        block.insert(int8_col);
        switch (write_type)
        {
        case SegmentWriteType::ToDisk:
            segment->write(dmContext(), std::move(block));
            break;
        case SegmentWriteType::ToCache:
            segment->writeToCache(dmContext(), block, 0, num_rows_write);
            break;
        }
    }

    ColumnDefinesPtr columns_to_read = std::make_shared<ColumnDefines>();
    {
        *columns_to_read = *DMTestEnv::getDefaultColumns();
        columns_to_read->emplace_back(column_i32_after_ddl);
        if (flush_before_ddl)
        {
            segment->flushCache(dmContext());
        }
        setColumns(columns_to_read);
    }

    {
        // read written data
        auto in = segment->getInputStream(dmContext(), *columns_to_read, {RowKeyRange::newAll(false, 1)});

        // check that we can read correct values
        size_t num_rows_read = 0;
        in->readPrefix();
        while (Block block = in->read())
        {
            num_rows_read += block.rows();
            ASSERT_TRUE(block.has(column_name_i8_to_i32));
            const ColumnWithTypeAndName & col = block.getByName(column_name_i8_to_i32);
            ASSERT_DATATYPE_EQ(col.type, column_i32_after_ddl.type);
            ASSERT_EQ(col.name, column_i32_after_ddl.name);
            ASSERT_EQ(col.column_id, column_i32_after_ddl.id);
            for (size_t i = 0; i < block.rows(); ++i)
            {
                auto       value    = col.column->getInt(i);
                const auto expected = static_cast<int64_t>(-1 * (i % 2 ? 1 : -1) * i);
                ASSERT_EQ(value, expected) << "at row: " << i;
            }
        }
        in->readSuffix();
        ASSERT_EQ(num_rows_read, num_rows_write);
    }


    /// Write some data after ddl, replacing som origin rows
    {
        /// write to segment, replacing some origin rows
        Block block = DMTestEnv::prepareSimpleWriteBlock(num_rows_write / 2, num_rows_write * 2, false, /* tso= */ 3);

        const size_t          num_rows = block.rows();
        ColumnWithTypeAndName int32_col(nullptr, column_i32_after_ddl.type, column_i32_after_ddl.name, column_id_i8_to_i32);
        {
            IColumn::MutablePtr m_col       = int32_col.type->createColumn();
            auto &              column_data = typeid_cast<ColumnVector<Int32> &>(*m_col).getData();
            column_data.resize(num_rows);
            for (size_t i = 0; i < num_rows; ++i)
            {
                column_data[i] = static_cast<int32_t>(-1 * (i % 2 ? 1 : -1) * i);
            }
            int32_col.column = std::move(m_col);
        }
        block.insert(int32_col);
        switch (write_type)
        {
        case SegmentWriteType::ToDisk:
            segment->write(dmContext(), std::move(block));
            break;
        case SegmentWriteType::ToCache:
            segment->writeToCache(dmContext(), block, 0, num_rows);
            break;
        }
    }

    {
        // read written data
        auto in = segment->getInputStream(dmContext(), *columns_to_read, {RowKeyRange::newAll(false, 1)});

        // check that we can read correct values
        size_t num_rows_read = 0;
        in->readPrefix();
        while (Block block = in->read())
        {
            num_rows_read += block.rows();
            ASSERT_TRUE(block.has(column_name_i8_to_i32));
            const ColumnWithTypeAndName & col = block.getByName(column_name_i8_to_i32);
            ASSERT_DATATYPE_EQ(col.type, column_i32_after_ddl.type);
            ASSERT_EQ(col.name, column_i32_after_ddl.name);
            ASSERT_EQ(col.column_id, column_i32_after_ddl.id);
            for (size_t i = 0; i < block.rows(); ++i)
            {
                auto value    = col.column->getInt(i);
                auto expected = 0;
                if (i < num_rows_write / 2)
                    expected = static_cast<int64_t>(-1 * (i % 2 ? 1 : -1) * i);
                else
                {
                    auto r   = i - num_rows_write / 2;
                    expected = static_cast<int64_t>(-1 * (r % 2 ? 1 : -1) * r);
                }
                // std::cerr << " row: " << i << "  "<< value << std::endl;
                ASSERT_EQ(value, expected) << "at row: " << i;
            }
        }
        in->readSuffix();
        ASSERT_EQ(num_rows_read, (size_t)(num_rows_write * 2));
    }

    // Flush cache and apply delta-merge, then read again
    // This will create a new stable with new schema, check the data.
    {
        segment->flushCache(dmContext());
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    {
        // check the stable data with new schema
        auto in = segment->getInputStream(dmContext(), *columns_to_read, {RowKeyRange::newAll(false, 1)});

        // check that we can read correct values
        size_t num_rows_read = 0;
        in->readPrefix();
        while (Block block = in->read())
        {
            num_rows_read += block.rows();
            ASSERT_TRUE(block.has(column_name_i8_to_i32));
            const ColumnWithTypeAndName & col = block.getByName(column_name_i8_to_i32);
            ASSERT_DATATYPE_EQ(col.type, column_i32_after_ddl.type);
            ASSERT_EQ(col.name, column_i32_after_ddl.name);
            ASSERT_EQ(col.column_id, column_i32_after_ddl.id);
            for (size_t i = 0; i < block.rows(); ++i)
            {
                auto value    = col.column->getInt(i);
                auto expected = 0;
                if (i < num_rows_write / 2)
                    expected = static_cast<int64_t>(-1 * (i % 2 ? 1 : -1) * i);
                else
                {
                    auto r   = i - num_rows_write / 2;
                    expected = static_cast<int64_t>(-1 * (r % 2 ? 1 : -1) * r);
                }
                // std::cerr << " row: " << i << "  "<< value << std::endl;
                ASSERT_EQ(value, expected) << "at row: " << i;
            }
        }
        in->readSuffix();
        ASSERT_EQ(num_rows_read, (size_t)(num_rows_write * 2));
    }
}
CATCH

TEST_P(Segment_DDL_test, AddColumn)
try
{
    const String   new_column_name = "i8";
    const ColumnID new_column_id   = 4;
    ColumnDefine   new_column_define(new_column_id, new_column_name, DataTypeFactory::instance().get("Int8"));
    const Int8     new_column_default_value_int = 16;
    new_column_define.default_value             = toField(new_column_default_value_int);

    const auto [write_type, flush_before_ddl] = GetParam();

    {
        auto columns_before_ddl = DMTestEnv::getDefaultColumns();
        // Not cache any rows
        DB::Settings db_settings;
        segment = reload(columns_before_ddl, std::move(db_settings));
    }

    const size_t num_rows_write = 100;
    {
        // write to segment
        Block block = DMTestEnv::prepareSimpleWriteBlock(0, num_rows_write, false);
        switch (write_type)
        {
        case SegmentWriteType::ToDisk:
            segment->write(dmContext(), std::move(block));
            break;
        case SegmentWriteType::ToCache:
            segment->writeToCache(dmContext(), block, 0, num_rows_write);
            break;
        }
    }

    auto columns_after_ddl = DMTestEnv::getDefaultColumns();
    {
        // DDL add new column with default value
        columns_after_ddl->emplace_back(new_column_define);
        if (flush_before_ddl)
        {
            // If write to cache, before apply ddl changes (change column data type), segment->flushCache must be called.
            segment->flushCache(dmContext());
        }
        setColumns(columns_after_ddl);
    }

    {
        // read written data
        auto in = segment->getInputStream(dmContext(), *columns_after_ddl, {RowKeyRange::newAll(false, 1)});

        // check that we can read correct values
        size_t num_rows_read = 0;
        in->readPrefix();
        while (Block block = in->read())
        {
            num_rows_read += block.rows();
            const ColumnWithTypeAndName & col = block.getByName(new_column_define.name);
            ASSERT_TRUE(col.type->equals(*new_column_define.type));
            ASSERT_EQ(col.name, new_column_define.name);
            ASSERT_EQ(col.column_id, new_column_define.id);
            for (size_t i = 0; i < block.rows(); ++i)
            {
                auto value = col.column->getInt(i);
                ASSERT_EQ(value, new_column_default_value_int) << "at row:" << i;
            }
        }
        in->readSuffix();
        ASSERT_EQ(num_rows_read, num_rows_write);
    }


    /// Write some data after ddl, replacing som origin rows
    {
        /// write to segment, replacing some origin rows
        Block block = DMTestEnv::prepareSimpleWriteBlock(num_rows_write / 2, num_rows_write * 2, false, /* tso= */ 3);

        const size_t          num_rows = block.rows();
        ColumnWithTypeAndName int8_col(
            nullptr, new_column_define.type, new_column_define.name, new_column_id, new_column_define.default_value);
        {
            IColumn::MutablePtr m_col       = int8_col.type->createColumn();
            auto &              column_data = typeid_cast<ColumnVector<Int8> &>(*m_col).getData();
            column_data.resize(num_rows);
            for (size_t i = 0; i < num_rows; ++i)
            {
                column_data[i] = static_cast<int8_t>(-1 * (i % 2 ? 1 : -1) * i);
            }
            int8_col.column = std::move(m_col);
        }
        block.insert(int8_col);
        switch (write_type)
        {
        case SegmentWriteType::ToDisk:
            segment->write(dmContext(), std::move(block));
            break;
        case SegmentWriteType::ToCache:
            segment->writeToCache(dmContext(), block, 0, num_rows);
            break;
        }
    }

    {
        // read written data
        auto in = segment->getInputStream(dmContext(), *columns_after_ddl, {RowKeyRange::newAll(false, 1)});

        // check that we can read correct values
        size_t num_rows_read = 0;
        in->readPrefix();
        while (Block block = in->read())
        {
            num_rows_read += block.rows();
            ASSERT_TRUE(block.has(new_column_name));
            const ColumnWithTypeAndName & col = block.getByName(new_column_name);
            ASSERT_DATATYPE_EQ(col.type, new_column_define.type);
            ASSERT_EQ(col.name, new_column_define.name);
            ASSERT_EQ(col.column_id, new_column_define.id);
            for (size_t i = 0; i < block.rows(); ++i)
            {
                int8_t value    = col.column->getInt(i);
                int8_t expected = 0;
                if (i < num_rows_write / 2)
                    expected = new_column_default_value_int;
                else
                {
                    auto r   = i - num_rows_write / 2;
                    expected = static_cast<int8_t>(-1 * (r % 2 ? 1 : -1) * r);
                }
                // std::cerr << " row: " << i << "  "<< value << std::endl;
                ASSERT_EQ(value, expected) << "at row: " << i;
            }
        }
        in->readSuffix();
        ASSERT_EQ(num_rows_read, (size_t)(num_rows_write * 2));
    }

    // Flush cache and apply delta-merge, then read again
    // This will create a new stable with new schema, check the data.
    {
        segment->flushCache(dmContext());
        segment = segment->mergeDelta(dmContext(), tableColumns());
        ;
    }

    {
        // read written data after delta-merge
        auto in = segment->getInputStream(dmContext(), *columns_after_ddl, {RowKeyRange::newAll(false, 1)});

        // check that we can read correct values
        size_t num_rows_read = 0;
        in->readPrefix();
        while (Block block = in->read())
        {
            num_rows_read += block.rows();
            ASSERT_TRUE(block.has(new_column_name));
            const ColumnWithTypeAndName & col = block.getByName(new_column_name);
            ASSERT_DATATYPE_EQ(col.type, new_column_define.type);
            ASSERT_EQ(col.name, new_column_define.name);
            ASSERT_EQ(col.column_id, new_column_define.id);
            for (size_t i = 0; i < block.rows(); ++i)
            {
                int8_t value    = col.column->getInt(i);
                int8_t expected = 0;
                if (i < num_rows_write / 2)
                    expected = new_column_default_value_int;
                else
                {
                    auto r   = i - num_rows_write / 2;
                    expected = static_cast<int8_t>(-1 * (r % 2 ? 1 : -1) * r);
                }
                // std::cerr << " row: " << i << "  "<< value << std::endl;
                ASSERT_EQ(value, expected) << "at row: " << i;
            }
        }
        in->readSuffix();
        ASSERT_EQ(num_rows_read, (size_t)(num_rows_write * 2));
    }
}
CATCH

INSTANTIATE_TEST_CASE_P(SegmentWriteType,
                        Segment_DDL_test,
                        ::testing::Combine( //
                            ::testing::Values(SegmentWriteType::ToDisk, SegmentWriteType::ToCache),
                            ::testing::Bool()),
                        paramToString);

} // namespace tests
} // namespace DM
} // namespace DB
