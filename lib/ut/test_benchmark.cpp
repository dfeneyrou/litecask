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

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "test_main.h"

using namespace litecask;
using namespace litecask::detail;

// Utils
// ======

static constexpr int ZipfMax = 100000;
static uint32_t      gZipfianLookup[ZipfMax];
static FILE*         gBenchmarkFileHandle = nullptr;

void
initializeZipfDistribution(double coef, uint32_t maxSize)
{
    // Compute the normalization factor
    double sum = 0.;
    for (uint32_t s = 1; s < maxSize; ++s) { sum += 1. / std::pow((double)s, coef); }
    double scale = (double)(ZipfMax - 1) / sum;

    // Write the approximated zipf-stretch lookup (uniform [0;ZipfMax[ -> [1, maxSize[
    sum     = 0.;
    int idx = 0;
    for (uint32_t s = 1; s < maxSize; ++s) {
        sum += 1. / std::pow((double)s, coef);
        int endIdx = std::min((int)(scale * sum), ZipfMax - 1);
        while (idx < endIdx) { gZipfianLookup[idx++] = s; }
    }
    while (idx < ZipfMax) { gZipfianLookup[idx++] = maxSize - 1; }
}

void
workerThread(int workerId, Datastore* store, uint16_t keySize, uint32_t valueSize, int readPercentage, int operationQty, bool forcedSync)
{
    (void)workerId;
    // Prepare the base key and value
    lcVector<uint8_t> key(keySize);
    lcVector<uint8_t> value(valueSize);
    lcVector<uint8_t> valueGet;
    Status            s = Status::Ok;
    assert(valueSize < ZipfMax);
    for (uint32_t i = 0; i < keySize; ++i) key[i] = ((uint8_t)i) & 0xFF;
    for (uint32_t i = 0; i < valueSize; ++i) value[i] = ((uint8_t)i) & 0xFF;
    uint64_t readThreshold = 65536 * readPercentage / 100;

    for (int opNbr = 0; opNbr < operationQty; ++opNbr) {
        uint64_t r      = testGetRandom();
        bool     doRead = ((r >> 32) & 0xFFFFUL) < readThreshold;

        *((uint32_t*)&key[0]) = gZipfianLookup[r % ZipfMax];
        if (doRead) {
            s = store->get(&key[0], keySize, valueGet);
            CHECK_EQ(s, Status::Ok);
        } else {
            *((uint32_t*)&value[0]) = opNbr;
            s                       = store->put(&key[0], keySize, &value[0], valueSize, {}, 0, forcedSync);
            CHECK_EQ(s, Status::Ok);
        }
    }
}

void
loadDatabase(Datastore& store, uint16_t keySize, int valueSize, int opQty, int startOpNbr = 0)
{
    // Prepare the base key and value
    lcVector<uint8_t> key(keySize, 0);
    lcVector<uint8_t> value(ZipfMax, 0);
    Status            s = Status::Ok;
    for (uint32_t i = 0; i < keySize; ++i) key[i] = ((uint8_t)i) & 0xFF;
    for (uint32_t i = 0; i < ZipfMax; ++i) value[i] = ((uint8_t)i) & 0xFF;

    for (int opNbr = 0; opNbr < opQty; ++opNbr) {
        *((uint32_t*)&key[0])   = startOpNbr + opNbr;
        *((uint32_t*)&value[0]) = startOpNbr + opNbr;
        s                       = store.put(&key[0], keySize, &value[0], valueSize, {});
        CHECK_EQ(s, Status::Ok);
    }

    // Flush the write buffer
    store.sync();
}

uint64_t
baseBenchmark(const lcString& descr, Datastore& store, int threadQty, uint16_t keySize, int valueSize, int readPercentage, int operationQty,
              bool forcedSync, double customValue = 0)
{
    std::vector<std::thread> threadList;
    threadList.reserve(threadQty);

    uint64_t                  startTimeUs = testGetTimeUs();
    const ValueCacheCounters& stats       = store.getValueCacheCounters();
    uint64_t                  lastHitQty  = stats.hitQty;
    uint64_t                  lastMissQty = stats.missQty;

    for (int threadNbr = 0; threadNbr < threadQty; ++threadNbr) {
        threadList.push_back(std::thread(&workerThread, threadNbr, &store, keySize, valueSize, readPercentage, operationQty, forcedSync));
    }
    for (std::thread& t : threadList) t.join();
    store.sync();
    uint64_t durationUs = testGetTimeUs() - startTimeUs;
    double   hitRatio =
        (double)(stats.hitQty - lastHitQty) / std::max(1.0, (double)(stats.hitQty - lastHitQty + stats.missQty - lastMissQty));

    if (gBenchmarkFileHandle) {
        fprintf(gBenchmarkFileHandle, "%s, %d, %d, %d, %d, %d, %" PRIu64 ", %d, %f\n", descr.c_str(), threadQty, keySize, valueSize,
                readPercentage, operationQty, durationUs, forcedSync ? 1 : 0, customValue);
    }
    printf("    %8s T=%02d K=%4d V=%5d Read=%3d%% -> %6.3f Mop/s    (cache hit=%5.1f%%)\n", descr.c_str(), threadQty, keySize, valueSize,
           readPercentage, (double)operationQty * threadQty / (double)durationUs, 100. * hitRatio);

    return durationUs;
}

