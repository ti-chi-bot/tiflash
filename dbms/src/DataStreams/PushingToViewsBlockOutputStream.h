#pragma once

#include <DataStreams/copyData.h>
#include <DataStreams/IBlockOutputStream.h>
#include <DataStreams/OneBlockInputStream.h>
#include <DataStreams/MaterializingBlockInputStream.h>
#include <Storages/StorageMaterializedView.h>


namespace DB
{


/** Writes data to the specified table and to all dependent materialized views.
  */
class PushingToViewsBlockOutputStream : public IBlockOutputStream
{
public:
    PushingToViewsBlockOutputStream(
        const String & database, const String & table, const StoragePtr & storage,
        const Context & context_, const ASTPtr & query_ptr_, bool no_destination = false);

    Block getHeader() const override { return storage->getSampleBlock(); }
    void write(const Block & block) override;

    void flush() override
    {
        if (output)
            output->flush();

        for (auto & view : views)
            view.out->flush();
    }

    void writePrefix() override
    {
        if (output)
            output->writePrefix();

        for (auto & view : views)
            view.out->writePrefix();
    }

    void writeSuffix() override
    {
        if (output)
            output->writeSuffix();

        for (auto & view : views)
            view.out->writeSuffix();
    }

private:
    StoragePtr storage;
    BlockOutputStreamPtr output;

    const Context & context;
    ASTPtr query_ptr;

    struct ViewInfo
    {
        ASTPtr query;
        String database;
        String table;
        BlockOutputStreamPtr out;
    };

    std::vector<ViewInfo> views;
    std::unique_ptr<Context> views_context;
};


}
