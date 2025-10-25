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
#include <string>
#include <vector>

#include "test_main.h"

using namespace litecask;
using namespace litecask::detail;

#define SETUP_DB()                                                           \
    constexpr int VALUE_SIZE   = 128;                                        \
    const char*   databasePath = "/tmp/litecask_test/index";                 \
    Datastore::erasePermanentlyAllContent_UseWithCaution(databasePath);      \
    lcVector<uint8_t> value(VALUE_SIZE);                                     \
    for (int i = 0; i < VALUE_SIZE; ++i) { value[i] = ((uint8_t)i) & 0xFF; } \
    lcVector<uint8_t> retrievedValue;                                        \
    Status            s{Status::Ok};

TEST_SUITE("Indexes")
{
    TEST_CASE("1-Sanity   : Base query")
    {
        SETUP_DB();
        Datastore store;
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        lcVector<uint8_t>           key{0, 1, 2, 3, 4, 5, 6, 7, 8};
        lcVector<lcVector<uint8_t>> matchingKeys;

        // Put the entry with errors in index consistency or order
        s = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, {{0, 2}, {5, 0}});  // Null size
        CHECK_EQ(s, Status::InconsistentKeyIndex);
        s = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, {{0, 2}, {5, 5}});  // Out of key range
        CHECK_EQ(s, Status::InconsistentKeyIndex);
        s = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, {{5, 2}, {0, 2}});  // Unordered
        CHECK_EQ(s, Status::UnorderedKeyIndex);
        s = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, {{5, 3}, {5, 2}});  // Unordered
        CHECK_EQ(s, Status::UnorderedKeyIndex);
        s = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, {{5, 3}, {5, 3}});  // Unordered
        CHECK_EQ(s, Status::UnorderedKeyIndex);

        // Put the entry successfully
        s = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, {{0, 2}, {5, 3}});
        CHECK_EQ(s, Status::Ok);

        // Get entry from write buffer
        s = store.get(&key[0], (uint32_t)key.size(), retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(retrievedValue[7], 7);

        // Get entry from cache
        store.sync();  // Flushes the write buffer
        s = store.get(&key[0], (uint32_t)key.size(), retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(retrievedValue[7], 7);

        // Query key from the index with the first "tag"
        s = store.query(lcVector<lcVector<uint8_t>>({{0, 1}}), matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 1);

        // Query the key from the index with the second "tag"
        s = store.query(lcVector<lcVector<uint8_t>>{{5, 6, 7}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 1);
        CHECK(matchingKeys[0] == key);

        // Get the key from the index with the two "tags"
        s = store.query(lcVector<lcVector<uint8_t>>{{0, 1}, {5, 6, 7}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 1);
        CHECK(matchingKeys[0] == key);

        // Query without index
        s = store.query(lcVector<lcVector<uint8_t>>{}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 0);

        // Query with empty key part
        s = store.query(lcVector<lcVector<uint8_t>>{{}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 0);
    }

    TEST_CASE("1-Sanity   : Index update")
    {
        SETUP_DB();
        Datastore store;
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        lcVector<uint8_t>           key{0, 1, 2, 3, 4, 5, 6, 7, 8};
        lcVector<lcVector<uint8_t>> matchingKeys;

        // Put the entry successfully
        s = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, {{1, 2}, {5, 3}});
        CHECK_EQ(s, Status::Ok);

        // Override the entry one index changed and not the other one
        s = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, {{0, 2}, {5, 3}});
        CHECK_EQ(s, Status::Ok);

        // Query key from the index with the first "tag"
        s = store.query(lcVector<lcVector<uint8_t>>{{0, 1}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 1);

        // Query the key from the index with the second "tag"
        s = store.query(lcVector<lcVector<uint8_t>>{{5, 6, 7}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 1);
        CHECK(matchingKeys[0] == key);

        // Get the key from the index with the two "tags"
        s = store.query(lcVector<lcVector<uint8_t>>{{0, 1}, {5, 6, 7}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 1);
        CHECK(matchingKeys[0] == key);

        // Override the entry and keep only the first index
        s = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, {{0, 2}});
        CHECK_EQ(s, Status::Ok);

        // Query key from the index with the first "tag"
        s = store.query(lcVector<lcVector<uint8_t>>{{0, 1}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 1);

        // Query the key from the index with the second "tag"
        s = store.query(lcVector<lcVector<uint8_t>>{{5, 6, 7}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 0);
    }

    TEST_CASE("1-Sanity   : Many indexes")
    {
        SETUP_DB();
        Datastore store;
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        lcVector<uint8_t> key;
        for (uint32_t i = 0; i < MaxKeyIndexQty * 2 + 2; ++i) { key.push_back((uint8_t)i); }
        lcVector<lcVector<uint8_t>> matchingKeys;

        // Update the same entry with increasing quantity of index
        lcVector<KeyIndex> keyIndexes;
        for (uint32_t keyIndexQty = 0; keyIndexQty <= MaxKeyIndexQty; ++keyIndexQty) {
            s = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, keyIndexes);
            CHECK_EQ(s, Status::Ok);
            keyIndexes.push_back({(uint8_t)keyIndexQty, (uint8_t)(keyIndexQty + 1)});
        }

        // Update with one key index more than the acceptable amount
        s = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, keyIndexes);
        CHECK_EQ(s, Status::InconsistentKeyIndex);

        // Check the queries with these index (single index and all indexes)
        lcVector<lcVector<uint8_t>> queryKeyParts;
        for (uint32_t keyIndexQty = 0; keyIndexQty < MaxKeyIndexQty; ++keyIndexQty) {
            // Build the new key part for query
            lcVector<uint8_t> newKeyPart;
            for (uint32_t i = keyIndexQty; i < keyIndexQty + (keyIndexQty + 1); ++i) { newKeyPart.push_back((uint8_t)i); }

            // Single
            s = store.query(lcVector<lcVector<uint8_t>>{newKeyPart}, matchingKeys);
            CHECK_EQ(s, Status::Ok);
            CHECK_EQ(matchingKeys.size(), 1);

            // AND of all key parts
            queryKeyParts.push_back(newKeyPart);
            s = store.query(queryKeyParts, matchingKeys);
            CHECK_EQ(s, Status::Ok);
            CHECK_EQ(matchingKeys.size(), 1);
        }
    }

    TEST_CASE("1-Sanity   : Indexed entry removal")
    {
        SETUP_DB();
        Datastore store;
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        lcVector<uint8_t>           key{0, 1, 2, 3, 4, 5, 6, 7, 8};
        lcVector<lcVector<uint8_t>> matchingKeys;

        // Put the entry successfully
        s = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, {{1, 2}, {5, 3}});
        CHECK_EQ(s, Status::Ok);

        // Query key from the index with the first "tag"
        s = store.query(lcVector<lcVector<uint8_t>>{{1, 2}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 1);

        // Remove the entry
        s = store.remove(&key[0], (uint32_t)key.size());
        CHECK_EQ(s, Status::Ok);

        // Query removed key from the index with the first "tag"
        s = store.query(lcVector<lcVector<uint8_t>>{{1, 2}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 0);

        // Put back the entry (with extra indexes)
        s = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, {{1, 2}, {2, 1}, {2, 2}, {2, 3}, {2, 4}, {5, 3}});
        CHECK_EQ(s, Status::Ok);

        // Query key from the index with the first "tag"
        s = store.query(lcVector<lcVector<uint8_t>>{{1, 2}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 1);
    }

    TEST_CASE("1-Sanity   : Index cleaning")
    {
        SETUP_DB();
        Datastore store;
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        lcVector<uint8_t>           key{0, 29, 19};
        lcVector<lcVector<uint8_t>> matchingKeys;

        // Fill with 100 keys, indexed with the same key part
        for (int i = 0; i < 100; ++i) {
            key[0] = (uint8_t)i;
            s      = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, {{1, 2}});
            CHECK_EQ(s, Status::Ok);
        }

        // Query the key part, it shall return the 100 items
        CHECK_EQ(store.getCounters().queryCallQty, 0);
        s = store.query(lcVector<lcVector<uint8_t>>{{29, 19}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 100);
        CHECK_EQ(store.getCounters().queryCallQty, 1);

        // Remove a few  entries
        for (int i = 10; i < 13; ++i) {
            key[0] = (uint8_t)i;
            s      = store.remove(&key[0], (uint32_t)key.size());
            CHECK_EQ(s, Status::Ok);
        }

        // Query the key part, it shall return the 100-3 items
        s = store.query(lcVector<lcVector<uint8_t>>{{29, 19}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 100 - 3);

        // Remove a few more  entries so that 10+ indexes have been removed
        for (int i = 13; i < 25; ++i) {
            key[0] = (uint8_t)i;
            s      = store.remove(&key[0], (uint32_t)key.size());
            CHECK_EQ(s, Status::Ok);
        }

        // Query the key part, it shall return the 100-15 items
        CHECK_EQ(store.getCounters().indexArrayCleaningQty, 0);
        CHECK_EQ(store.getCounters().indexArrayCleanedEntries, 0);
        s = store.query(lcVector<lcVector<uint8_t>>{{29, 19}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 100 - 15);
        CHECK_EQ(store.getCounters().indexArrayCleaningQty, 1);
        CHECK_EQ(store.getCounters().indexArrayCleanedEntries, 15);
    }

    TEST_CASE("1-Sanity   : Query variants")
    {
        SETUP_DB();
        Datastore store;
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        lcVector<uint8_t>           key{1, 2, 3, 4, 5, 6, 7, 8, 9};
        lcVector<lcVector<uint8_t>> matchingKeys;

        // Put the entry
        s = store.put(&key[0], (uint32_t)key.size(), &value[0], VALUE_SIZE, {{1, 2}, {5, 3}});
        CHECK_EQ(s, Status::Ok);

        // Query key with single vector<uint8_t> API
        s = store.query(lcVector<uint8_t>{2, 3}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 1);

        // Query key with single string API
        s = store.query(lcString{2, 3}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 1);

        // Query key with multiple vector<uint8_t> API
        s = store.query(lcVector<lcVector<uint8_t>>{{2, 3}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 1);

        // Query key with multiple string API
        s = store.query(lcVector<lcString>{{2, 3}}, matchingKeys);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(matchingKeys.size(), 1);

        // Query key with single vector<uint8_t> API and arena output
        ArenaAllocator        alloc;
        lcVector<QueryResult> arenaMatchingKeys;
        s = store.query(lcVector<uint8_t>{2, 3}, arenaMatchingKeys, alloc);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(arenaMatchingKeys.size(), 1);

        // Query key with single string API and arena output
        s = store.query(lcString{2, 3}, arenaMatchingKeys, alloc);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(arenaMatchingKeys.size(), 1);

        // Query key with multiple vector<uint8_t> API and arena output
        s = store.query(lcVector<lcVector<uint8_t>>{{2, 3}}, arenaMatchingKeys, alloc);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(arenaMatchingKeys.size(), 1);

        // Query key with single string API and arena output
        s = store.query(lcVector<lcString>{{2, 3}}, arenaMatchingKeys, alloc);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(arenaMatchingKeys.size(), 1);
    }

}  // End of test suite
