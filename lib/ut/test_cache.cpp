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

#define SETUP_DB()                                                                  \
    constexpr int KEY_SIZE     = 4;                                                 \
    constexpr int VALUE_SIZE   = 128;                                               \
    const char*   databasePath = "/tmp/litecask_test/cache";                        \
    Datastore::erasePermanentlyAllContent_UseWithCaution(databasePath);             \
    uint32_t          numberKey = 1;                                                \
    lcVector<uint8_t> value(VALUE_SIZE);                                            \
    for (int i = 0; i < VALUE_SIZE; ++i) { value[i] = ((uint8_t)i) & 0xFF; }        \
    lcVector<uint8_t> value2(VALUE_SIZE);                                           \
    for (int i = 0; i < VALUE_SIZE; ++i) { value2[i] = ((uint8_t)(i + 1)) & 0xFF; } \
    lcVector<uint8_t> retrievedValue;                                               \
    Status            s{Status::Ok};                                                \
    (void)KEY_SIZE;                                                                 \
    (void)numberKey;

TEST_SUITE("Value cache")
{
    TEST_CASE("1-Sanity   : High level behavior")
    {
        SETUP_DB();
        Datastore store;
        store.setWriteBufferBytes(0);  // No write buffer, which masks partially the cache behavior
        store.setLogLevel(LogLevel::Warn);
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        const ValueCacheCounters& stats = store.getValueCacheCounters();

        // Store
        CHECK_EQ(stats.currentInCacheValueQty.load(), 0);
        s = store.put(&numberKey, 4, &value[0], VALUE_SIZE);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(stats.currentInCacheValueQty.load(), 1);

        // Update
        s = store.put(&numberKey, 4, &value2[0], VALUE_SIZE);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(stats.currentInCacheValueQty.load(), 1);

        // Removed
        s = store.remove(&numberKey, 4);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(stats.currentInCacheValueQty.load(), 0);

        s = store.close();
        CHECK_EQ(s, Status::Ok);
    }

    TEST_CASE("1-Sanity   : LRU eviction")
    {
        SETUP_DB();
        constexpr int cacheByteSize = 1024 * 1024;
        Datastore     store;
        store.setWriteBufferBytes(0);  // No write buffer, which masks partially the cache behavior
        store.setLogLevel(LogLevel::Warn);

        // Open the store
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        // Saturate it with entries
        uint32_t opQty = cacheByteSize / VALUE_SIZE;  // Cache fully filled
        for (uint32_t opNbr = 0; opNbr < opQty; ++opNbr) {
            numberKey               = opNbr;
            *((uint32_t*)&value[0]) = opNbr;
            s                       = store.put(&numberKey, 4, &value[0], VALUE_SIZE);
            CHECK_EQ(s, Status::Ok);
        }

        // Count the eviction count
        const ValueCacheCounters& stats = store.getValueCacheCounters();
        CHECK_EQ(stats.insertCallQty, opQty);

        // Check the LRU behavior
        // First check the entries which are still inside the cache, counted as "hit"
        CHECK_EQ(stats.hitQty.load(), 0);
        uint32_t evictedQty = (uint32_t)stats.evictedQty.load();
        for (uint32_t opNbr = evictedQty; opNbr < opQty; ++opNbr) {
            numberKey = opNbr;
            s         = store.get(&numberKey, 4, retrievedValue);
            CHECK_EQ(s, Status::Ok);
            CHECK_EQ(retrievedValue[7], 7);
        }
        CHECK_EQ(stats.hitQty.load(), opQty - evictedQty);

        // Then check the entries that have been evicted cache and are counted as "miss"
        for (uint32_t opNbr = 0; opNbr < evictedQty; ++opNbr) {
            numberKey = opNbr;
            s         = store.get(&numberKey, 4, retrievedValue);
            CHECK_EQ(s, Status::Ok);
            CHECK_EQ(retrievedValue[7], 7);
        }
        CHECK_EQ(stats.missQty.load(), evictedQty);
        CHECK_EQ(stats.hitQty.load(), opQty - evictedQty);

        s = store.close();
        CHECK_EQ(s, Status::Ok);
    }

    TEST_CASE("1-Sanity   : LRU bumping")
    {
        SETUP_DB();
        constexpr int cacheByteSize = 1024 * 1024;
        Datastore     store(cacheByteSize);
        store.setWriteBufferBytes(0);  // No write buffer, which masks partially the cache behavior
        store.setLogLevel(LogLevel::Warn);

        // Open the store
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        const ValueCacheCounters& stats    = store.getValueCacheCounters();
        uint32_t                  maxOpQty = cacheByteSize / VALUE_SIZE;  // Element qty that fills the cache for sure

        auto doLruMaintenance = [&]() {
            bool requestOk = store.requestUpKeeping();
            CHECK_EQ(requestOk, true);
            for (int round = 0; store.isUpkeepingOnGoing() && round < 20; ++round) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        };

        // Fill the cache at 50 %
        uint32_t halfOpQty = 0;
        do {
            numberKey = halfOpQty;
            s         = store.put(&numberKey, 4, &value[0], VALUE_SIZE);
            CHECK_EQ(s, Status::Ok);
            ++halfOpQty;

            // Stop when the cache is half filled
            double fillRatio = (double)store.getValueCacheAllocatedBytes() / (double)store.getValueCacheMaxAllocatableBytes();
            if (fillRatio >= 0.5) break;
        } while (halfOpQty >= maxOpQty ||
                 (double)store.getValueCacheAllocatedBytes() / (double)store.getValueCacheMaxAllocatableBytes() < 0.5);
        CHECK(halfOpQty < maxOpQty);

        // Read the key "3" so that it is moved to the warm queue
        CHECK_EQ(stats.hitQty.load(), 0);
        numberKey = 3;
        s         = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(retrievedValue[7], 7);
        CHECK_EQ(stats.hitQty.load(), 1);

        // Load and saturate the cache
        for (uint32_t opNbr = halfOpQty; opNbr < maxOpQty; ++opNbr) {
            numberKey = opNbr;
            s         = store.put(&numberKey, 4, &value[0], VALUE_SIZE);
            CHECK_EQ(s, Status::Ok);
        }

        // Check that the key "4" is missed, because evicted (basic eviction)
        CHECK_EQ(stats.missQty.load(), 0);
        numberKey = 4;
        s         = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(stats.missQty.load(), 1);

        // Check that the "3" (accessed one) is still inside the cache (warm queue effect)
        CHECK_EQ(stats.hitQty.load(), 1);
        numberKey = 3;
        s         = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(stats.missQty.load(), 1);
        CHECK_EQ(stats.hitQty.load(), 2);

        // Fully refill the cache with new keys, as a scan would do
        for (uint32_t opNbr = maxOpQty; opNbr < 2 * maxOpQty; ++opNbr) {
            numberKey = opNbr;
            s         = store.put(&numberKey, 4, &value[0], VALUE_SIZE);
            CHECK_EQ(s, Status::Ok);
        }

        doLruMaintenance();

        // Check that the "accessed one" is still inside the cache (scan resistance due to warm queue)
        CHECK_EQ(stats.hitQty.load(), 2);
        numberKey = 3;
        s         = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(stats.missQty.load(), 1);
        CHECK_EQ(stats.hitQty.load(), 3);

        // Read more than 40% of entries (but not "3") so that the key "3" moves to the cold LRU queue
        for (uint32_t opNbr = maxOpQty + halfOpQty; opNbr < 2 * maxOpQty; ++opNbr) {
            numberKey = opNbr;
            s         = store.get(&numberKey, 4, retrievedValue);
            CHECK_EQ(s, Status::Ok);
        }

        doLruMaintenance();

        // Check that the "3" (accessed one) is still inside the cache (bumped back to warm queue)
        uint64_t hitQty = stats.hitQty.load();
        numberKey       = 3;
        s               = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(stats.hitQty.load(), hitQty + 1);

        // Fill the cache with new keys, as a scan would do
        for (uint32_t opNbr = 2 * maxOpQty; opNbr < 3 * maxOpQty; ++opNbr) {
            numberKey = opNbr;
            s         = store.put(&numberKey, 4, &value[0], VALUE_SIZE);
            CHECK_EQ(s, Status::Ok);
        }

        doLruMaintenance();

        // Check that the "accessed one" is still inside the cache (previous intra-bump in warm queue was effective)
        numberKey = 3;
        s         = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(retrievedValue[7], 7);
        CHECK_EQ(stats.hitQty.load(), hitQty + 2);

        // Fully refill the cache with new keys and make them active (renew the warm queue)
        for (uint32_t opNbr = 2 * maxOpQty; opNbr < 3 * maxOpQty; ++opNbr) {
            numberKey = opNbr;
            s         = store.put(&numberKey, 4, &value[0], VALUE_SIZE);
            CHECK_EQ(s, Status::Ok);
            s = store.get(&numberKey, 4, retrievedValue);
            CHECK_EQ(s, Status::Ok);
        }

        doLruMaintenance();

        // Check that the key "3" is now missed, because evicted
        uint64_t missQty = stats.missQty.load();
        numberKey        = 3;
        s                = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(stats.missQty.load(), missQty + 1);
    }

}  // End of test suite
