// Litecask - High performance, persistent embedded Key-Value storage engine.
//
// The MIT License (MIT)
//
// Copyright(c) 2023, Damien Feneyrou <dfeneyrou@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#define DOCTEST_CONFIG_IMPLEMENT
#include "test_main.h"

using namespace doctest;

// ==========================================================================================
// Test global context
// ==========================================================================================

// Global context to have different durations of tests
TestDuration gTestDuration = TestDuration::Short;

// Global context to display names of skipped tests
bool gTestDoDisplaySkippedTests = false;

// ==========================================================================================
// Our "compact" doctest reporter
// ==========================================================================================

struct CompactReporter : public IReporter {
    // caching pointers/references to objects of these types - safe to do
    const ContextOptions& opt;
    const TestCaseData*   tc;
    bool                  firstError = true;
    uint64_t              startTimeUs;
    std::mutex            mutex;

    static constexpr const char* Red         = "\033[0;31m";
    static constexpr const char* Green       = "\033[0;32m";
    static constexpr const char* Blue        = "\033[0;34m";
    static constexpr const char* Cyan        = "\033[0;36m";
    static constexpr const char* Yellow      = "\033[0;33m";
    static constexpr const char* Grey        = "\033[1;30m";
    static constexpr const char* LightGrey   = "\033[0;37m";
    static constexpr const char* BrightRed   = "\033[1;31m";
    static constexpr const char* BrightGreen = "\033[1;32m";
    static constexpr const char* BrightWhite = "\033[1;37m";
    static constexpr const char* None        = "\033[0m";

    const char* skipPathFromFilename(const char* file)
    {
        auto back    = std::strrchr(file, '\\');
        auto forward = std::strrchr(file, '/');
        if (back || forward) {
            if (back > forward) { forward = back; }
            return forward + 1;
        }
        return file;
    }

    void separator()
    {
        printf("%s=====================================================================================================%s\n", Grey, None);
    }

    CompactReporter(const ContextOptions& in) : opt(in), tc(nullptr) {}

    void report_query(const QueryData& /*in*/) override
    {
        if (opt.version) {
            printf("Litecask version is %d.%d.%d\n", LITECASK_VERSION_MAJOR, LITECASK_VERSION_MINOR, LITECASK_VERSION_PATCH);
        } else if (opt.help) {
            separator();
            printf("This executable tests the litecask library %d.%d.%d\n", LITECASK_VERSION_MAJOR, LITECASK_VERSION_MINOR,
                   LITECASK_VERSION_PATCH);
            printf("Parameters have two dimensions:\n");
            printf(" - the selection of the kind of tests\n");
            printf(" - the selection of the depth of the tests\n");
            printf("Default parameters focus on development cycle, i.e. short sanity tests\n");
            separator();
            printf("The available keywords are:\n");
            printf(" sanity                               add sanity tests (default if no other keyword)\n");
            printf(" benchmark                            add benchmark tests\n");
            printf(" stress                               add stress tests\n");
            separator();
            printf("The available option durations are:\n");
            printf(" -l                                   longer test duration\n");
            printf(" -ll                                  even longer test duration\n");
            separator();
            printf("The available misc options are:\n");
            printf(" -?, --help, -h                       prints this message and exit\n");
            printf(" -v, --version                        prints the litecask version and exit\n");
            printf(" -ss                                  show names of skipped tests\n");
            printf("The available option filters are (union with selected kinds of tests):\n");
            printf(" -tc,  --test-case=<filters>          filters     tests by their name\n");
            printf(" -tce, --test-case-exclude=<filters>  filters OUT tests by their name\n");
            printf(" -ts,  --test-suite=<filters>         filters     tests by their test suite\n");
            printf(" -tse, --test-suite-exclude=<filters> filters OUT tests by their test suite\n");
            separator();
        }
    }

    void test_run_start() override
    {
        separator();
        startTimeUs = testGetTimeUs();
    }

    void test_run_end(const TestRunStats& p) override
    {
        uint64_t durationUs = testGetTimeUs() - startTimeUs;
        separator();

        printf("Specified duration  : %s (%.1fs)\n",
               (testGetDuration() == TestDuration::Short) ? "Short" : ((testGetDuration() == TestDuration::Long) ? "Long" : "Longest"),
               1e-6 * (double)durationUs);
        printf("Tests pass/skip/fail: %s%d%s / %s%d%s / %s%d%s\n", (p.numTestCasesPassingFilters - p.numTestCasesFailed > 0) ? Green : Grey,
               p.numTestCasesPassingFilters - p.numTestCasesFailed, None,
               (p.numTestCases - p.numTestCasesPassingFilters > 0) ? Yellow : Grey, p.numTestCases - p.numTestCasesPassingFilters, None,
               (p.numTestCasesFailed > 0) ? Red : Grey, p.numTestCasesFailed, None);
        printf("Global Status       : %s%s%s\n", (p.numTestCasesFailed > 0) ? Red : Green,
               (p.numTestCasesFailed > 0) ? "FAILURE" : "SUCCESS", None);
        separator();
        printf("\n");
    }

