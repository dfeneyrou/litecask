![litecasks-logo](https://github.com/dfeneyrou/test/blob/main/doc/images/litecask-logo.png)

[![Build and check](https://github.com/dfeneyrou/test/actions/workflows/build.yml/badge.svg)](https://github.com/dfeneyrou/test/actions/workflows/build.yml)

## Litecask: a high performance embeddable persistent key-value store
 - C++17 **single-header**
 - Based on [`Bitcask`](https://riak.com/assets/bitcask-intro.pdf)
 - High performance
   - Insertion rate bottleneck is the disk I/O saturation
   - Lookup throughput benefits from a scalable **concurrent hashtable** and a **built-in memory cache**
 - **Crash friendliness** because architectured as a log-structured file systems, only the non-disk-flushed data are lost
 - Ability to handle datasets much larger than RAM without degradation: only the keys reside in memory
 - Easy software integration: copying 1 header file is enough, **no external dependencies**
 - Ease of backup and restore: backuping 1 flat directory is enough
 - Support of **indexation using parts of the keys**
 - Support of entry lifetime

<ins>Litecask is:</ins>
 - an efficient embedded database as a **building block** for your application
 - a **lean library** with an opinionated set of features, to maximize control and limit entropy
   - tracked internal performance (hashtable, allocator, cache, startup time...)
   - consistency by using ASAN, TSAN, clang-format, clang-tidy, and custom tests based on [`doctest`](https://github.com/doctest/doctest)
 - an enhanced implementation of [`Bitcask`](https://riak.com/assets/bitcask-intro.pdf), additionally featuring:
   - a built-in cache
   - indexing capabilities
 - a single header file, including:
   - a [`TLSF`](http://www.gii.upv.es/tlsf) heap-based memory allocator
   - a concurrent hashtable with optimistic locking (inspired by [`memC3`](https://github.com/efficient/memc3) and [this analysis](https://memcached.org/blog/paper-review-memc3/))
   - a lock-free RW-lock (aka shared mutex) reducing both false sharing and kernel access
   - a memory cache with segmented LRU (inspired by [`memcached`](https://memcached.org/blog/modern-lru))

<ins>Litecask is not:</ins>
 - a remote database
   - it misses some layers: a high performance network part (asio, evpp...) with a client-server communication protocol
 - a billion entries database. Indeed, a fundament of [`Bitcask`](https://riak.com/assets/bitcask-intro.pdf) is to keep the key directory in memory.
   - 100 millions entries is however in its range, depending on average key size and available RAM (value size does not matter, up to ~256 TB total)
   - scaling horizontally would imply making the database remote and sharded

## Getting started

A simple example inserting and retrieving a value is shown below:
```C++
// example.cpp . Place the file litecask.h is in the same folder
// Build with: 'c++ --std=c++17 example.cpp -o example' (Linux) or 'cl.exe /std:c++17 /EHsc example.cpp' (Windows)

#include "litecask.h"

int main(int argc, char** argv)
{
    litecask::Datastore store;
    store.open("/tmp/my_temp_db");

    // Store an entry
    std::vector<uint8_t> value{1,2,3,4,5,6,7,8};
    store.put("my key identifier", value);

    // Retrieve the entry
    std::vector<uint8_t> retrievedValue;
    store.get("my key identifier", retrievedValue);
    assert(retrievedValue==value);

    store.close();
}
```

## Benchmarks

Performance are highly dependent on the hardware (CPU quantity, CPU caches, memory bandwidth, disk speed, OS...).  
The results in this section correspond to a laptop with 16 CPUs (i7-11800H @2.30GHz) on Linux, obtained with the built-in benchmarks (matplotlib is required to draw the graphs in the optional last command):
```sh
mkdir build
cd build
cmake ..
make -j $(nproc)
./bin/litecask_test benchmark -ll
../ci/benchmark -n
```

### Access

#### Monothread performance

![litecasks-logo](https://github.com/dfeneyrou/test/blob/main/doc/images/litecask_benchmark_throughput_monothread.png)

Result for a 1 million entries database, 8 bytes keys, values in cache and Zipf-1.0 access distribution.

The memory throughput graph on the right is deduced from the left graph and the size of the value.  
The lower rate when writing is due to disk I/O bottleneck, highlighted by the asymptote when value size grows.

#### Multithread performance
![litecasks-logo](https://github.com/dfeneyrou/test/blob/main/doc/images/litecask_benchmark_throughput_multithread.png)

Result for a 1 million entries database, 8 bytes keys, 256 bytes values, values in cache and Zipf-1.0 access distribution.

The read access scales well with the thread quantity thanks to the concurrent hashtable and shared lock implementations.  
The full write does not scale with multi-threading due to the one-writer constraint (log-structured design).

### Other characteristics

Measures for a 30 million entries datastore with 8 bytes keys and 16 bytes value size:
| Characteristic       |   Performance   |
|-------------------|-----------------|
| Total startup load time   | 3.975 seconds (regardless of value sizes) |
| Total Used memory         | 2064 MB  (regardless of value sizes) |
| Startup load rate   | 7.5 million entries / second |
| RAM / entry         | 61 bytes + the key size + 2 bytes per index (global averaged overhead) |
| Disk size / entry   | 16 bytes + the key size + 2 bytes per index + the value size |

<details>
<summary>Effect of deferred write</summary>

<br/>
Deferred write is simply writing in an intermediate memory buffer to avoid costly kernel disk write calls. This buffer is flushed on disk only if full or non empty after a configurable timeout.

In case of process disruption, a drawback is the potential loss of these not-yet-on-disk entries.

Conditions: *monothread, key size 8 bytes, value size 8 bytes, write buffer size 100KB*
| Write kind       |   Performance   |
|-------------------|-----------------|
| Deferred sync  | 4.170 Mop/s  |
| Forced   sync  | 1.139 Mop/s  |

</details>

<details>
<summary>Effect of value cache</summary>

<br/>
A cache keeps the value of some entries in memory so that their access does not require a costly disk read.
The strategy of selecting which value to keep or not is implementation dependent.

Conditions:  *monothread, key size 8 bytes, value size 256 bytes, 1 million entries, zipf-1.0 access pattern*
| Cache size percentage       |   Performance   | Cache hit |
|-------------------|-----------------|--------------------|
| 0%   | 2.594 Mop/s  | 0.0%  |
| 25%   | 6.132 Mop/s  | 96.0%  |
| 50%   | 6.167 Mop/s  | 96.2%  |
| 90%   | 6.226 Mop/s  | 96.8%   |
| 100%   | 7.217 Mop/s  | 100%  |

Note: the cache effect is even bigger effect with multithreaded access.
</details>

## Indexation and query

### Principle

By design and without any upper layer library, a key-value store has limited capabilities to scan and query:
 - Log Structured Hash tables scan by peering at each entry.
 - Log Structured Merge trees internally sort keys, allowing key-range based scans.

Litecask, under the first category, proposes a different approach: **use parts of the key as indexes**.  
A high level view of this internal behavior could be the usage of a dedicated hash table that takes an array of bytes as input and returns a set of unique keys as output.

### Example of use

Let's consider the following entry with a text-based key:
```C++
store.put("UJohn Doe/CUS/TTax document/0001", value);
```

The text key can be visually split into:
 - `UJohn Doe` from byte 0, length 9
 - `CUS` from byte 10, length 3
 - `TTax document` from byte 14, length 13

These chunks of keys can be used as an index by upgrading the previous insertion command as follows:
```C++
store.put("UJohn Doe/CUS/TTax document/0001", value, { {0,9}, {10, 3}, {14, 13} });
```

Thanks to this lightweight indexes, it is now possible to query for the user `UJohn Doe`, the country `CUS`, or type `TTax document` entries. Or an intersection of these.
```C++
std::vector<std::vector<uint8_t>> matchingKeys;

// Query for user
store.query("UJohn Doe", matchingKeys);
assert(matchingKeys.size()==1);

// Query for country
store.query("CUS", matchingKeys);
assert(matchingKeys.size()==1);

// Query for user AND country (implicit AND. OR can be performed by additional queries and removing duplicates)
store.query({std::string("UJohn Doe"), std::string("CUS")}, matchingKeys);
assert(matchingKeys.size()==1);
```

Note: in this example, the prefixes `U`, `C` and `T` prevent mixing "columns" in case of same content.  
The separating '/' is purely for human readability.

## API

#### Datastore

<details>
<summary><code>Datastore::Datastore(...)</code> - Datastore instance creation </summary>

```C++
Datastore::Datastore(size_t cacheBytes = 256 * 1024 * 1024);
```

| Parameter name              |   Description                       |
|-------------------|-------------------------------------|
| `cacheBytes`   | Defines the maximum memory usage of the value cache in bytes. Default is 256 MB. <br/> Once this size is reached, values are evicted to allow insertion of new ones. <br/> Value cache greatly improves read performance by avoiding disk access. |

</details>

<details>
<summary><code>Status Datastore::open(...)</code> - Datastore opening </summary>

```C++
Status Datastore::open(std::filesystem::path dbDirectoryPath, bool doCreateIfNotExist = true);
 ```

| Parameter name              |   Description                         |
|-------------------|-------------------------------------|
| `dbDirectoryPath`   | The path of the litecask datastore |
| `doCreateIfNotExist`   | Boolean to decide to create a non existing datastore (default) |
<br/>

| Return code             |   Comment                         |
|-------------------|-------------------------------------|
| `Status::Ok`   | The datastore was successfully opened |
| `Status::StoreAlreadyOpen`   | The datastore instance is already in opened state |
| `Status::CannotOpenStore`   | The provided path does not correspond to a litecask store |
| `Status::StoreAlreadyInUse` | The datastore files are already in use by another process |
| `Status::BadDiskAccess` | The datastore cannot be opened due to file access issues |

</details>

<details>
 <summary><code>Status Datastore::close(...)</code> - Datastore closing </summary>

```C++
Status Datastore::close();
 ```

| Return code             |   Comment                         |
|-------------------|-------------------------------------|
| `Status::Ok`   | The datastore was successfully opened |
| `Status::StoreNotOpen`   | The datastore instance was not in opened state |

</details>

<details>
 <summary><code>void Datastore::sync(...)</code> - Flush the write buffer </summary>

```C++
void Datastore::sync();
 ```

This synchronization of the written data to the disk is performed:
 - When this function `sync()` is called explicitely
 - When an entry is inserted with the `forceDiskSync` flag set to `true`
 - When the write buffer is full
 - Automatically with a configurable period

Note: this synchronization is at the application level, protecting against loss when the application crashes.
If a sudden shutdown of the machine occurs, the content of non-written OS disk cache may still be lost.

</details>

#### Put

<details>
<summary><code>Status Datastore::put(...)</code> - Entry insertion </summary>

```C++
// Key and value as pointer plus size
Status Datastore::put(const void* key, size_t keySize,
                      const void* value, size_t valueSize,
                      const std::vector<KeyIndex>& keyIndexes = {},
                      uint32_t ttlSec = 0,
                      bool forceDiskSync = false);

// Variant 1: key as vector
Status Datastore::put(const std::vector<uint8_t>& key,
                      const void* value, size_t valueSize,
                      const std::vector<KeyIndex>& keyIndexes = {},
                      uint32_t ttlSec = 0,
                      bool forceDiskSync = false);

// Variant 2: key as string. Note that the null termination is not part of the key
Status Datastore::put(const std::string& key,
                      const void* value, size_t valueSize,
                      const std::vector<KeyIndex>& keyIndexes = {},
                      uint32_t ttlSec = 0,
                      bool forceDiskSync = false);

// Variant 3: key as vector and value as vector
Status Datastore::put(const std::vector<uint8_t>& key,
                      const std::vector<uint8_t>& value,
                      const std::vector<KeyIndex>& keyIndexes = {},
                      uint32_t ttlSec = 0,
                      bool forceDiskSync = false);

// Variant 4: key as string and value as vector
Status Datastore::put(const std::string& key,
                      const std::vector<uint8_t>& value,
                      const std::vector<KeyIndex>& keyIndexes = {},
                      uint32_t ttlSec = 0,
                      bool forceDiskSync = false);

// This structure defines a part of the key [start index; size[ to use as an index.
// An array of key indexes MUST be sorted by increasing startIdx, then size if startIdx are equal.
// Except this constraint on sorting, key indexes can overlap.
struct KeyIndex {
    uint8_t startIdx; // Relative to the start of the key
    uint8_t size;
};
```

| Parameter name |   Description                      |
|---------------|-------------------------------------|
| `key`         | The pointer or the structure of the key |
| `keySize`     | In case of key as a pointer, the size of the key in bytes. Maximum accepted size is 65534 bytes |
| `value`       | The pointer or the structure of the value |
| `valueSize`   | In case of value as a pointer, the size of the value in bytes |
| `keyIndexes`  | An array of KeyIndex structures which defines the parts of the key to use as an index. Default is no index |
| `ttlSec`      | The 'Time To Live' of the entry in second. Default is zero which means no lifetime limit |
| `forceDiskSync` | Boolean to force the write on disk of the full write buffer after this entry. Default is false. <br/> Note that it covers just the application cache, not the OS one. |

<br/>

| Return code       |   Comment                         |
|-------------------|-------------------------------------|
| `Status::Ok`   | The entry was successfully stored |
| `Status::BadKeySize`   | The key size is bigger than 65535 |
| `Status::InconsistentKeyIndex` | The quantity of key index is bigger than 64, or the index address bytes outside of the key |
| `Status::UnorderedKeyIndex` | The index are not in order |
| `Status::BadValueSize` | The value size is bigger than 4294901760 bytes |
| `Status::StoreNotOpen` | The datastore is not open |
| `Status::OutOfMemory` | The system is running our of memory |

</details>

#### Remove

<details>
<summary><code>Status Datastore::remove(...)</code> - Entry removal </summary>

```C++
// Key pointer plus size
Status Datastore::remove(const void* key, size_t keySize,
                         bool forceDiskSync = false);

// Variant 1: key as vector
Status Datastore::remove(const std::vector<uint8_t>& key,
                         bool forceDiskSync = false);

// Variant 2: key as string
Status Datastore::remove(const std::string& key,
                         bool forceDiskSync = false);
 ```

| Parameter name    |   Description                         |
|-------------------|-------------------------------------|
| `key`       | The pointer or the structure of the key |
| `keySize`   | In case of key as a pointer, the size of the key in bytes. Maximum accepted size is 65534 bytes |
| `forceDiskSync` | Boolean to force the write on disk of the full write buffer after this entry removal. Default is false. <br/> Note that it covers just the application cache, not the OS one. |

<br/>

| Return code       |   Comment                         |
|-------------------|-------------------------------------|
| `Status::Ok`   | The entry was successfully stored |
| `Status::BadKeySize`   | The key size is bigger than 65535 |
| `Status::StoreNotOpen` | The datastore is not open |
| `Status::EntryNotFound` | The key was not found in the datastore |

</details>

#### Get

<details>
<summary><code>Status Datastore::get(...)</code> - Entry retrieval </summary>

```C++
// Key pointer and size
Status Datastore::get(const void* key, size_t keySize,
                      std::vector<uint8_t>& value);

// Variant 1: key as vector
Status Datastore::get(const std::vector<uint8_t>& key,
                      std::vector<uint8_t>& value);

// Variant 2: key as string
Status Datastore::get(const std::string& key,
                      std::vector<uint8_t>& value);
 ```

| Parameter name    |   Description                         |
|-------------------|-------------------------------------|
| `key`       | The pointer or the structure of the key |
| `keySize`   | In case of key as a pointer, the size of the key in bytes. Maximum accepted size is 65534 bytes |
| `value` | The output array structure for the retrieved value |

<br/>

| Return code             |   Comment                         |
|-------------------|-------------------------------------|
| `Status::Ok`   | The entry was successfully retrieved |
| `Status::BadKeySize`   | The key size is bigger than 65535 |
| `Status::StoreNotOpen` | The datastore is not open |
| `Status::EntryNotFound` | The key was not found in the datastore |
| `Status::EntryCorrupted` | The entry was retrieved from disk and the checksum is incorrect (i.e. corrupted entry) |

</details>

#### Query

<details>
<summary><code>Status Datastore::query(...)</code> - Entries query based on index </summary>

```C++
// Query variant 1: single key part as vector
Status query(const std::vector<uint8_t>& keyPart,
             std::vector<std::vector<uint8_t>>& matchingKeys);

// Query variant 2: single key part as string
Status query(const std::string& keyPart,
             std::vector<std::vector<uint8_t>>& matchingKeys);

// Query variant 3: multiple key part as vector of vector
Status query(const std::vector<std::vector<uint8_t>>& keyParts,
             std::vector<std::vector<uint8_t>>& matchingKeys);

// Query variant 4: key part as vector of string
Status query(const std::vector<std::string>& keyParts,
             std::vector<std::vector<uint8_t>>& matchingKeys);

// Query variant 5: single key part as vector, with arena allocator for output array of keys
Status query(const std::vector<uint8_t>& keyPart,
             std::vector<QueryResult>& arenaMatchingKeys,
             ArenaAllocator& allocator);

// Query variant 6: single key part as string, with arena allocator for output array of keys
Status query(const std::string& keyPart,
             std::vector<QueryResult>& arenaMatchingKeys,
             ArenaAllocator& allocator);

// Query variant 7: multiple key parts as vector, with arena allocator for output array of keys
Status query(const std::vector<std::vector<uint8_t>>& keyParts,
             std::vector<QueryResult>& arenaMatchingKeys,
             ArenaAllocator& allocator);

// Query variant 8: multiple key parts as string, with arena allocator for output array of keys
Status query(const std::vector<std::string>& keyParts,
             std::vector<QueryResult>& arenaMatchingKeys,
             ArenaAllocator& allocator);

// This structure defines a 'query result' by providing a memory span, when using an arena allocator API.
struct QueryResult {
    uint8_t* ptr;
    uint16_t size;
};

// 'minAllocChunkBytes' is the performed allocation size if the requested amount is smaller than this value.
// For efficiency reasons, it should to be several orders of magnitude larger than the typical allocation size
class ArenaAllocator {
  ArenaAllocator(size_t minAllocChunkBytes = 1024 * 1024);
  uint8_t* allocate(size_t bytes);  // Used by the query API
  size_t getAllocatedBytes() const;
  void reset(); // Free all allocations, but keeps the memory chunks internally. Invalidates QueryResult content.
};
 ```

| Parameter name    |   Description                         |
|-------------------|-------------------------------------|
| `keyPart`       | A string or array of bytes to use as a single index to query |
| `keyParts`   | An array of string or array of bytes to use as a multiple index to query. <br/> The resulting keys shall match **all** indexes ('AND' operation) |
| `matchingKeys` | The output array of array of bytes holding the matching keys |
| `allocator` | An arena allocator which improves efficiency of large queries by batching allocations <br/> Used only with the arena allocator APIs and in association with the QueryResult structure |

<br/>

| Return code       |   Comment                         |
|-------------------|-------------------------------------|
| `Status::Ok`   | The entry was successfully retrieved |
| `Status::BadKeySize`   | One of the provided key chunk has a size bigger than 65535 |

</details>

#### Configuration

<details>
<summary><code>Status Datastore::setConfig(...)</code> - Set datastore configuration </summary>

```C++
Status Datastore::setConfig(const Config& config);

struct Config {
    // General store parameters
    // ========================

    //   'dataFileMaxBytes' defines the maximum byte size of a data file before switching to a new one.
    //   It implicitely limits the maximum size of the database as there can be at most 65535 data files.
    //   Bigger data files make the total size bigger (up to 65535*4GB = 281 TiB)
    //   Smaller data files make the merge time shorter
    uint32_t dataFileMaxBytes = 100'000'000;

    //   'mergeCyclePeriodMs' defines the merge period for the database, in milliseconds.
    //   This merge cycle first checks if the 'merge' process is needed. If positive, the eligible data files
    //   are selected and compacted into defragmented and smaller files which eventually replace the old ones.
    uint32_t mergeCyclePeriodMs = 60'000;

    //   'upkeepCyclePeriodMs' defines the upkeep period for the internal structures, in milliseconds.
    //   It copes mainly with the cache eviction and the KeyDir resizing. This latter does not wait the end of
    //   the cycle and start working immediately
    uint32_t upkeepCyclePeriodMs = 1000;

    //   'writeBufferFlushPeriodMs' defines the maximum time for the write buffer to be flushed on disk.
    //   This limits the amount of data that can be lost in case of sudden interruption of the program, while
    //   avoiding costly disk access at each write operation.
    //   Note that the effective period is the maximum between upkeepCyclePeriodMs and writeBufferFlushPeriodMs.
    //   Note also that the "put" API offers to force-flush directly on disk (with a performance cost).
    uint32_t writeBufferFlushPeriodMs = 5000;

    //   'upkeepKeyDirBatchSize' defines the quantity of KeyDir entries to update in a row.
    //   This includes both KeyDir resizing and data file compaction mechanisms.
    //   A higher quantity of entries will make the transition finish earlier, at the price of higher spikes of
    //   latency on entry write or update. A too low value could paradoxically induce a forced resizing of the
    //   remaining part of the KeyDir if the next resize arrives before the end of the previous one.
    uint32_t upkeepKeyDirBatchSize = 100'000;

    //   'upkeepValueCacheBatchSize' defines the quantity of cached value entries to update in a row in the LRU.
    //   A higher quantity of entries will make the background task finish earlier, at the price of higher spikes of
    //   latency on entry write or update. A too low value could paradoxically induce a forced task to clean and
    //   evict cached values at inserting time.
    uint32_t upkeepValueCacheBatchSize = 10000;

    //   'valueCacheTargetMemoryLoadPercentage' configures the target load for the cache, so that the remaining free
    //   space ensures a performant insertion in the cache. The eviction required to meet this target load is deferred
    //   in a background task. Too low a value wastes cache memory, too high a value prevent the insertion a new entry
    //   in the cache because of lack of free space.
    uint32_t valueCacheTargetMemoryLoadPercentage = 90;

    // Merge Triggers
    // ==============
    // They determine the conditions under which merging will be invoked. They fall into two basic categories:

    //   'mergeTriggerDataFileFragmentationPercentage' describes the percentage of dead keys to total keys in
    //   a file that will trigger merging.
    //   Increasing this value will cause merging to occur less often.
    uint32_t mergeTriggerDataFileFragmentationPercentage = 60;

    //   'mergeTriggerDataFileDeadByteThreshold' describes how much data stored for dead keys in a single file triggers
    //   merging. Increasing the value causes merging to occur less often, whereas decreasing the value causes merging
    //   to happen more often.
    uint32_t mergeTriggerDataFileDeadByteThreshold = 51'200'000;

    // Merge data file selection
    // =========================
    // These parameters determine which files will be selected for inclusion in a merge operation.

    //  'mergeSelectDataFileFragmentationPercentage' describes which percentage of dead keys to total keys in a file
    //  causes it to be included in the merge.
    //  Note: this value shall be less than the corresponding trigger threshold.
    uint32_t mergeSelectDataFileFragmentationPercentage = 40;

    //  'mergeSelectDataFileDeadByteThreshold' describes which ratio the minimum amount of data occupied by dead keys
    //  in a file to cause it to be included in the merge.
    //  Note: this value shall be less than the corresponding trigger threshold.
    uint32_t mergeSelectDataFileDeadByteThreshold = 12'800'000;

    //  'mergeSelectDataFileSmallSizeTheshold' describes the minimum size below which a file is included in the merge.
    //  The purpose is to reduce the quantity of small data files to keep open file quantity low.
    uint32_t mergeSelectDataFileSmallSizeTheshold = 10'000'000;
};

 ```

| Parameter name    |   Description             |
|-------------------|-------------------------------------|
| `config`       | The configuration structure |

<br/>

| Return code             |   Comment                      |
|-------------------|-------------------------------------|
| `Status::Ok`      | The entry was successfully retrieved |
| `Status::BadParameterValue`   | A parameter has a bad value. Check the logs for details (warning) |
| `Status::InconsistentParameterValues` | Some parameter values are incompatible. Check the logs for details (warning) |

</details>

<details>
<summary><code>Config Datastore::getConfig(...)</code> - Get datastore configuration </summary>

```C++
Config Datastore::getConfig() const;
 ```

| Return value      |   Comment                           |
|-------------------|-------------------------------------|
| `config`          | The current configuration structure |

</details>

<details>
<summary><code>Status Datastore::setWriteBufferBytes(...)</code> - Write buffer configuration </summary>

```C++
Status Datastore::setWriteBufferBytes(uint32_t writeBufferBytes);
 ```

| Parameter name              |   Description             |
|-------------------|-------------------------------------|
| `writeBufferBytes` | The size of the write buffer in bytes. Default is 100 KB. <br/> Note: above tens of typical entry size, increasing further this value should not have a big impact on performances |

<br/>

| Return code             |   Comment                      |
|-------------------|-------------------------------------|
| `Status::Ok`      | The entry was successfully retrieved |

</details>


<details>
<summary><code>bool Datastore::setLogLevel(...)</code> - Set the logging level </summary>

```C++
bool Datastore::setLogLevel(LogLevel level);

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3, Fatal = 4, None = 5 };
```

| Parameter name              |   Description             |
|-------------------|-------------------------------------|
| `level` | The minimum log level. Logs with a strictly lower level are filtered out. Default is `LogLevel::Info` |

<br/>

| Return code             |   Comment                      |
|-------------------|-------------------------------------|
| `boolean`      | True if the provided log level is within the defined range |

</details>


<details>
<summary><code>void Datastore::setLogHandler(...)</code> - Override the default log handler </summary>

```C++
void Datastore::setLogHandler(const std::function<void(LogLevel, const char*, bool)>& logHandler);
```

| Parameter name              |   Description             |
|-------------------|-------------------------------------|
| `logHandler` | The new log handler function. The provided parameters are:<br/> - the log level (after filtering) <br/> - the message string <br/> - a boolean notifying the termination of the logging process if `true` |

The default logger simply writes in rolling files `litecask<N>.log` at the root of the datastore folder.
<br/>

</details>

#### Compaction control

To remove dead entries, a merge/compaction of the data files is performed automatically in background.
The API in this section is not required for proper function of the datastore.  
It is however possible to force manually such process to target for instance a period where the load is low.  
Such compaction is not systematic nor global, please refer to the configuration structure for further details.  

<details>
<summary><code>bool Datastore::requestMerge(...)</code> - Explicit merge/compaction request </summary>

```C++
bool Datastore::requestMerge();
```

| Return code       |   Comment                      |
|-------------------|--------------------------------|
| `boolean`      |  Returns `true` if the request has been properly processed. <br/> If a merge/compaction process is on-going, the returned value is `false` |

</details>

<details>
<summary><code>bool Datastore::isMergeOnGoing(...)</code> - Get state of the  merge/compaction request </summary>

```C++
bool Datastore::isMergeOnGoing() const;
```

| Return code       |   Comment                      |
|-------------------|--------------------------------|
| `boolean`      |  Returns `true` if a merge/compaction is on-going |

</details>


#### Observability

<details>
<summary><code>uint64_t Datastore::getEstimatedUsedMemoryBytes(...)</code> - Get datastore global used memory </summary>

```C++
uint64_t Datastore::getEstimatedUsedMemoryBytes(bool withCache = false) const;
```

| Parameter name              |   Description             |
|-------------------|-------------------------------------|
| `withCache`       | Boolean to select the inclusion of the value cache used memory. Default is `false` |

<br/>

| Return value       |   Comment                      |
|-------------------|--------------------------------|
| `uint64_t`      |  The estimated byte quantity used by the datastore |

</details>

<details>
<summary><code>uint64_t Datastore::getValueCacheMaxAllocatableBytes(...)</code> - Get configured value cache size </summary>

```C++
uint64_t Datastore::getValueCacheMaxAllocatableBytes() const;
```

| Return value       |   Comment                      |
|-------------------|--------------------------------|
| `uint64_t`      |  Returns the configured value cache size in bytes |

</details>


<details>
<summary><code>uint64_t Datastore::getValueCacheAllocatedBytes(...)</code> - Get used value cache size </summary>

```C++
uint64_t Datastore::getValueCacheAllocatedBytes() const;
```

| Return value       |   Comment                      |
|-------------------|--------------------------------|
| `uint64_t`      |  Returns the currently allocated value cache size in bytes |

</details>

<details>
<summary><code>const DatastoreCounters& Datastore::getCounters(...)</code> - Get datastore counters </summary>

```C++
const DatastoreCounters& Datastore::getCounters() const;

// All fields are monotonic counters
struct DatastoreCounters {
    // API calls
    std::atomic<uint64_t> openCallQty;
    std::atomic<uint64_t> openCallFailedQty;
    std::atomic<uint64_t> closeCallQty;
    std::atomic<uint64_t> closeCallFailedQty;
    std::atomic<uint64_t> putCallQty;
    std::atomic<uint64_t> putCallFailedQty;
    std::atomic<uint64_t> removeCallQty;
    std::atomic<uint64_t> removeCallNotFoundQty;
    std::atomic<uint64_t> removeCallFailedQty;
    std::atomic<uint64_t> getCallQty;
    std::atomic<uint64_t> getCallNotFoundQty;
    std::atomic<uint64_t> getCallCorruptedQty;
    std::atomic<uint64_t> getCallFailedQty;
    std::atomic<uint64_t> getWriteBufferHitQty;
    std::atomic<uint64_t> getCacheHitQty;
    std::atomic<uint64_t> queryCallQty;
    std::atomic<uint64_t> queryCallFailedQty;
    // Data files
    std::atomic<uint64_t> dataFileCreationQty;
    std::atomic<uint64_t> dataFileMaxQty;
    std::atomic<uint64_t> activeDataFileSwitchQty;
    // Index
    std::atomic<uint64_t> indexArrayCleaningQty;
    std::atomic<uint64_t> indexArrayCleanedEntries;
    // Maintenance (merge / compaction)
    std::atomic<uint64_t> mergeCycleQty;
    std::atomic<uint64_t> mergeCycleWithMergeQty;
    std::atomic<uint64_t> mergeGainedDataFileQty;
    std::atomic<uint64_t> mergeGainedBytes;
    std::atomic<uint64_t> hintFileCreatedQty;
};
```

| Return value         |   Comment                      |
|----------------------|--------------------------------|
| `DatastoreCounters`  |  A constant reference on the internal datastore counters structure |

</details>

<details>
<summary><code>const ValueCacheCounters& Datastore::getValueCacheCounters(...)</code> - Get value cache counters </summary>

```C++
const ValueCacheCounters& Datastore::getValueCacheCounters() const;

// All fields are monotonic counters, except 'currentInCacheValueQty'
struct ValueCacheCounters {
    std::atomic<uint64_t> insertCallQty;
    std::atomic<uint64_t> getCallQty;
    std::atomic<uint64_t> removeCallQty;
    std::atomic<uint32_t> currentInCacheValueQty; // Not a monotonic counter
    std::atomic<uint64_t> hitQty;
    std::atomic<uint64_t> missQty;
    std::atomic<uint64_t> evictedQty;
};
```

| Return value         |   Comment                      |
|----------------------|--------------------------------|
| `ValueCacheCounters`  |  A constant reference on the internal value cache counters structure |

</details>


<details>
<summary><code>DataFileStats Datastore::getFileStats(...)</code> - Get data file statistics </summary>

```C++
DataFileStats Datastore::getFileStats() const

// Aggregated statistics over all data files
struct DataFileStats {
    uint64_t fileQty;
    uint64_t entries; // Includes all entries, including tomb and dead
    uint64_t entryBytes;
    uint64_t tombBytes;
    uint64_t tombEntries; // A 'tomb' entry marks an entry deletion
    uint64_t deadBytes;
    uint64_t deadEntries; // An obsolete entry superseded by a newer one
};
```

| Return value         |   Comment                      |
|----------------------|--------------------------------|
| `DataFileStats`      |  A data file statistics structure |

</details>

## Misc

### Support

Supported OS:
 - Linux
 - Windows

Note: in our tests, performance on Windows are lower than on Linux.

### Limits

| Description       |   Limit                |
|-------------------|------------------------|
| Maximum key size | 65534 bytes |
| Maximum entry qty | System memory dependent <br/> Approximate cost per entry is: key size + 60 bytes |
| Maximum value size | 4294901760 (0xFFFF0000) bytes |
| Maximum datastore size | File system dependent <br/> - Data file handles: maximum 65535 data files <br/> - Disk space: each data file can be up to 4 GB, for a total of 256 TB |
| Maximum index quantity per entry | 64 |
| Indexable part of the key | First 256 bytes (storage efficiency reasons) |

Also this project is young, feedback is welcome!

### License

Litecask source code is available under the [MIT license](https://github.com/dfeneyrou/litecask/LICENSE)

Associated components:
 - Hash function: [`Wyhash`](https://github.com/wangyi-fudan/wyhash)
   - Selected for its [good non-cryptographic properties, speed and small code size](https://github.com/rurban/smhasher#summary)
   - Released in the [public domain](http://unlicense.org/)
 - Test framework: [`doctest`](https://github.com/doctest/doctest)
   - Released under the [MIT license](https://github.com/doctest/doctest/blob/master/LICENSE.txt)
