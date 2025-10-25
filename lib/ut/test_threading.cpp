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

#include <stdint.h>

#include <filesystem>
#include <thread>
#include <vector>

#include "test_main.h"

using namespace litecask;
using namespace litecask::detail;

// Helpers
// =======

void
readThread(Datastore* store, uint32_t firstNumber, uint32_t qty)
{
    constexpr uint64_t MaxTries = 1000'000;
    lcVector<uint8_t>  retrievedValue;

    for (uint32_t numberKey = firstNumber; numberKey < firstNumber + qty; ++numberKey) {
        uint64_t consecutiveReadTries = 0;
        Status   s                    = Status::EntryNotFound;

        // Read each entry in order. Retries until found in the DB
        while (s != Status::Ok && consecutiveReadTries < MaxTries) {
            s = store->get(&numberKey, 4, retrievedValue);
            ++consecutiveReadTries;
        }
        CHECK(consecutiveReadTries < MaxTries);
    }
}

void
writeThread(Datastore* store, uint32_t firstNumber, uint32_t qty)
{
    constexpr int     VALUE_SIZE = 128;
    lcVector<uint8_t> value(VALUE_SIZE);

    for (uint32_t numberKey = firstNumber; numberKey < firstNumber + qty; ++numberKey) {
        for (int i = 0; i < VALUE_SIZE; ++i) { value[i] = ((uint8_t)numberKey) & 0xFF; }
        Status s = store->put(&numberKey, 4, &value[0], VALUE_SIZE);
        CHECK_EQ(s, Status::Ok);
    }
    // printf("WRITE: %u entries\n", qty);
}

// Tests
// =====
TEST_SUITE("Multithreading")
{
    TEST_CASE("1-Sanity   : One read one write")
    {
        // Database cleanup and setup useful variables
        const char* databasePath = "/tmp/litecask_test/threading";
        Datastore::erasePermanentlyAllContent_UseWithCaution(databasePath);

        Datastore store;
        Status    s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);
        uint32_t firstNumber = 0;
        uint32_t qty         = 10000;

        std::thread rt(readThread, &store, firstNumber, qty);
        std::thread wt(writeThread, &store, firstNumber, qty);

        wt.join();
        rt.join();

        CHECK_EQ(store.getCounters().getCallCorruptedQty, 0);
        CHECK_EQ(store.getCounters().getCallFailedQty, 0);
        CHECK_EQ(store.getCounters().getCallQty, qty);
        CHECK_EQ(store.getCounters().putCallQty, qty);

        // Check for corruption
        s = store.close();
        CHECK_EQ(s, Status::Ok);
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(store.getCounters().getCallCorruptedQty, 0);
        CHECK_EQ(store.getCounters().getCallFailedQty, 0);
    }

    TEST_CASE("1-Sanity   : One read two writes")
    {
        // Database cleanup and setup useful variables
        const char* databasePath = "/tmp/litecask_test/threading";
        Datastore::erasePermanentlyAllContent_UseWithCaution(databasePath);

        Datastore store;
        Status    s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        uint32_t firstNumber = 0;
        uint32_t qty         = 10000;

        std::thread rt(readThread, &store, firstNumber, qty);
        std::thread wt1(writeThread, &store, firstNumber, qty);
        std::thread wt2(writeThread, &store, firstNumber, qty);

        wt1.join();
        wt2.join();
        rt.join();

        // Check for corruption
        s = store.close();
        CHECK_EQ(s, Status::Ok);
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);
    }

}  // End of test suite
