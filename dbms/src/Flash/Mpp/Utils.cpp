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

#include <Flash/Mpp/Utils.h>

#include <memory>

namespace DB
{
mpp::MPPDataPacket getPacketWithError(String reason)
{
    mpp::MPPDataPacket data;
    auto err = std::make_unique<mpp::Error>();
    err->set_msg(std::move(reason));
    data.set_allocated_error(err.release());
    return data;
}

} // namespace DB
