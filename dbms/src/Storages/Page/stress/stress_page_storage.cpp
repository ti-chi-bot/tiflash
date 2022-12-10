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

#include <PSStressEnv.h>
#include <PSWorkload.h>

int main(int argc, char ** argv)
try
{
    StressEnv::initGlobalLogger();
    auto env = StressEnv::parse(argc, argv);
    env.setup();

    auto & mamager = StressWorkloadManger::getInstance();
    mamager.setEnv(env);
    mamager.runWorkload();

    return StressEnvStatus::getInstance().isSuccess();
}
catch (...)
{
    DB::tryLogCurrentException("");
    exit(-1);
}
