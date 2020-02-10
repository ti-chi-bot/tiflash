#pragma once

#include <set>
#include <vector>

#include <Storages/Page/Page.h>
#include <Storages/Page/PageEntries.h>
#include <Storages/Page/VersionSet/PageEntriesBuilder.h>
#include <Storages/Page/VersionSet/PageEntriesEdit.h>
#include <Storages/Page/WriteBatch.h>
#include <Storages/Page/mvcc/VersionSet.h>

namespace DB
{

class PageEntriesVersionSet : public ::DB::MVCC::VersionSet<PageEntries, PageEntriesEdit, PageEntriesBuilder>
{
public:
    explicit PageEntriesVersionSet(const ::DB::MVCC::VersionSetConfig & config_, Poco::Logger * log)
        : ::DB::MVCC::VersionSet<PageEntries, PageEntriesEdit, PageEntriesBuilder>(config_)
    {
        (void)log;
    }

public:
    using SnapshotPtr = ::DB::MVCC::VersionSet<PageEntries, PageEntriesEdit, PageEntriesBuilder>::SnapshotPtr;

    /// `gcApply` only accept PageEntry's `PUT` changes and will discard changes if PageEntry is invalid
    /// append new version to version-list
    std::pair<std::set<PageFileIdAndLevel>, std::set<PageId>> gcApply(PageEntriesEdit & edit);

    /// List all PageFile that are used by any version
    std::pair<std::set<PageFileIdAndLevel>, std::set<PageId>> listAllLiveFiles(const std::unique_lock<std::shared_mutex> &) const;
};


} // namespace DB
