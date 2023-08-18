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

#include <Storages/IStorage.h>

#include <ext/shared_ptr_helper.h>


namespace DB
{
class Context;


class StorageSystemModels : public ext::SharedPtrHelper<StorageSystemModels>
    , public IStorage
{
public:
<<<<<<< HEAD:dbms/src/Storages/System/StorageSystemModels.h
    std::string getName() const override { return "SystemModels"; }
    std::string getTableName() const override { return name; }
=======
    Planner(Context & context_, const PlanQuerySource & plan_source_);
>>>>>>> 6638f2067b (Fix license and format coding style (#7962)):dbms/src/Flash/Planner/Planner.h

    BlockInputStreams read(
        const Names & column_names,
        const SelectQueryInfo & query_info,
        const Context & context,
        QueryProcessingStage::Enum & processed_stage,
        size_t max_block_size,
        unsigned num_streams) override;

private:
    const std::string name;

protected:
    StorageSystemModels(const std::string & name);
};

} // namespace DB
