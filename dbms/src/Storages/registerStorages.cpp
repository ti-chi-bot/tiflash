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

#include <Common/config.h>
#include <Storages/StorageFactory.h>
#include <Storages/registerStorages.h>


namespace DB
{
void registerStorageLog(StorageFactory & factory);
void registerStorageDeltaMerge(StorageFactory & factory);
void registerStorageNull(StorageFactory & factory);
void registerStorageMemory(StorageFactory & factory);
void registerStorageSet(StorageFactory & factory);


void registerStorages()
{
    auto & factory = StorageFactory::instance();

    registerStorageLog(factory);
    registerStorageDeltaMerge(factory);
    registerStorageNull(factory);
    registerStorageMemory(factory);
    registerStorageSet(factory);
}

} // namespace DB