void
setup(const char* suffix)
{
    constexpr int MaxFilenameSize = 64;
    char          filename[MaxFilenameSize];
    snprintf(filename, sizeof(filename), "benchmark%s.csv", suffix);

    gBenchmarkFileHandle = fopen(filename, "w");
    if (!gBenchmarkFileHandle) {
        printf("ERROR: unable to create the file '%s'", filename);
        assert(gBenchmarkFileHandle);
    }

    fprintf(gBenchmarkFileHandle, "#Descr, ThreadQty, KeySize, ValueSize, ReadPercent, OperationQty, DurationUs, ForcedWriteSync\n");
}

void
tearDown()
{
    // Tear down
    if (gBenchmarkFileHandle) {
        fclose(gBenchmarkFileHandle);
        gBenchmarkFileHandle = nullptr;
    }
}

// Tests
// =====

TEST_SUITE("Benchmarks")
{
#if 0
    TEST_CASE("2-Benchmark: Debug")
    {
        const char*        databasePath = "/tmp/litecask_test/benchmark";
        constexpr int      readPercent  = 100;
        constexpr uint16_t keySize      = 8;
        constexpr uint32_t valueSize    = 8;
        constexpr int      threadQty    = 5;
        int                batchQty     = 50'000;
        if (testGetDuration() == TestDuration::Long) {
            batchQty = 100'000;
        } else if (testGetDuration() == TestDuration::Longest) {
            batchQty = 200'000;
        }
        initializeZipfDistribution(0.9, batchQty);

        // Prepare the new database
        Datastore::erasePermanentlyAllContent_UseWithCaution(databasePath);
        Datastore store(1000*1024*1024);  // The benchmark assumes that values are in the cache
        Status s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);
        assert(s == Status::Ok);
        loadDatabase(store, keySize, valueSize, batchQty);

        baseBenchmark("Debug test", store, threadQty, keySize, valueSize, readPercent, batchQty, false);
    }
