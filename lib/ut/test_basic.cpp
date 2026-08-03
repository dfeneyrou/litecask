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
#include <stdint.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "test_main.h"

using namespace litecask;
using namespace litecask::detail;

#define SETUP_DB()                                                                  \
    constexpr int KEY_SIZE     = 4;                                                 \
    constexpr int VALUE_SIZE   = 128;                                               \
    const char*   databasePath = "/tmp/litecask_test/basic";                        \
    Datastore::erasePermanentlyAllContent_UseWithCaution(databasePath);             \
    uint32_t          numberKey = 1;                                                \
    lcVector<uint8_t> value(VALUE_SIZE);                                            \
    for (int i = 0; i < VALUE_SIZE; ++i) { value[i] = ((uint8_t)i) & 0xFF; }        \
    lcVector<uint8_t> value2(VALUE_SIZE);                                           \
    for (int i = 0; i < VALUE_SIZE; ++i) { value2[i] = ((uint8_t)(i + 1)) & 0xFF; } \
    lcVector<uint8_t> retrievedValue;                                               \
    Status            s{Status::Ok};                                                \
    Datastore         store;                                                        \
    (void)KEY_SIZE;                                                                 \
    (void)numberKey;

TEST_SUITE("Basic")
{
    TEST_CASE("1-Sanity   : Structure sizes")
    {
        // Check packing works
        CHECK_EQ(sizeof(DataFileEntry), 16);
        CHECK_EQ(sizeof(HintFileEntry), 16);
    }

    TEST_CASE("1-Sanity   : Config consistency")
    {
        // Database cleanup and setup useful variables
        SETUP_DB();

        // Accept default config
        s = store.setConfig(store.getConfig());
        CHECK_EQ(s, Status::Ok);

        // Set a valid work config
        Config c;
        c.dataFileMaxBytes                            = 11'000;
        c.mergeCyclePeriodMs                          = 60'000;
        c.upkeepCyclePeriodMs                         = 1000;
        c.writeBufferFlushPeriodMs                    = 5000;
        c.upkeepKeyDirBatchSize                       = 100'000;
        c.upkeepValueCacheBatchSize                   = 1000;
        c.mergeTriggerDataFileFragmentationPercentage = 2;
        c.mergeTriggerDataFileDeadByteThreshold       = 10'000;
        c.mergeSelectDataFileFragmentationPercentage  = 1;
        c.mergeSelectDataFileDeadByteThreshold        = 9000;
        c.mergeSelectDataFileSmallSizeThreshold        = 8000;
        s                                             = store.setConfig(c);
        CHECK_EQ(s, Status::Ok);

        // Check out of bound and inconsistent values
        Config c2;
#define CHECK_BAD_PARAM_VALUE(paramName, badValue, status) \
    c2           = c;                                      \
    c2.paramName = badValue;                               \
    s            = store.setConfig(c2);                    \
    CHECK_EQ(s, Status::status);

        CHECK_BAD_PARAM_VALUE(dataFileMaxBytes, 1023, BadParameterValue);
        CHECK_BAD_PARAM_VALUE(mergeCyclePeriodMs, 0, BadParameterValue);
        CHECK_BAD_PARAM_VALUE(upkeepCyclePeriodMs, 0, BadParameterValue);
        // No constraint on writeBufferFlushPeriodMs
        CHECK_BAD_PARAM_VALUE(upkeepKeyDirBatchSize, 0, BadParameterValue);
        CHECK_BAD_PARAM_VALUE(upkeepValueCacheBatchSize, 0, BadParameterValue);
        CHECK_BAD_PARAM_VALUE(mergeTriggerDataFileFragmentationPercentage, 0, BadParameterValue);
        CHECK_BAD_PARAM_VALUE(mergeTriggerDataFileFragmentationPercentage, 101, BadParameterValue);
        CHECK_BAD_PARAM_VALUE(mergeTriggerDataFileDeadByteThreshold, 11001, InconsistentParameterValues);
        CHECK_BAD_PARAM_VALUE(mergeSelectDataFileFragmentationPercentage, 0, BadParameterValue);
        CHECK_BAD_PARAM_VALUE(mergeSelectDataFileFragmentationPercentage, 101, BadParameterValue);
        CHECK_BAD_PARAM_VALUE(mergeSelectDataFileFragmentationPercentage, 3, InconsistentParameterValues);
        CHECK_BAD_PARAM_VALUE(mergeSelectDataFileDeadByteThreshold, 10001, InconsistentParameterValues);
        CHECK_BAD_PARAM_VALUE(mergeSelectDataFileSmallSizeThreshold, 1023, BadParameterValue);
    }

    TEST_CASE("1-Sanity   : LockFile DB protection against multiple opening")
    {
        SETUP_DB();  // Removes any lock

        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        Datastore store2;
        s = store2.open(databasePath);
        CHECK_EQ(s, Status::StoreAlreadyInUse);

        s = store.close();
        CHECK_EQ(s, Status::Ok);
    }

    TEST_CASE("1-Sanity   : Base example in documentation")
    {
        // Note: this test does not comply to doctest check primitive because
        // it verifies public examples that is independent of doctest.
        const char* databasePath = "/tmp/litecask_test/basic";
        Datastore::erasePermanentlyAllContent_UseWithCaution(databasePath);
        lcVector<lcVector<uint8_t>> matchingKeys;

        // Create the databse
        litecask::Datastore store;
        store.open(databasePath);

        // Store an entry
        std::vector<uint8_t> value{1, 2, 3, 4, 5, 6, 7, 8};
        store.put("key", value);

        // Retrieve the entry
        std::vector<uint8_t> retrievedValue;
        store.get("key", retrievedValue);

        // Do something useful...
        assert(value == retrievedValue);

        // Querying
        store.put("UJohn Doe/CUS/TTax document/0001", value);
        store.put("UJohn Doe/CUS/TTax document/0001", value, {{0, 9}, {10, 3}, {14, 13}});

        // Query for user
        store.query("UJohn Doe", matchingKeys);
        assert(matchingKeys.size() == 1);

        // Query for country
        store.query("CUS", matchingKeys);
        assert(matchingKeys.size() == 1);

        // Query for user AND country (implicit AND)
        store.query({std::string("UJohn Doe"), std::string("CUS")}, matchingKeys);
        assert(matchingKeys.size() == 1);

        // Close the database
        store.close();
    }

    TEST_CASE("1-Sanity   : Implicit DB close at destruction time")
    {
        SETUP_DB();

        {
            Datastore store2;
            // Open
            CHECK_EQ(store2.getCounters().openCallQty, 0);
            s = store2.open(databasePath);
            CHECK_EQ(s, Status::Ok);
            CHECK_EQ(store2.getCounters().openCallQty, 1);
            CHECK_EQ(store2.getCounters().openCallFailedQty, 0);

            // Second opening shall fail
            s = store2.open(databasePath);
            CHECK_EQ(s, Status::StoreAlreadyOpen);
            CHECK_EQ(store2.getCounters().openCallFailedQty, 1);
            CHECK_EQ(store2.getCounters().openCallQty, 1);

            // Add an entry
            s = store2.put(&numberKey, 4, &value[0], VALUE_SIZE);
            CHECK_EQ(s, Status::Ok);

            // RAII: proper close without corruption shall occur
        }

        // Open
        CHECK_EQ(store.getCounters().openCallQty, 0);
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(store.getCounters().openCallQty, 1);
        CHECK_EQ(store.getCounters().openCallFailedQty, 0);

        // Get the previously put value
        s = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::Ok);

        CHECK_EQ(store.getCounters().closeCallQty, 0);
        s = store.close();
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(store.getCounters().closeCallQty, 1);
        CHECK_EQ(store.getCounters().closeCallFailedQty, 0);
        s = store.close();
        CHECK_EQ(s, Status::StoreNotOpen);
        CHECK_EQ(store.getCounters().closeCallQty, 1);
        CHECK_EQ(store.getCounters().closeCallFailedQty, 1);
    }

    TEST_CASE("1-Sanity   : Switch of active data file")
    {
        SETUP_DB();

        // Set a valid work config
        Config c;
        c.dataFileMaxBytes                            = 2048;
        c.mergeCyclePeriodMs                          = 60'000;
        c.upkeepCyclePeriodMs                         = 1000;
        c.writeBufferFlushPeriodMs                    = 5000;
        c.upkeepKeyDirBatchSize                       = 100'000;
        c.upkeepValueCacheBatchSize                   = 1000;
        c.mergeTriggerDataFileFragmentationPercentage = 60;
        c.mergeTriggerDataFileDeadByteThreshold       = 1024;
        c.mergeSelectDataFileFragmentationPercentage  = 40;
        c.mergeSelectDataFileDeadByteThreshold        = 1024;
        c.mergeSelectDataFileSmallSizeThreshold        = 1024;
        s                                             = store.setConfig(c);
        CHECK_EQ(s, Status::Ok);

        // Open
        CHECK_EQ(store.getCounters().activeDataFileSwitchQty, 0);
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(store.getCounters().activeDataFileSwitchQty, 1);

        // Add entries to the limit of the switch
        int entryQty = c.dataFileMaxBytes / (uint32_t)(sizeof(DataFileEntry) + KEY_SIZE + VALUE_SIZE);
        if (entryQty * (sizeof(DataFileEntry) + KEY_SIZE + VALUE_SIZE) == c.dataFileMaxBytes) { --entryQty; }
        for (int i = 0; i < entryQty; ++i) {
            s = store.put(&numberKey, 4, &value[0], VALUE_SIZE);
            CHECK_EQ(s, Status::Ok);
            ++numberKey;  // Create unique entries
        }
        CHECK_EQ(store.getCounters().activeDataFileSwitchQty, 1);

        // One more entry and we switch to another active file
        s = store.put(&numberKey, 4, &value[0], VALUE_SIZE);
        CHECK_EQ(s, Status::Ok);
        ++numberKey;  // Create unique entries
        CHECK_EQ(store.getCounters().activeDataFileSwitchQty, 2);

        s = store.close();
        CHECK_EQ(s, Status::Ok);
    }

    TEST_CASE("1-Sanity   : API basic stimulation")
    {
        // Database cleanup and setup useful variables
        SETUP_DB();

        // Calls before initialization
        CHECK_EQ(store.getCounters().putCallQty, 0);
        CHECK_EQ(store.getCounters().putCallFailedQty, 0);
        s = store.put(&numberKey, 4, &value[0], VALUE_SIZE);
        CHECK_EQ(s, Status::StoreNotOpen);
        CHECK_EQ(store.getCounters().putCallQty, 0);
        CHECK_EQ(store.getCounters().putCallFailedQty, 1);

        CHECK_EQ(store.getCounters().getCallQty, 0);
        CHECK_EQ(store.getCounters().getCallFailedQty, 0);
        s = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::StoreNotOpen);
        CHECK_EQ(store.getCounters().getCallQty, 0);
        CHECK_EQ(store.getCounters().getCallFailedQty, 1);

        CHECK_EQ(store.getCounters().removeCallQty, 0);
        CHECK_EQ(store.getCounters().removeCallFailedQty, 0);
        s = store.remove(&numberKey, 4);
        CHECK_EQ(s, Status::StoreNotOpen);
        CHECK_EQ(store.getCounters().removeCallQty, 0);
        CHECK_EQ(store.getCounters().removeCallFailedQty, 1);

        CHECK_EQ(store.getCounters().closeCallQty, 0);
        CHECK_EQ(store.getCounters().closeCallFailedQty, 0);
        s = store.close();
        CHECK_EQ(s, Status::StoreNotOpen);
        CHECK_EQ(store.getCounters().closeCallQty, 0);
        CHECK_EQ(store.getCounters().closeCallFailedQty, 1);

        // Open the database
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        // Add an entry
        s = store.put(&numberKey, 4, &value[0], VALUE_SIZE);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(store.getCounters().putCallQty, 1);
        CHECK_EQ(store.getCounters().putCallFailedQty, 1);

        // Check entry presence
        s = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(retrievedValue.size(), VALUE_SIZE);
        CHECK_EQ(retrievedValue[7], 7);
        CHECK_EQ(store.getCounters().getCallQty, 1);
        CHECK_EQ(store.getCounters().getCallNotFoundQty, 0);
        CHECK_EQ(store.getCounters().getCallCorruptedQty, 0);
        CHECK_EQ(store.getCounters().getCallFailedQty, 1);

        // Remove the entry
        CHECK_EQ(store.getCounters().removeCallQty, 0);
        s = store.remove(&numberKey, 4);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(store.getCounters().removeCallQty, 1);
        CHECK_EQ(store.getCounters().removeCallNotFoundQty, 0);
        CHECK_EQ(store.getCounters().removeCallFailedQty, 1);
        s = store.remove(&numberKey, 4);
        CHECK_EQ(s, Status::EntryNotFound);
        CHECK_EQ(store.getCounters().removeCallQty, 1);
        CHECK_EQ(store.getCounters().removeCallNotFoundQty, 1);
        CHECK_EQ(store.getCounters().removeCallFailedQty, 1);

        // Check entry absence
        s = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::EntryNotFound);
        CHECK_EQ(store.getCounters().getCallQty, 1);
        CHECK_EQ(store.getCounters().getCallNotFoundQty, 1);
        CHECK_EQ(store.getCounters().getCallCorruptedQty, 0);
        CHECK_EQ(store.getCounters().getCallFailedQty, 1);

        s = store.close();
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(store.getCounters().closeCallQty, 1);
        CHECK_EQ(store.getCounters().closeCallFailedQty, 1);
    }

    TEST_CASE("1-Sanity   : API variants")
    {
        // Database cleanup and setup useful variables
        SETUP_DB();

        // Open the database
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        std::vector<uint8_t>        keyVec{1, 2, 3, 4, 5, 6};
        std::string                 keyStr{"\x01\x02\x03\x04\x05\x06"};
        std::vector<KeyIndex>       indexes{{1, 4}};  // 1 index "\x02\x03\x04\x05"
        ArenaAllocator              alloc;
        lcVector<lcVector<uint8_t>> matchingKeys;
        lcVector<QueryResult>       arenaMatchingKeys;

        for (int putMethod = 0; putMethod < 5; ++putMethod) {
            for (int getMethod = 0; getMethod < 3; ++getMethod) {
                for (int queryMethod = 0; queryMethod < 8; ++queryMethod) {
                    for (int removeMethod = 0; removeMethod < 3; ++removeMethod) {
                        // Add an entry
                        switch (putMethod) {
                            case 0:
                                s = store.put(&keyVec[0], keyVec.size(), &value[0], VALUE_SIZE, indexes);
                                break;  // Add an entry by pointers
                            case 1:
                                s = store.put(keyVec, &value[0], VALUE_SIZE, indexes);
                                break;  // Add an entry with key as a vector
                            case 2:
                                s = store.put(keyStr, &value[0], VALUE_SIZE, indexes);
                                break;  // Add an entry with key as a string
                            case 3:
                                s = store.put(keyVec, value, indexes);
                                break;  // Add an entry with key as a vector and value as a vector
                            case 4:
                                s = store.put(keyStr, value, indexes);
                                break;  // Add an entry with key as a string and value as a vector
                            default:
                                break;
                        }
                        CHECK_EQ(s, Status::Ok);

                        // Get the entry
                        switch (getMethod) {
                            case 0:
                                s = store.get(&keyVec[0], keyVec.size(), retrievedValue);
                                break;  // Get an entry with key as a pointer and size
                            case 1:
                                s = store.get(keyVec, retrievedValue);
                                break;  // Get an entry with key as a vector
                            case 2:
                                s = store.get(keyStr, retrievedValue);
                                break;  // Get an entry with key as a string
                            default:
                                break;
                        }
                        CHECK_EQ(s, Status::Ok);
                        CHECK_EQ(retrievedValue.size(), VALUE_SIZE);
                        CHECK_EQ(retrievedValue[7], 7);

                        // Query the entry
                        switch (queryMethod) {
                            case 0:
                                s = store.query(lcVector<uint8_t>{2, 3, 4, 5}, matchingKeys);
                                break;  // Query key with single vector<uint8_t> API
                            case 1:
                                s = store.query("\x02\x03\x04\x05", matchingKeys);
                                break;  // Query key with single string API
                            case 2:
                                s = store.query(lcVector<lcVector<uint8_t>>{{2, 3, 4, 5}}, matchingKeys);
                                break;  // Query key withmultiple vector<uint8_t> API
                            case 3:
                                s = store.query(lcVector<lcString>{"\x02\x03\x04\x05"}, matchingKeys);
                                break;  // Query key with multiple string API
                            case 4:
                                s = store.query(lcVector<uint8_t>{2, 3, 4, 5}, arenaMatchingKeys, alloc);
                                break;  // Query key with single vector<uint8_t> API and arena output
                            case 5:
                                s = store.query("\x02\x03\x04\x05", arenaMatchingKeys, alloc);
                                break;  // Query key with single string API and arena output
                            case 6:
                                s = store.query(lcVector<lcVector<uint8_t>>{{2, 3, 4, 5}}, arenaMatchingKeys, alloc);
                                break;  // Query key withmultiple vector<uint8_t> API and arena output
                            case 7:
                                s = store.query(lcVector<lcString>{"\x02\x03\x04\x05"}, arenaMatchingKeys, alloc);
                                break;  // Query key with multiple string API and arena output
                            default:
                                break;
                        }
                        CHECK_EQ(s, Status::Ok);
                        CHECK(((matchingKeys.size() == 1) || (arenaMatchingKeys.size() == 1)));
                        matchingKeys.clear();
                        arenaMatchingKeys.clear();

                        // Remove the entry
                        switch (queryMethod) {
                            case 0:
                                s = store.remove(&keyVec[0], keyVec.size());
                                break;
                            case 1:
                                s = store.remove(keyVec);
                                break;
                            case 2:
                                s = store.remove(keyStr);
                                break;
                            default:
                                break;
                        }
                        CHECK_EQ(s, Status::Ok);
                    }
                }
            }
        }

        s = store.close();
        CHECK_EQ(s, Status::Ok);
    }

    TEST_CASE("1-Sanity   : Merge period")
    {
        // Database cleanup and setup useful variables
        SETUP_DB();

        // Set a valid work config
        Config c;
        c.dataFileMaxBytes                            = 11'000;
        c.mergeCyclePeriodMs                          = 50;
        c.upkeepCyclePeriodMs                         = 100;
        c.writeBufferFlushPeriodMs                    = 5000;
        c.upkeepKeyDirBatchSize                       = 100'000;
        c.upkeepValueCacheBatchSize                   = 1000;
        c.mergeTriggerDataFileFragmentationPercentage = 2;
        c.mergeTriggerDataFileDeadByteThreshold       = 10'000;
        c.mergeSelectDataFileFragmentationPercentage  = 1;
        c.mergeSelectDataFileDeadByteThreshold        = 9000;
        c.mergeSelectDataFileSmallSizeThreshold        = 8000;
        s                                             = store.setConfig(c);
        CHECK_EQ(s, Status::Ok);

        // Open
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        CHECK_EQ(store.getCounters().mergeCycleQty, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        CHECK_GE(store.getCounters().mergeCycleQty.load(), 1);
        CHECK_LE(store.getCounters().mergeCycleQty.load(), 4);  // Loose qty test because gdb and ASAN can be slow

        s = store.close();
        CHECK_EQ(s, Status::Ok);
    }

    TEST_CASE("1-Sanity   : Delete behavior subtlety when merging")
    {
        // Database cleanup and setup useful variables
        SETUP_DB();

        // Open
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        // File 1: value A
        s = store.put(&numberKey, 4, &value[0], VALUE_SIZE);
        CHECK_EQ(s, Status::Ok);
        s = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(retrievedValue[7], 7);

        store.createNewActiveDataFileUnlocked();
        s = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(retrievedValue.size(), VALUE_SIZE);
        CHECK_EQ(retrievedValue[7], 7);

        // File 2: value B
        s = store.put(&numberKey, 4, &value2[0], VALUE_SIZE);
        CHECK_EQ(s, Status::Ok);
        s = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(retrievedValue.size(), VALUE_SIZE);
        CHECK_EQ(retrievedValue[0], 1);

        store.createNewActiveDataFileUnlocked();
        s = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK_EQ(retrievedValue.size(), VALUE_SIZE);
        CHECK_EQ(retrievedValue[0], 1);

        // File 3: removed
        s = store.remove(&numberKey, 4);
        CHECK_EQ(s, Status::Ok);
        s = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::EntryNotFound);

        // Merge with file 1 & 3 selected (not file 2)
        lcVector<MergeFileInfo> mergeInfos    = {{0, {}}, {2, {}}};
        lcString                mergeBasename = store.createNewActiveDataFileUnlocked();
        store.createMergedDataFiles(mergeInfos, mergeBasename, 150 * 1024 * 1024);
        store.replaceDataFiles(mergeInfos);

        // Check: the key should not exist after reloading
        store.close();
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        s = store.get(&numberKey, 4, retrievedValue);
        CHECK_EQ(s, Status::EntryNotFound);

        s = store.close();
        CHECK_EQ(s, Status::Ok);

        // LATER: check that when compacting B, the database becomes empty
    }

    TEST_CASE("1-Sanity   : Big entries")
    {
        // Database cleanup and setup useful variables
        SETUP_DB();

        // Open
        s = store.open(databasePath);
        CHECK_EQ(s, Status::Ok);

        // Build the K V
        constexpr int     BigKeySize        = 65000;
        constexpr int     TooBigKeySize     = 65535;
        constexpr int     MuchTooBigKeySize = 66000;
        lcVector<uint8_t> bigKey(MuchTooBigKeySize);
        for (int i = 0; i < MuchTooBigKeySize; ++i) { bigKey[i] = (uint8_t)(i + 14); }
        constexpr int     BigValueSize = 2'000'000;  // Above the (default) write buffer and page cache size
        lcVector<uint8_t> bigValue(BigValueSize);
        for (int i = 0; i < BigValueSize; ++i) { bigValue[i] = (uint8_t)(i + 43); }

        // Check the big key size is ok
        s = store.put(&bigKey[0], BigKeySize, &bigValue[0], (uint32_t)bigValue.size());
        CHECK_EQ(s, Status::Ok);
        s = store.get(&bigKey[0], BigKeySize, retrievedValue);
        CHECK_EQ(s, Status::Ok);
        CHECK(!memcmp(&retrievedValue[0], &bigValue[0], BigValueSize));

        // Check the too big key size is refused politely
        s = store.put(&bigKey[0], TooBigKeySize, &bigValue[0], (uint32_t)bigValue.size());
        CHECK_EQ(s, Status::BadKeySize);

        // Check the really too big key size is also refused politely
        s = store.put(&bigKey[0], MuchTooBigKeySize, &bigValue[0], (uint32_t)bigValue.size());
        CHECK_EQ(s, Status::BadKeySize);

        // Close
        s = store.close();
        CHECK_EQ(s, Status::Ok);
    }

    TEST_CASE("1-Sanity   : Logs")
    {
        SETUP_DB();

        store.setTestLogMaxFileBytes(6000);

        // Open
        std::filesystem::path dbPath(databasePath);
        store.setLogLevel(LogLevel::Debug);
        s = store.open(dbPath);
        CHECK_EQ(s, Status::Ok);

        char myLog[256];
        memset(myLog, 'A', sizeof(myLog));
        myLog[255] = 0;

        // Check that the first log file is created
        for (int i = 0; i < 15; ++i) { store.log(LogLevel::Debug, "%s", myLog); }
        s = store.close();
        CHECK_EQ(s, Status::Ok);
        s = store.open(dbPath);
        CHECK_EQ(s, Status::Ok);
        CHECK(osGetFileSize(dbPath / "litecask.log") > 0);
        CHECK(osGetFileSize(dbPath / "litecask1.log") < 0);

        // Check that the first log file is appended
        // Additional open/close needed as log files are created at open time and can be buffered
        s = store.close();
        CHECK_EQ(s, Status::Ok);
        s = store.open(dbPath);
        CHECK_EQ(s, Status::Ok);
        s = store.close();
        CHECK_EQ(s, Status::Ok);
        s = store.open(dbPath);
        CHECK_EQ(s, Status::Ok);
        CHECK(osGetFileSize(dbPath / "litecask.log") > 0);
        CHECK(osGetFileSize(dbPath / "litecask1.log") < 0);

        // Check that a second log file is created when the first is full
        for (int i = 0; i < 15; ++i) { store.log(LogLevel::Debug, "%s", myLog); }
        s = store.close();
        CHECK_EQ(s, Status::Ok);
        s = store.open(dbPath);
        CHECK_EQ(s, Status::Ok);
        s = store.close();
        CHECK_EQ(s, Status::Ok);
        s = store.open(dbPath);
        CHECK_EQ(s, Status::Ok);
        CHECK(osGetFileSize(dbPath / "litecask.log") > 0);
        CHECK(osGetFileSize(dbPath / "litecask1.log") > 0);
        CHECK(osGetFileSize(dbPath / "litecask.log") < osGetFileSize(dbPath / "litecask1.log"));

        // Check that subsequent log files are created and others are renamed accordingly
        for (int j = 0; j < 20; ++j) {
            for (int i = 0; i < 20; ++i) { store.log(LogLevel::Debug, "%s", myLog); }
            s = store.close();
            CHECK_EQ(s, Status::Ok);
            s = store.open(dbPath);
            CHECK_EQ(s, Status::Ok);
        }
        s = store.close();
        CHECK_EQ(s, Status::Ok);
        s = store.open(dbPath);
        CHECK_EQ(s, Status::Ok);
        s = store.close();
        CHECK_EQ(s, Status::Ok);
        s = store.open(dbPath);
        CHECK_EQ(s, Status::Ok);
        CHECK(osGetFileSize(dbPath / "litecask5.log") > 0);
        CHECK(osGetFileSize(dbPath / "litecask6.log") < 0);
        CHECK(osGetFileSize(dbPath / "litecask.log") > 0);
        CHECK(osGetFileSize(dbPath / "litecask.log") < osGetFileSize(dbPath / "litecask1.log"));

        // Close
        s = store.close();
        CHECK_EQ(s, Status::Ok);
    }
}  // End of test suite
