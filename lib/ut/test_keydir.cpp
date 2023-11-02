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

#include "test_main.h"

using namespace litecask;
using namespace litecask::detail;

// Utils
// ======

TEST_SUITE("KeyDir")
{
    TEST_CASE("1-Sanity   : KeyDir load factor and probing")
    {
        constexpr int KeySize                    = 8;
        constexpr int MaintenanceKeyDirBatchSize = 100'000;
        uint32_t      mapSize                    = 128 * 1024;
        if (testGetDuration() == TestDuration::Long) {
            mapSize = 1 * 1024 * 1024;
        } else if (testGetDuration() == TestDuration::Longest) {
            mapSize = 16 * 1024 * 1024;
        }
        bool      doPrint = (testGetDuration() != TestDuration::Short);  // Print only for long durations (to have concise quick sanity)
        KeyDirMap keyDir(detail::KeyStorageAllocBytes, mapSize, [&](uint32_t, bool isStart, bool) {
            while (isStart && keyDir.isResizingOngoing()) keyDir.backgroundResizeWork(MaintenanceKeyDirBatchSize);
        });
        lcVector<uint8_t> key(KeySize, 0);
        KeyChunk          entry{128, 0, NotStored, 0, 0, KeySize, 0, 0};
        OldKeyChunk       oldEntry;

        keyDir.setMaxLoadFactor(1.);            // No resizing due to load factor. Our initial dimensioning prevents any locked situation
        keyDir.setInstrumentationEnable(true);  // Enabling of the probe counting

        // Loop on different load factors
        if (doPrint) { printf("  > Analysis of KeyDir lookup probing (%d-associative hashtable of size %u):\n", KeyDirAssocQty, mapSize); }
        int64_t startLoadedKey = 0;
        for (int64_t percentLoad = 55; percentLoad <= 90LL; percentLoad += 5LL) {
            // Load the table a bit more
            int64_t lastLoadedKey = percentLoad * mapSize / 100;
            for (int64_t opNbr = startLoadedKey; opNbr < lastLoadedKey; ++opNbr) {
                *((uint32_t*)&key[0])  = (uint32_t)opNbr;
                uint64_t keyHash       = LITECASK_HASH_FUNC(&key[0], KeySize);
                Status   storageStatus = keyDir.insertEntry((uint32_t)keyHash, &key[0], nullptr, entry, oldEntry);
                CHECK_EQ(storageStatus, Status::Ok);
            }

            // Measure
            uint64_t probeMax       = 0;
            uint64_t startProbeSum  = 0;
            uint64_t startFindCount = 0;
            keyDir.getProbeCount(probeMax, startProbeSum, startFindCount);
            for (int64_t opNbr = startLoadedKey; opNbr < lastLoadedKey; ++opNbr) {
                *((uint32_t*)&key[0]) = (uint32_t)opNbr;
                uint64_t keyHash      = LITECASK_HASH_FUNC(&key[0], KeySize);
                bool     isFound      = keyDir.find((uint32_t)keyHash, &key[0], KeySize, entry);
                CHECK_EQ(isFound, true);
            }
            uint64_t endProbeSum  = 0;
            uint64_t endFindCount = 0;
            keyDir.getProbeCount(probeMax, endProbeSum, endFindCount);

            // Check
            double avgProbeCount = (double)(endProbeSum - startProbeSum) / (double)std::max((uint64_t)1, endFindCount - startFindCount);
            if (doPrint) {
                printf("    %.0f%% load factor: Avg probe count=%5.2f  Max probe count=%2" PRIu64 "\n", 100. * keyDir.getLoadFactor(),
                       avgProbeCount, probeMax);
            }
            CHECK(avgProbeCount < 5.);  // The threshold is twice the current max number. Hardcoded threshold to detect regression.
            CHECK(probeMax < 50);       // The threshold is twice above the current max number.

            // Try next load level
            startLoadedKey = lastLoadedKey;
        }
    }

    TEST_CASE("2-Benchmark: KeyDir performance")
    {
        constexpr int KeySize                    = 8;
        constexpr int AccessQty                  = 1024 * 1024;
        constexpr int MaintenanceKeyDirBatchSize = 100'000;
        uint32_t      mapSize                    = 128 * 1024;
        if (testGetDuration() == TestDuration::Long) {
            mapSize = 1 * 1024 * 1024;
        } else if (testGetDuration() == TestDuration::Longest) {
            mapSize = 16 * 1024 * 1024;
        }
        KeyDirMap keyDir(detail::KeyStorageAllocBytes,
                         mapSize,  // We do not care about resizing
                         [&](uint32_t, bool isStart, bool) {
                             while (isStart && keyDir.isResizingOngoing()) keyDir.backgroundResizeWork(MaintenanceKeyDirBatchSize);
                         });
        keyDir.setMaxLoadFactor(0.95);
        lcVector<uint8_t> key(KeySize, 0);
        KeyChunk          entry{128, 0, NotStored, 0, 0, KeySize, 0, 0};
        OldKeyChunk       oldEntry;

        // Load the table
        uint64_t startTimeUs   = testGetTimeUs();
        int      lastLoadedKey = 90 * mapSize / 100;
        for (int opNbr = 0; opNbr < lastLoadedKey; ++opNbr) {
            *((uint32_t*)&key[0])  = opNbr;
            uint64_t keyHash       = LITECASK_HASH_FUNC(&key[0], KeySize);
            Status   storageStatus = keyDir.insertEntry((uint32_t)keyHash, &key[0], nullptr, entry, oldEntry);
            CHECK_EQ(storageStatus, Status::Ok);
            CHECK_EQ(oldEntry.isValid, false);
        }
        uint64_t durationUs = testGetTimeUs() - startTimeUs;

        printf("  > KeyDir lookup benchmark (%d table size, keySize=%d, 1 thread, %2.0f%% table load):\n", mapSize, KeySize,
               100. * keyDir.getLoadFactor());
        printf(
            "  >   Note: This is not a pure hashtable benchmark. Insertion implies key memory allocation.\n"
            "  >   Access implies reading and returning the stored data in this allocated structure.\n");

        printf("    Amortized load  time: %3" PRId64 " ns per entry  %6.3f Mop/s\n", (1000 * durationUs) / lastLoadedKey,
               (double)lastLoadedKey / (double)durationUs);

        // Benchmark the lookup for positive access
        startTimeUs = testGetTimeUs();
        for (int i = 0; i < AccessQty; ++i) {
            *((uint32_t*)&key[0]) = (uint32_t)(testGetRandom() % lastLoadedKey);
            uint64_t keyHash      = LITECASK_HASH_FUNC(&key[0], KeySize);
            bool     isFound      = keyDir.find((uint32_t)keyHash, &key[0], KeySize, entry);
            CHECK_EQ(isFound, true);
        }
        durationUs = testGetTimeUs() - startTimeUs;
        printf("    Positive access time: %3" PRId64 " ns per entry  %6.3f Mop/s\n", (1000 * durationUs) / AccessQty,
               (double)AccessQty / (double)durationUs);

        // Benchmark the lookup for negative access
        startTimeUs = testGetTimeUs();
        for (int i = 0; i < AccessQty; ++i) {
            *((uint32_t*)&key[0]) = (uint32_t)(mapSize + testGetRandom() % lastLoadedKey);
            uint64_t keyHash      = LITECASK_HASH_FUNC(&key[0], KeySize);
            bool     isFound      = keyDir.find((uint32_t)keyHash, &key[0], KeySize, entry);
            CHECK_EQ(isFound, false);
        }
        durationUs = testGetTimeUs() - startTimeUs;
        printf("    Negative access time: %3" PRId64 " ns per entry  %6.3f Mop/s\n", (1000 * durationUs) / AccessQty,
               (double)AccessQty / (double)durationUs);
    }

}  // End of test suite
