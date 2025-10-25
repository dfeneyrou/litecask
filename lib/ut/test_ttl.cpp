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
#include <functional>
#include <vector>

#include "test_main.h"

using namespace litecask;
using namespace litecask::detail;

#define SETUP_DB()                                                                  \
    constexpr int KEY_SIZE     = 4;                                                 \
    constexpr int VALUE_SIZE   = 128;                                               \
    const char*   databasePath = "/tmp/litecask_test/ttl";                          \
    Datastore::erasePermanentlyAllContent_UseWithCaution(databasePath);             \
    uint32_t          numberKey = 1;                                                \
    lcVector<uint8_t> value(VALUE_SIZE);                                            \
    for (int i = 0; i < VALUE_SIZE; ++i) { value[i] = ((uint8_t)i) & 0xFF; }        \
    lcVector<uint8_t> value2(VALUE_SIZE);                                           \
    for (int i = 0; i < VALUE_SIZE; ++i) { value2[i] = ((uint8_t)(i + 1)) & 0xFF; } \
    lcVector<uint8_t> value3(VALUE_SIZE);                                           \
    for (int i = 0; i < VALUE_SIZE; ++i) { value3[i] = ((uint8_t)(2 + 1)) & 0xFF; } \
    lcVector<uint8_t>           retrievedValue;                                     \
    lcVector<lcVector<uint8_t>> matchingKeys;                                       \
    Status                      s{Status::Ok};                                      \
    Datastore                   store;                                              \
    (void)KEY_SIZE;                                                                 \
    (void)numberKey;

TEST_SUITE("Time to live")
{
    TEST_CASE("1-Sanity   : Simple TTL")
    {
        // Database cleanup and setup useful variables
        SETUP_DB();

        // Set the time hook
        uint32_t officialTimeSec = 0;
        store.setTestTimeFunction([&officialTimeSec]() { return officialTimeSec; });

        // Open the database
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        // Add an entry without TTL, Indexed on the key part containing the single byte '\0'
        numberKey = 1;
        s         = store.put(&numberKey, 4, &value[0], VALUE_SIZE, {{1, 1}}, 0);
        CHECK_EQ(s, Status::Ok);

        // Add an entry with TTL of 10s
        numberKey = 2;
        s         = store.put(&numberKey, 4, &value2[0], VALUE_SIZE, {{1, 1}}, 10);
        CHECK_EQ(s, Status::Ok);

        // Add an entry with TTL of 20s
        numberKey = 3;
        s         = store.put(&numberKey, 4, &value[0], VALUE_SIZE, {{1, 1}}, 20);
        CHECK_EQ(s, Status::Ok);

        for (uint32_t newTimeSec = 5; newTimeSec < 30; newTimeSec += 10) {
            // Set the time
            officialTimeSec = newTimeSec;
            store.updateNow();

            // Check entry 1 presence always
            numberKey = 1;
            s         = store.get(&numberKey, 4, retrievedValue);
            CHECK_EQ(s, Status::Ok);

            // Check entry 2 presence
            numberKey = 2;
            s         = store.get(&numberKey, 4, retrievedValue);
            CHECK_EQ(s, (officialTimeSec < 10) ? Status::Ok : Status::EntryNotFound);

            // Check entry 3 presence
            numberKey = 3;
            s         = store.get(&numberKey, 4, retrievedValue);
            CHECK_EQ(s, (officialTimeSec < 20) ? Status::Ok : Status::EntryNotFound);

            // Check the query
            s = store.query(lcVector<uint8_t>{0}, matchingKeys);
            CHECK_EQ(s, Status::Ok);
            CHECK_EQ(matchingKeys.size(), (officialTimeSec < 10) ? 3 : ((officialTimeSec < 20) ? 2 : 1));
        }

        s = store.close();
        CHECK_EQ(s, Status::Ok);
    }
}