    void test_case_start(const TestCaseData& in) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        tc         = &in;
        firstError = true;
        // Print a test header if not a sanity test.
        // By design, sanity tests do not output anything, so footer is not ambiguous.
        // Also sanity tests are run very often and shall stay visually unbloated.
        // Other tests may output additional lines, so a "header" helps to group them with the
        // actual test and not the previous one
        if (std::string(tc->m_name).find("Sanity") == std::string::npos) { printf("%s%s%s\n", Blue, tc->m_name, None); }
    }

    void test_case_reenter(const TestCaseData& /*in*/) override {}

    void test_case_end(const CurrentTestCaseStats& st) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        printf("%s%-60s    %s %s    %s%-16s [%.2fs]%s\n", st.failure_flags ? Red : Blue, tc->m_name, st.failure_flags ? Red : Green,
               st.failure_flags ? "FAILURE" : "SUCCESS", Grey, tc->m_test_suite, st.seconds, None);
    }

    void test_case_exception(const TestCaseException& in) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        printf("%s> EXCEPTION %s%s\n", Red, in.error_string.c_str(), None);
    }

    void subcase_start(const SubcaseSignature& /*in*/) override {}

    void subcase_end() override {}

    void log_assert(const AssertData& in) override
    {
        if (!in.m_failed && !opt.success) { return; }
        std::lock_guard<std::mutex> lock(mutex);

        if (firstError) { printf("\n"); }  // Skip a line to have visual separation with previous test
        firstError = false;

        bool isWarning = (in.m_at & assertType::is_warn);
        char tmpStr[512];
        snprintf(tmpStr, sizeof(tmpStr), "> %s %s ", isWarning ? "WARNING" : "ERROR", tc->m_name);
        printf("%s%s %s%s( %s )%s    at %s:%d %s\n", isWarning ? Yellow : Red, tmpStr, Cyan, assertString(in.m_at), in.m_expr, None,
               skipPathFromFilename(tc->m_file.c_str()), in.m_line, None);
        printf("%.*s Values are ( %s )\n", (int)strlen(tmpStr), "                                                            ",
               in.m_decomp.c_str());
    }

    void log_message(const MessageData& /*in*/) override {}

    void test_case_skipped(const TestCaseData& in) override
    {
        tc = &in;
        if (gTestDoDisplaySkippedTests) {
            printf("%s%-60s     %sSKIPPED    %s%s%s\n", Blue, tc->m_name, Yellow, Grey, tc->m_test_suite, None);
        }
    }
};

REGISTER_REPORTER("compact", 0, CompactReporter);

// ==========================================================================================
// Our main
// ==========================================================================================

int
main(int argc, char** argv)
{
    doctest::Context context;

    // Set our defaults
    context.setOption("reporters", "compact");
    context.setOption("order-by", "name");

    // Parse command line for test options
    context.applyCommandLine(argc, argv);

    // Parse command line for our test topics and test duration options
    bool     userTcFilterIsPresent = false;
    lcString tcFilter;
    int      argNbr = 1;
    while (argNbr < argc && argv[argNbr]) {
        const char* argStr = argv[argNbr];

        if (!strncmp(argStr, "-tc=", 4) || !strncmp(argStr, "--test-case=", 12)) {
            userTcFilterIsPresent = true;
        } else if (!strcmp(argStr, "-l")) {
            gTestDuration = TestDuration::Long;
        } else if (!strcmp(argStr, "-ll")) {
            gTestDuration = TestDuration::Longest;
        }

        else if (!strcmp(argStr, "-ss")) {
            gTestDoDisplaySkippedTests = true;
        }

        else if (!strcmp(argStr, "sanity")) {
            tcFilter += lcString(!tcFilter.empty() ? "," : "") + "1-Sanity*";
        } else if (!strcmp(argStr, "benchmark")) {
            tcFilter += lcString(!tcFilter.empty() ? "," : "") + "2-Benchmark*";
        } else if (!strcmp(argStr, "stress")) {
            tcFilter += lcString(!tcFilter.empty() ? "," : "") + "3-Stress*";
        }

        else if (argStr[0] != '-') {
            printf("ERROR: bad keyword '%s'. Valid values are 'sanity', 'benchmark' and 'stress'\n", argStr);
            exit(1);
        }

        ++argNbr;
    }

    // Set the filter for our list of tests to execute
    if (!userTcFilterIsPresent && tcFilter.empty()) tcFilter = "1-Sanity*";
    context.setOption("tc", tcFilter.c_str());

    // Execute the test and return the status
    return context.run();
}
