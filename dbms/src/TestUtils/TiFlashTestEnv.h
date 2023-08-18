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

#include <Interpreters/Context.h>
#include <Poco/File.h>
#include <Poco/Path.h>
#include <Poco/SortedDirectoryIterator.h>
#include <TestUtils/TiFlashTestException.h>
#include <fmt/core.h>

namespace DB::tests
{
class TiFlashTestEnv
{
public:
    static String getTemporaryPath(const std::string_view test_case = "")
    {
        String path = "./tmp/";
        if (!test_case.empty())
            path += std::string(test_case);

        return Poco::Path(path).absolute().toString();
    }

    static void tryRemovePath(const std::string & path)
    {
        try
        {
            if (Poco::File p(path); p.exists())
            {
                p.remove(true);
            }
        }
        catch (...)
        {
            tryLogCurrentException("gtest", fmt::format("while removing dir `{}`", path));
        }
    }

    static std::pair<Strings, Strings> getPathPool(const Strings & testdata_path = {})
    {
        Strings result;
        if (!testdata_path.empty())
            for (const auto & p : testdata_path)
                result.push_back(Poco::Path{p}.absolute().toString());
        else
            result.push_back(Poco::Path{getTemporaryPath()}.absolute().toString());
        return std::make_pair(result, result);
    }

<<<<<<< HEAD
    static void setupLogger(const String & level = "trace", std::ostream & os = std::cerr);
=======
    static void setupLogger(const String & level = "trace", std::ostream & os = std::cerr, bool enable_colors = true);
    static void setUpTestContext(
        Context & context,
        DAGContext * dag_context,
        MockStorage * mock_storage,
        const TestType & test_type);
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))

    // If you want to run these tests, you should set this envrionment variablle
    // For example:
    //     ALSO_RUN_WITH_TEST_DATA=1 ./dbms/gtests_dbms --gtest_filter='IDAsPath*'
    static bool isTestsWithDataEnabled() { return (Poco::Environment::get("ALSO_RUN_WITH_TEST_DATA", "0") == "1"); }

    static Strings findTestDataPath(const String & name)
    {
        const static std::vector<String> SEARCH_PATH = {"../tests/testdata/", "/tests/testdata/"};
        for (const auto & prefix : SEARCH_PATH)
        {
            String path = prefix + name;
            if (auto f = Poco::File(path); f.exists() && f.isDirectory())
            {
                Strings paths;
                Poco::SortedDirectoryIterator dir_end;
                for (Poco::SortedDirectoryIterator dir_it(f); dir_it != dir_end; ++dir_it)
                    paths.emplace_back(path + "/" + dir_it.name() + "/");
                return paths;
            }
        }
        throw Exception("Can not find testdata with name[" + name + "]");
    }

    static Context getContext(const DB::Settings & settings = DB::Settings(), Strings testdata_path = {});

<<<<<<< HEAD
    static void initializeGlobalContext(Strings testdata_path = {}, bool enable_ps_v3 = false);
    static Context & getGlobalContext() { return *global_context; }
=======
    /// Returns a fresh ContextPtr.
    static ContextPtr getContext(const DB::Settings & settings, Strings testdata_path = {});

    static FileProviderPtr getDefaultFileProvider();

    static void initializeGlobalContext(
        Strings testdata_path = {},
        PageStorageRunMode ps_run_mode = PageStorageRunMode::ONLY_V3,
        uint64_t bg_thread_count = 2);
    static void addGlobalContext(
        const DB::Settings & settings,
        Strings testdata_path = {},
        PageStorageRunMode ps_run_mode = PageStorageRunMode::ONLY_V3,
        uint64_t bg_thread_count = 2);
    static Context & getGlobalContext() { return *global_contexts[0]; }
    static Context & getGlobalContext(int idx) { return *global_contexts[idx]; }
    static int globalContextSize() { return global_contexts.size(); }
>>>>>>> 6638f2067b (Fix license and format coding style (#7962))
    static void shutdown();

    TiFlashTestEnv() = delete;

private:
    static std::unique_ptr<Context> global_context;
};
} // namespace DB::tests