#endif

    TEST_CASE("2-Benchmark: Monothread performance")
    {
        // Database cleanup and setup useful variables
        constexpr int databaseSize = 1'000'000;
        const char*   databasePath = "/tmp/litecask_test/benchmark";
        int           batchQty     = 10'000;
        if (testGetDuration() == TestDuration::Long) {
            batchQty = 50'000;
        } else if (testGetDuration() == TestDuration::Longest) {
            batchQty = 150'000;
        }
        setup("monothread");

        // Fixed value size tests, 1 thread
        struct FixedSize {
            uint16_t keySize;
            uint32_t valueSize;
        };
        std::vector<FixedSize> sizes{{8, 8},   {8, 256}, {8, 512},  {8, 1024}, {8, 2048}, {8, 4096},
                                     {256, 8}, {512, 8}, {1024, 8}, {2048, 8}, {4096, 8}};
        std::vector<int>       readPercents{0, 95, 100};
        initializeZipfDistribution(1.0, databaseSize);

        for (const FixedSize& fs : sizes) {
            // Prepare the new database
            Datastore::erasePermanentlyAllContent_UseWithCaution(databasePath);
            Datastore store(1000 * 1024 * 1024);  // The benchmark assumes that values are in the cache
            Status    s = store.open(databasePath);
            CHECK_EQ(s, Status::Ok);
            assert(s == Status::Ok);
            loadDatabase(store, fs.keySize, fs.valueSize, databaseSize);

            for (int readPercent : readPercents) {
                baseBenchmark("Monothread", store, 1, fs.keySize, fs.valueSize, readPercent, batchQty, false);
            }
        }

        tearDown();
    }

    TEST_CASE("2-Benchmark: Multithread performance")
    {
        // Database cleanup and setup useful variables
        constexpr int databaseSize = 1'000'000;
        const char*   databasePath = "/tmp/litecask_test/benchmark";
        setup("multithread");
        int batchQty = 2'500;
        if (testGetDuration() == TestDuration::Long) {
            batchQty = 10'000;
        } else if (testGetDuration() == TestDuration::Longest) {
            batchQty = 50'000;
        }

        // Zipfian distribution for value size, and multithread
        std::vector<int> readPercents{0, 95, 100};
        initializeZipfDistribution(1.0, databaseSize);

        // Prepare the new database
        Datastore::erasePermanentlyAllContent_UseWithCaution(databasePath);
        Datastore store(1000 * 1024 * 1024);  // The benchmark assumes that values are in the cache
        Status    s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);
        loadDatabase(store, 8, 256, databaseSize);

        for (int threadQty = 1; threadQty <= 15; ++threadQty) {
            for (int readPercent : readPercents) { baseBenchmark("Multithread", store, threadQty, 8, 256, readPercent, batchQty, false); }
        }

        tearDown();
    }

    TEST_CASE("2-Benchmark: Startup performance")
    {
        const char*   databasePath = "/tmp/litecask_test/benchmark";
        constexpr int KeySize      = 8;
        constexpr int ValueSize    = 16;

        // Prepare the new database
        Datastore::erasePermanentlyAllContent_UseWithCaution(databasePath);
        printf("  > Startup time benchmark (keySize=%d, valueSize=%d):\n", KeySize, ValueSize);

        // Loop on different database size
        std::vector<double> entryMillions{0.1};
        if (testGetDuration() == TestDuration::Long) {
            entryMillions = {0.1, 1., 5.};
        } else if (testGetDuration() == TestDuration::Longest) {
            entryMillions = {0.1, 1., 10., 30.};
        }

        int lastEntryQty = 0;
        for (double entryMillion : entryMillions) {
            int newEntryQty = (int)(entryMillion * 1024. * 1024.);

            // Add some entries
            {
                Datastore store(1000 * 1024 * 1024);
                Status    s = store.open(databasePath);
                CHECK_EQ(s, Status::Ok);
                loadDatabase(store, KeySize, ValueSize, newEntryQty - lastEntryQty, lastEntryQty);
                lastEntryQty = newEntryQty;
                s            = store.close();
                CHECK_EQ(s, Status::Ok);
            }

            // Open it once to ensure that the previous files (including the previous active) has a hint file
            {
                Datastore store(1000 * 1024 * 1024);
                Status    s = store.open(databasePath);
                CHECK_EQ(s, Status::Ok);

                // Force Hint file
                bool requestOk = store.requestMerge();
                CHECK_EQ(requestOk, true);
                int round = 0;
                while (store.isMergeOnGoing() && round < 1000) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    ++round;
                }
                CHECK(round < 1000);

                s = store.close();
                CHECK_EQ(s, Status::Ok);
            }

            // Measure the database loading time
            uint64_t durationUs      = 0;
            uint64_t usedMemoryBytes = 0;
            {
                uint64_t  startTimeUs = testGetTimeUs();
                Datastore store(1000 * 1024 * 1024);
                Status    s = store.open(databasePath);
                CHECK_EQ(s, Status::Ok);
                durationUs      = testGetTimeUs() - startTimeUs;
                usedMemoryBytes = store.getEstimatedUsedMemoryBytes();
                s               = store.close();
                CHECK_EQ(s, Status::Ok);
            }
            printf("    %5.1f million entries: startup time= %-.3fs (%6.3f Mentries/s)    used memory=%-4" PRId64
                   " MB (key size + %.0fB / entry)\n",
                   entryMillion, 1e-6 * (double)durationUs, entryMillion * 1e6 / (double)durationUs, usedMemoryBytes / 1000000,
                   (double)usedMemoryBytes / (entryMillion * 1000000.) - KeySize);
        }
    }

    TEST_CASE("2-Benchmark: Synced write performance")
    {
        const char*        databasePath = "/tmp/litecask_test/benchmark";
        constexpr uint16_t keySize      = 8;
        constexpr uint32_t valueSize    = 8;
        int                batchQty     = 50'000;
        if (testGetDuration() == TestDuration::Long) {
            batchQty = 200'000;
        } else if (testGetDuration() == TestDuration::Longest) {
            batchQty = 400'000;
        }
        initializeZipfDistribution(0.9, batchQty);

        // Prepare the new database
        Datastore::erasePermanentlyAllContent_UseWithCaution(databasePath);
        printf("  > Synced write time benchmark:\n");
        Datastore store;
        Status    s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        baseBenchmark("Deferred sync", store, 1, keySize, valueSize, 0, batchQty, false);
        baseBenchmark("Forced   sync", store, 1, keySize, valueSize, 0, batchQty, true);
    }

    TEST_CASE("2-Benchmark: Cache size")
    {
        // Database cleanup and setup useful variables
        const char*        databasePath = "/tmp/litecask_test/benchmark";
        constexpr uint16_t keySize      = 8;
        constexpr uint32_t valueSize    = 256;
        int                batchQty     = 100'000;
        if (testGetDuration() == TestDuration::Long) {
            batchQty = 1'000'000;
        } else if (testGetDuration() == TestDuration::Longest) {
            batchQty = 5'000'000;
        }
        size_t dataFullyInCacheBytes = batchQty * valueSize;
        printf("  > Cache size effect on read performance (%.1f million entries):\n", 1e-6 * batchQty);

        std::vector<double> cacheFractions{0., 0.25, 0.5, 0.9, 1.5};
        initializeZipfDistribution(1.0, batchQty);

        for (double fraction : cacheFractions) {
            // Prepare the new database
            Datastore::erasePermanentlyAllContent_UseWithCaution(databasePath);
            Datastore store((size_t)(fraction * (double)dataFullyInCacheBytes));
            Status    s = store.open(databasePath);
            CHECK_EQ(s, Status::Ok);
            assert(s == Status::Ok);
            loadDatabase(store, keySize, valueSize, batchQty);

            char titleStr[64];
            snprintf(titleStr, sizeof(titleStr), "Cache size %3.0f%%", 100. * std::min(1., fraction));
            baseBenchmark(titleStr, store, 1, keySize, valueSize, 100, batchQty, false, fraction);
        }
    }

}  // End of test suite
