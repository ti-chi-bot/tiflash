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

#include <Poco/Logger.h>
#include <Storages/Page/V3/LogFile/LogFilename.h>
#include <Storages/Page/V3/LogFile/LogFormat.h>
#include <Storages/Page/V3/PageDirectory.h>
#include <Storages/Page/V3/PageDirectoryFactory.h>
#include <Storages/Page/V3/PageEntriesEdit.h>
#include <Storages/Page/V3/PageEntry.h>
#include <Storages/Page/V3/WAL/WALReader.h>
#include <Storages/Page/V3/WAL/serialize.h>
#include <Storages/Page/V3/WALStore.h>
#include <Storages/Page/V3/tests/entries_helper.h>
#include <Storages/tests/TiFlashStorageTestBasic.h>
#include <TestUtils/MockDiskDelegator.h>
#include <TestUtils/TiFlashTestEnv.h>

#include <random>

namespace DB::PS::V3::tests
{
TEST(WALSeriTest, AllPuts)
{
    PageEntryV3 entry_p1{.file_id = 1, .size = 1, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageEntryV3 entry_p2{.file_id = 1, .size = 2, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageVersionType ver20(/*seq=*/20);
    PageEntriesEdit edit;
    edit.put(1, entry_p1);
    edit.put(2, entry_p2);

    for (auto & rec : edit.getMutRecords())
        rec.version = ver20;

    auto deseri_edit = DB::PS::V3::ser::deserializeFrom(DB::PS::V3::ser::serializeTo(edit));
    ASSERT_EQ(deseri_edit.size(), 2);
    auto iter = deseri_edit.getRecords().begin();
    EXPECT_EQ(iter->type, EditRecordType::PUT);
    EXPECT_EQ(iter->page_id.low, 1);
    EXPECT_EQ(iter->version, ver20);
    EXPECT_SAME_ENTRY(iter->entry, entry_p1);
}

TEST(WALSeriTest, PutsAndRefsAndDels)
try
{
    PageEntryV3 entry_p3{.file_id = 1, .size = 3, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageEntryV3 entry_p5{.file_id = 1, .size = 5, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageVersionType ver21(/*seq=*/21);
    PageEntriesEdit edit;
    edit.put(3, entry_p3);
    edit.ref(4, 3);
    edit.put(5, entry_p5);
    edit.del(2);
    edit.del(1);
    edit.del(987);

    for (auto & rec : edit.getMutRecords())
        rec.version = ver21;

    auto deseri_edit = DB::PS::V3::ser::deserializeFrom(DB::PS::V3::ser::serializeTo(edit));
    ASSERT_EQ(deseri_edit.size(), 6);
    auto iter = deseri_edit.getRecords().begin();
    EXPECT_EQ(iter->type, EditRecordType::PUT);
    EXPECT_EQ(iter->page_id.low, 3);
    EXPECT_EQ(iter->version, ver21);
    EXPECT_SAME_ENTRY(iter->entry, entry_p3);
    iter++;
    EXPECT_EQ(iter->type, EditRecordType::REF);
    EXPECT_EQ(iter->page_id.low, 4);
    EXPECT_EQ(iter->version, ver21);
    EXPECT_EQ(iter->entry.file_id, INVALID_BLOBFILE_ID);
    iter++;
    EXPECT_EQ(iter->type, EditRecordType::PUT);
    EXPECT_EQ(iter->page_id.low, 5);
    EXPECT_EQ(iter->version, ver21);
    EXPECT_SAME_ENTRY(iter->entry, entry_p5);
    iter++;
    EXPECT_EQ(iter->type, EditRecordType::DEL);
    EXPECT_EQ(iter->page_id.low, 2);
    EXPECT_EQ(iter->version, ver21);
    iter++;
    EXPECT_EQ(iter->type, EditRecordType::DEL);
    EXPECT_EQ(iter->page_id.low, 1);
    EXPECT_EQ(iter->version, ver21);
    iter++;
    EXPECT_EQ(iter->type, EditRecordType::DEL);
    EXPECT_EQ(iter->page_id.low, 987);
    EXPECT_EQ(iter->version, ver21);
}
CATCH

TEST(WALSeriTest, Upserts)
{
    PageEntryV3 entry_p1_2{.file_id = 2, .size = 1, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageEntryV3 entry_p3_2{.file_id = 2, .size = 3, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageEntryV3 entry_p5_2{.file_id = 2, .size = 5, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageVersionType ver20_1(/*seq=*/20, /*epoch*/ 1);
    PageVersionType ver21_1(/*seq=*/21, /*epoch*/ 1);
    PageEntriesEdit edit;
    edit.upsertPage(1, ver20_1, entry_p1_2);
    edit.upsertPage(3, ver21_1, entry_p3_2);
    edit.upsertPage(5, ver21_1, entry_p5_2);

    auto deseri_edit = DB::PS::V3::ser::deserializeFrom(DB::PS::V3::ser::serializeTo(edit));
    ASSERT_EQ(deseri_edit.size(), 3);
    auto iter = deseri_edit.getRecords().begin();
    EXPECT_EQ(iter->type, EditRecordType::UPSERT);
    EXPECT_EQ(iter->page_id.low, 1);
    EXPECT_EQ(iter->version, ver20_1);
    EXPECT_SAME_ENTRY(iter->entry, entry_p1_2);
    iter++;
    EXPECT_EQ(iter->type, EditRecordType::UPSERT);
    EXPECT_EQ(iter->page_id.low, 3);
    EXPECT_EQ(iter->version, ver21_1);
    EXPECT_SAME_ENTRY(iter->entry, entry_p3_2);
    iter++;
    EXPECT_EQ(iter->type, EditRecordType::UPSERT);
    EXPECT_EQ(iter->page_id.low, 5);
    EXPECT_EQ(iter->version, ver21_1);
    EXPECT_SAME_ENTRY(iter->entry, entry_p5_2);
}

TEST(WALLognameTest, parsing)
{
    LoggerPtr log = Logger::get("WALLognameTest");
    const String parent_path("/data1");

    {
        LogFilename f = LogFilename::parseFrom(parent_path, "log_1_2", log);
        EXPECT_EQ(f.parent_path, parent_path);
        EXPECT_EQ(f.log_num, 1);
        EXPECT_EQ(f.level_num, 2);
        EXPECT_EQ(f.stage, LogFileStage::Normal);

        EXPECT_EQ(f.filename(LogFileStage::Temporary), ".temp.log_1_2");
        EXPECT_EQ(f.fullname(LogFileStage::Temporary), "/data1/.temp.log_1_2");
        EXPECT_EQ(f.filename(LogFileStage::Normal), "log_1_2");
        EXPECT_EQ(f.fullname(LogFileStage::Normal), "/data1/log_1_2");
    }

    {
        LogFilename f = LogFilename::parseFrom(parent_path, ".temp.log_345_78", log);
        EXPECT_EQ(f.parent_path, parent_path);
        EXPECT_EQ(f.log_num, 345);
        EXPECT_EQ(f.level_num, 78);
        EXPECT_EQ(f.stage, LogFileStage::Temporary);

        EXPECT_EQ(f.filename(LogFileStage::Temporary), ".temp.log_345_78");
        EXPECT_EQ(f.fullname(LogFileStage::Temporary), "/data1/.temp.log_345_78");
        EXPECT_EQ(f.filename(LogFileStage::Normal), "log_345_78");
        EXPECT_EQ(f.fullname(LogFileStage::Normal), "/data1/log_345_78");
    }

    for (const auto & n : Strings{
             "something_wrong",
             "log_1_2_3",
             ".temp.log_1_2_3",
             "log_1",
             ".temp.log_1",
             "log_abc_def",
             ".temp.log_abc_def",
         })
    {
        LogFilename f = LogFilename::parseFrom(parent_path, n, log);
        EXPECT_EQ(f.stage, LogFileStage::Invalid) << n;
    }
}

TEST(WALLognameSetTest, ordering)
{
    LoggerPtr log = Logger::get("WALLognameTest");
    const String parent_path("/data1");

    LogFilenameSet filenames;
    for (const auto & n : Strings{
             "log_2_1",
             "log_2_0",
             ".temp.log_2_1", // ignored since we have inserted "log_2_1"
             "log_1_2",
             ".temp.log_1_3",
         })
    {
        filenames.insert(LogFilename::parseFrom(parent_path, n, log));
    }
    ASSERT_EQ(filenames.size(), 4);
    auto iter = filenames.begin();
    EXPECT_EQ(iter->log_num, 1);
    EXPECT_EQ(iter->level_num, 2);
    ++iter;
    EXPECT_EQ(iter->log_num, 1);
    EXPECT_EQ(iter->level_num, 3);
    ++iter;
    EXPECT_EQ(iter->log_num, 2);
    EXPECT_EQ(iter->level_num, 0);
    ++iter;
    EXPECT_EQ(iter->log_num, 2);
    EXPECT_EQ(iter->level_num, 1);

    ++iter;
    EXPECT_EQ(iter, filenames.end());
}


class WALStoreTest
    : public DB::base::TiFlashStorageTestBasic
    , public testing::WithParamInterface<bool>
{
public:
    WALStoreTest()
        : multi_paths(GetParam())
    {
    }

    void SetUp() override
    {
        auto path = getTemporaryPath();
        dropDataOnDisk(path);

        if (!multi_paths)
        {
            delegator = std::make_shared<DB::tests::MockDiskDelegatorSingle>(getTemporaryPath());
        }
        else
        {
            // mock 8 dirs for multi-paths
            Strings paths;
            for (size_t i = 0; i < 8; ++i)
            {
                paths.emplace_back(fmt::format("{}/path_{}", path, i));
            }
            delegator = std::make_shared<DB::tests::MockDiskDelegatorMulti>(paths);
        }
    }

private:
    const bool multi_paths;

protected:
    PSDiskDelegatorPtr delegator;
    WALStore::Config config;
};

TEST_P(WALStoreTest, FindCheckpointFile)
{
    LoggerPtr log = Logger::get("WALLognameTest");
    auto path = getTemporaryPath();

    {
        // no checkpoint
        LogFilenameSet files{
            LogFilename::parseFrom(path, "log_1_0", log),
            LogFilename::parseFrom(path, "log_2_0", log),
            LogFilename::parseFrom(path, "log_3_0", log),
            LogFilename::parseFrom(path, "log_4_0", log),
        };
        auto [cp, files_to_read] = WALStoreReader::findCheckpoint(std::move(files));
        ASSERT_FALSE(cp.has_value());
        EXPECT_EQ(files_to_read.size(), 4);
    }

    {
        // checkpoint and some other logfiles
        LogFilenameSet files{
            LogFilename::parseFrom(path, "log_12_1", log),
            LogFilename::parseFrom(path, "log_13_0", log),
            LogFilename::parseFrom(path, "log_14_0", log),
        };
        auto [cp, files_to_read] = WALStoreReader::findCheckpoint(std::move(files));
        ASSERT_TRUE(cp.has_value());
        EXPECT_EQ(cp->log_num, 12);
        EXPECT_EQ(cp->level_num, 1);
        EXPECT_EQ(files_to_read.size(), 2);
    }

    {
        // some files before checkpoint left on disk
        LogFilenameSet files{
            LogFilename::parseFrom(path, "log_10_0", log),
            LogFilename::parseFrom(path, "log_11_0", log),
            LogFilename::parseFrom(path, "log_12_0", log),
            LogFilename::parseFrom(path, "log_12_1", log),
            LogFilename::parseFrom(path, "log_13_0", log),
            LogFilename::parseFrom(path, "log_14_0", log),
        };
        auto [cp, files_to_read] = WALStoreReader::findCheckpoint(std::move(files));
        ASSERT_TRUE(cp.has_value());
        EXPECT_EQ(cp->log_num, 12);
        EXPECT_EQ(cp->level_num, 1);
        EXPECT_EQ(files_to_read.size(), 2);
    }
}

TEST_P(WALStoreTest, Empty)
{
    auto ctx = DB::tests::TiFlashTestEnv::getContext();
    auto provider = ctx.getFileProvider();
    auto path = getTemporaryPath();
    size_t num_callback_called = 0;
    auto [wal, reader] = WALStore::create(getCurrentTestName(), provider, delegator, config);
    ASSERT_NE(wal, nullptr);
    while (reader->remained())
    {
        auto [ok, edit] = reader->next();
        (void)edit;
        if (!ok)
        {
            reader->throwIfError();
            // else it just run to the end of file.
            break;
        }
        num_callback_called += 1;
    }
    ASSERT_EQ(num_callback_called, 0);
}

TEST_P(WALStoreTest, ReadWriteRestore)
try
{
    auto ctx = DB::tests::TiFlashTestEnv::getContext();
    auto provider = ctx.getFileProvider();
    auto path = getTemporaryPath();

    // Stage 1. empty
    std::vector<size_t> size_each_edit;
    auto [wal, reader] = WALStore::create(getCurrentTestName(), provider, delegator, config);
    {
        size_t num_applied_edit = 0;
        auto reader = WALStoreReader::create(getCurrentTestName(), provider, delegator);
        for (; reader->remained(); reader->next())
        {
            num_applied_edit += 1;
        }
        EXPECT_EQ(num_applied_edit, 0);
        EXPECT_EQ(reader->lastLogNum(), 0);
    }
    ASSERT_NE(wal, nullptr);

    // Stage 2. Apply with only puts
    PageEntryV3 entry_p1{.file_id = 1, .size = 1, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageEntryV3 entry_p2{.file_id = 1, .size = 2, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageVersionType ver20(/*seq=*/20);
    {
        PageEntriesEdit edit;
        edit.put(1, entry_p1);
        edit.put(2, entry_p2);
        size_each_edit.emplace_back(edit.size());
        wal->apply(edit, ver20);
    }

    wal.reset();
    reader.reset();

    std::tie(wal, reader) = WALStore::create(getCurrentTestName(), provider, delegator, config);
    {
        size_t num_applied_edit = 0;
        while (reader->remained())
        {
            const auto & [ok, edit] = reader->next();
            if (!ok)
                break;
            // Details of each edit is verified in `WALSeriTest`
            EXPECT_EQ(size_each_edit[num_applied_edit], edit.size());
            num_applied_edit += 1;
        }
        EXPECT_EQ(num_applied_edit, 1);
    }

    // Stage 3. Apply with puts and refs
    PageEntryV3 entry_p3{.file_id = 1, .size = 3, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageEntryV3 entry_p5{.file_id = 1, .size = 5, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageVersionType ver21(/*seq=*/21);
    {
        PageEntriesEdit edit;
        edit.put(3, entry_p3);
        edit.ref(4, 3);
        edit.put(5, entry_p5);
        edit.del(2);
        size_each_edit.emplace_back(edit.size());
        wal->apply(edit, ver21);
    }

    wal.reset();
    reader.reset();

    std::tie(wal, reader) = WALStore::create(getCurrentTestName(), provider, delegator, config);
    {
        size_t num_applied_edit = 0;
        while (reader->remained())
        {
            const auto & [ok, edit] = reader->next();
            if (!ok)
                break;
            // Details of each edit is verified in `WALSeriTest`
            EXPECT_EQ(size_each_edit[num_applied_edit], edit.size());
            num_applied_edit += 1;
        }
        EXPECT_EQ(num_applied_edit, 2);
    }


    // Stage 4. Apply with delete and upsert
    PageEntryV3 entry_p1_2{.file_id = 2, .size = 1, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageEntryV3 entry_p3_2{.file_id = 2, .size = 3, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageEntryV3 entry_p5_2{.file_id = 2, .size = 5, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageVersionType ver20_1(/*seq=*/20, /*epoch*/ 1);
    PageVersionType ver21_1(/*seq=*/21, /*epoch*/ 1);
    {
        PageEntriesEdit edit;
        edit.upsertPage(1, ver20_1, entry_p1_2);
        edit.upsertPage(3, ver21_1, entry_p3_2);
        edit.upsertPage(5, ver21_1, entry_p5_2);
        size_each_edit.emplace_back(edit.size());
        wal->apply(edit);
    }

    wal.reset();
    reader.reset();

    {
        size_t num_applied_edit = 0;
        auto reader = WALStoreReader::create(getCurrentTestName(), provider, delegator);
        while (reader->remained())
        {
            const auto & [ok, edit] = reader->next();
            if (!ok)
                break;
            // Details of each edit is verified in `WALSeriTest`
            EXPECT_EQ(size_each_edit[num_applied_edit], edit.size());
            num_applied_edit += 1;
        }
        EXPECT_EQ(num_applied_edit, 3);
    }
}
CATCH

TEST_P(WALStoreTest, ReadWriteRestore2)
try
{
    auto ctx = DB::tests::TiFlashTestEnv::getContext();
    auto provider = ctx.getFileProvider();
    auto path = getTemporaryPath();

    auto [wal, reader] = WALStore::create(getCurrentTestName(), provider, delegator, config);
    ASSERT_NE(wal, nullptr);

    std::vector<size_t> size_each_edit;
    // Stage 1. Apply with only puts
    PageEntryV3 entry_p1{.file_id = 1, .size = 1, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageEntryV3 entry_p2{.file_id = 1, .size = 2, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageVersionType ver20(/*seq=*/20);
    {
        PageEntriesEdit edit;
        edit.put(1, entry_p1);
        edit.put(2, entry_p2);
        size_each_edit.emplace_back(edit.size());
        wal->apply(edit, ver20);
    }

    // Stage 2. Apply with puts and refs
    PageEntryV3 entry_p3{.file_id = 1, .size = 3, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageEntryV3 entry_p5{.file_id = 1, .size = 5, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageVersionType ver21(/*seq=*/21);
    {
        PageEntriesEdit edit;
        edit.put(3, entry_p3);
        edit.ref(4, 3);
        edit.put(5, entry_p5);
        edit.del(2);
        size_each_edit.emplace_back(edit.size());
        wal->apply(edit, ver21);
    }

    // Stage 3. Apply with delete and upsert
    PageEntryV3 entry_p1_2{.file_id = 2, .size = 1, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageEntryV3 entry_p3_2{.file_id = 2, .size = 3, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageEntryV3 entry_p5_2{.file_id = 2, .size = 5, .tag = 0, .offset = 0x123, .checksum = 0x4567};
    PageVersionType ver20_1(/*seq=*/20, /*epoch*/ 1);
    PageVersionType ver21_1(/*seq=*/21, /*epoch*/ 1);
    {
        PageEntriesEdit edit;
        edit.upsertPage(1, ver20_1, entry_p1_2);
        edit.upsertPage(3, ver21_1, entry_p3_2);
        edit.upsertPage(5, ver21_1, entry_p5_2);
        size_each_edit.emplace_back(edit.size());
        wal->apply(edit);
    }

    wal.reset();

    {
        size_t num_applied_edit = 0;
        auto reader = WALStoreReader::create(getCurrentTestName(), provider, delegator);
        while (reader->remained())
        {
            const auto & [ok, edit] = reader->next();
            if (!ok)
                break;
            // Details of each edit is verified in `WALSeriTest`
            EXPECT_EQ(size_each_edit[num_applied_edit], edit.size()) << fmt::format("edit size not match at idx={}", num_applied_edit);
            num_applied_edit += 1;
        }
        EXPECT_EQ(num_applied_edit, 3);
    }

    {
        size_t num_applied_edit = 0;
        std::tie(wal, reader) = WALStore::create(getCurrentTestName(), provider, delegator, config);
        while (reader->remained())
        {
            auto [ok, edit] = reader->next();
            if (!ok)
            {
                reader->throwIfError();
                // else it just run to the end of file.
                break;
            }
            // Details of each edit is verified in `WALSeriTest`
            EXPECT_EQ(size_each_edit[num_applied_edit], edit.size()) << fmt::format("edit size not match at idx={}", num_applied_edit);
            num_applied_edit += 1;
        }
        EXPECT_EQ(num_applied_edit, 3);
    }
}
CATCH

TEST_P(WALStoreTest, ManyEdits)
try
{
    auto ctx = DB::tests::TiFlashTestEnv::getContext();
    auto provider = ctx.getFileProvider();
    auto path = getTemporaryPath();

    // Stage 1. empty
    auto [wal, reader] = WALStore::create(getCurrentTestName(), provider, delegator, config);
    ASSERT_NE(wal, nullptr);

    std::mt19937 rd;
    std::uniform_int_distribution<> d(0, 20);

    // Stage 2. insert many edits
    constexpr size_t num_edits_test = 100000;
    PageId page_id = 0;
    std::vector<size_t> size_each_edit;
    size_each_edit.reserve(num_edits_test);
    PageVersionType ver(/*seq*/ 32);
    for (size_t i = 0; i < num_edits_test; ++i)
    {
        PageEntryV3 entry{.file_id = 2, .size = 1, .tag = 0, .offset = 0x123, .checksum = 0x4567};
        PageEntriesEdit edit;
        const size_t num_pages_put = d(rd);
        for (size_t p = 0; p < num_pages_put; ++p)
        {
            page_id += 1;
            entry.size = page_id;
            edit.put(page_id, entry);
        }
        wal->apply(edit, ver);

        size_each_edit.emplace_back(num_pages_put);
        ver.sequence += 1;
    }

    wal.reset();

    size_t num_edits_read = 0;
    size_t num_pages_read = 0;
    std::tie(wal, reader) = WALStore::create(getCurrentTestName(), provider, delegator, config);
    while (reader->remained())
    {
        auto [ok, edit] = reader->next();
        if (!ok)
        {
            reader->throwIfError();
            // else it just run to the end of file.
            break;
        }
        num_pages_read += edit.size();
        EXPECT_EQ(size_each_edit[num_edits_read], edit.size()) << fmt::format("at idx={}", num_edits_read);
        num_edits_read += 1;
    }
    EXPECT_EQ(num_edits_read, num_edits_test);
    EXPECT_EQ(num_pages_read, page_id);

    LOG_FMT_INFO(&Poco::Logger::get("WALStoreTest"), "Done test for {} persist pages in {} edits", num_pages_read, num_edits_test);

    // Stage 3. compact logs and verify
    // wal->compactLogs();
    // wal.reset();

    // // After logs compacted, they should be written as one edit.
    // num_edits_read = 0;
    // num_pages_read = 0;
    // wal = WALStore::create(
    //     [&](PageEntriesEdit && edit) {
    //         num_pages_read += edit.size();
    //         EXPECT_EQ(page_id, edit.size()) << fmt::format("at idx={}", num_edits_read);
    //         num_edits_read += 1;
    //     },
    //     provider,
    //     delegator);
    // EXPECT_EQ(num_edits_read, 1);
    // EXPECT_EQ(num_pages_read, page_id);
}
CATCH

INSTANTIATE_TEST_CASE_P(
    Disks,
    WALStoreTest,
    ::testing::Bool(),
    [](const ::testing::TestParamInfo<WALStoreTest::ParamType> & info) -> String {
        const auto multi_path = info.param;
        if (multi_path)
            return "multi_disks";
        return "single_disk";
    });

} // namespace DB::PS::V3::tests
