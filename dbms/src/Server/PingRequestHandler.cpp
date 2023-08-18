<<<<<<< HEAD:dbms/src/Server/PingRequestHandler.cpp
// Copyright 2022 PingCAP, Ltd.
=======
// Copyright 2023 PingCAP, Inc.
>>>>>>> 6638f2067b (Fix license and format coding style (#7962)):dbms/src/Flash/Executor/ExecutionResult.h
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

#include "PingRequestHandler.h"

#include <IO/HTTPCommon.h>

#include <Common/Exception.h>

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>

namespace DB
{

void PingRequestHandler::handleRequest(
    Poco::Net::HTTPServerRequest &,
    Poco::Net::HTTPServerResponse & response)
{
    try
    {
        const auto & config = server.config();
        setResponseDefaultHeaders(response, config.getUInt("keep_alive_timeout", 10));

<<<<<<< HEAD:dbms/src/Server/PingRequestHandler.cpp
        const char * data = "Ok.\n";
        response.sendBuffer(data, strlen(data));
    }
    catch (...)
    {
        tryLogCurrentException("PingRequestHandler");
    }
}
=======
    static ExecutionResult success() { return {true, nullptr}; }
>>>>>>> 6638f2067b (Fix license and format coding style (#7962)):dbms/src/Flash/Executor/ExecutionResult.h

}
