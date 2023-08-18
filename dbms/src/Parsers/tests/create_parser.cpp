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

#include <Parsers/ASTCreateQuery.h>
#include <Parsers/ParserCreateQuery.h>
#include <Parsers/formatAST.h>
#include <Parsers/parseQuery.h>

#include <iostream>


int main(int, char **)
{
    using namespace DB;

    std::string input
        = "CREATE TABLE hits (URL String, UserAgentMinor2 FixedString(2), EventTime DateTime) ENGINE = Log";
    ParserCreateQuery parser;
    ASTPtr ast = parseQuery(parser, input.data(), input.data() + input.size(), "", 0);

    formatAST(*ast, std::cerr);
    std::cerr << std::endl;

    return 0;
}
