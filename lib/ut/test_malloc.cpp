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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "test_main.h"

using namespace litecask;
using namespace litecask::detail;

// Requires access to allocator internal (provided by the compilation flag LITECASK_BUILD_FOR_TEST)
void
checkConsistency(TlsfAllocator* t)
{
#define TLSF_CHECK(cond, msg)                                                  \
    if (!(cond)) {                                                             \
        fprintf(stderr, "Failed Tlsf allocator check: %s - %s\n", msg, #cond); \
        abort();                                                               \
    }

    // Loop on first layer
    for (uint32_t flIdx = 0; flIdx < TlsfFlQty; ++flIdx) {
        // Loop on second layer
        for (uint32_t slIdx = 0; slIdx < TlsfSlQty; ++slIdx) {
            uint64_t   flBit    = t->_flBitmap & (1U << flIdx);
            uint64_t   slBitmap = t->_slBitmaps[flIdx];
            uint64_t   slBit    = slBitmap & ((uint64_t)1 << slIdx);
            tlsfBlock* block    = t->_freeBlocks[flIdx][slIdx];

            // Check consistency between 1st and 2nd level bitmaps
            if (!flBit) TLSF_CHECK(!slBit, "The SL bitmap is not null, which is not consistent with the FL bitmap");
            if (!slBit) {
                TLSF_CHECK(!block, "The free block list is not null, which is not consistent with the SL bitmap");
                continue;
            }

            // Case the free list is not empty: check that the chained free blocks are valid
            while (block) {
                // Check chaining and field values
                TLSF_CHECK(block->isFree(), "The block should be free");
                TLSF_CHECK(!block->isPrevFree(), "The previous block should have been merged with current free one");
                TLSF_CHECK(!block->getNext()->isFree(), "The next block should have been merged with the current free one");
                TLSF_CHECK(block->getNext()->isPrevFree(), "The next block should indicate that its previous block is free");
                TLSF_CHECK(block->getPayloadSize() >= sizeof(tlsfBlock) - sizeof(tlsfBlock*), "The free block is too small");

                // Check that they are in the right list, as a function of their size
                uint32_t firstLayerIdx = 0, secondLayerIdx = 0;
                t->findSizeFittingList(block->getPayloadSize(), &firstLayerIdx, &secondLayerIdx);
                TLSF_CHECK(firstLayerIdx == flIdx && secondLayerIdx == slIdx, "The block is not in the correct free list");

                // Next free block of the list
                block = block->nextFreeBlock;
            }
        }
    }
}

TEST_SUITE("Memory allocator")
{
    TEST_CASE("1-Sanity   : Large allocations")
    {
        constexpr int memorySizeBytes = 1'000'000;
        TlsfAllocator tlsf(memorySizeBytes);

        for (int testSize = 64; testSize <= memorySizeBytes / 2; testSize *= 2) {
            for (int s = std::max(1, testSize - 100); s < testSize; ++s) {
                void* p = tlsf.malloc(s);
                CHECK(p);

                void* q = tlsf.malloc(s);
                CHECK(q);
                tlsf.free(q);

                q = tlsf.malloc(s);
                CHECK(q);
                tlsf.free(q);

                tlsf.free(p);
                checkConsistency(&tlsf);
            }
        }
    }

    TEST_CASE("1-Sanity   : Random allocations")
    {
        TlsfAllocator tlsf(100 * 1024 * 1024);

        std::vector<int> sizes = {16, 32, 64};
        if (testGetDuration() == TestDuration::Long) {
            sizes = {16, 32, 64, 128, 256, 512, 1024};
        } else if (testGetDuration() == TestDuration::Longest) {
            sizes = {16, 32, 64, 128, 256, 512, 1024, 1024 * 1024};
        }

        for (uint32_t i = 0; i < sizes.size(); i++) {
            int testSize = sizes[i];

            for (int iteration = 0; iteration < 512; ++iteration) {
                std::vector<void*> allocatedPointers;
                allocatedPointers.reserve(1024);

                const int maxAllocBytes   = 1 + (int)(testGetRandom() % testSize);
                int       bytesToAllocate = (int)(testSize * (1 + (testGetRandom() % 10)));
                while (bytesToAllocate > 0) {
                    // Allocate
                    int   len = (int)(1 + (testGetRandom() % maxAllocBytes));
                    void* ptr = tlsf.malloc(len);
                    CHECK(ptr);
                    bytesToAllocate -= len;
                    allocatedPointers.push_back(ptr);

                    // Sometimes reallocate
                    if (testGetRandom() % 10 == 0) {
                        tlsf.free(allocatedPointers.back());
                        allocatedPointers.pop_back();
                        len = (int)(1 + (testGetRandom() % maxAllocBytes));
                        ptr = tlsf.malloc(len);
                        CHECK(ptr);
                        allocatedPointers.push_back(ptr);
                    }

                    checkConsistency(&tlsf);

                    // Fill the content for small test sizes (duration...)
                    if (testSize <= 1000000) { memset(ptr, 0xad, len); }

                    // Add a marking
                    ((uint8_t*)ptr)[0] = 0xde;

                    if ((int)allocatedPointers.size() == 2 * testSize) { break; }
                }

                // Deallocate in random order
                while (!allocatedPointers.empty()) {
                    int      j   = (int)(testGetRandom() % allocatedPointers.size());
                    uint8_t* ptr = (uint8_t*)allocatedPointers[j];
                    CHECK(ptr[0] == 0xde);
                    tlsf.free(ptr);
                    allocatedPointers[j] = allocatedPointers.back();
                    allocatedPointers.pop_back();
                }

                checkConsistency(&tlsf);

            }  // End of iterations

        }  // End of loop on test sizes
    }

    TEST_CASE("1-Sanity   : Memory filling and overhead")
    {
        constexpr int cacheBytes = 1 * 1024 * 1024;
        TlsfAllocator tlsf(cacheBytes);

        std::vector<int> sizes = {15, 16, 17, 18, 19, 20, 21, 22, 27, 28, 30, 31, 32};
        if (testGetDuration() != TestDuration::Short) {
            sizes = {1,  7,  8,  15, 16, 17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,
                     29, 30, 31, 32, 64, 128, 129, 130, 131, 132, 160, 255, 256, 257, 512, 1024};
        }

        std::vector<void*> allocations;
        allocations.reserve(cacheBytes / 8);
        void* ptr = nullptr;

        for (uint32_t i = 0; i < sizes.size(); i++) {
            size_t usedSize = sizes[i];
            allocations.clear();

            // Fill the cache with elements of this size
            while ((ptr = tlsf.malloc(usedSize)) != nullptr) { allocations.push_back(ptr); }
            size_t qty = allocations.size();

            // Free them
            for (void* ptr2 : allocations) tlsf.free(ptr2);

            // Reallocate
            allocations.clear();
            while ((ptr = tlsf.malloc(usedSize)) != nullptr) { allocations.push_back(ptr); }
            size_t qty2 = allocations.size();

            // Free them
            for (void* ptr2 : allocations) tlsf.free(ptr2);

            // Ensure reproductibility
            CHECK_EQ(qty, qty2);

            // Compute per-allocation overhead
            double overheadByte =
                (double)usedSize * ((double)cacheBytes / (double)usedSize - (double)qty) / (double)std::max((size_t)1, qty);
            double theoreticalOverheadByte = 8.;  // Overhead for block management
            int    align =
                (usedSize > 16) ? 8 : 24;  // Below 16 bytes size, the strongest alignement constraint is the internal block structure
            theoreticalOverheadByte += (align - ((int)usedSize % align)) % align;  // Loss due to N-byte alignment
            theoreticalOverheadByte += 1.;                                         // Rounding margin

#if 0
            printf("block size=%4ld:   Measured overhead is %4.1f   (theoretical limit is %2.0f)\n",
                   usedSize, overheadByte, theoreticalOverheadByte);
#endif
            CHECK(overheadByte < 32.);  // By design, the overhead is lower than 24 (internal block structure) + 8-bytes aligned bloc
            CHECK(overheadByte <= theoreticalOverheadByte);
        }
    }

    TEST_CASE("1-Sanity   : Allocator reset")
    {
        TlsfAllocator tlsf(100 * 1024 * 1024);

        std::vector<int> sizes = {16, 32, 64, 128, 256, 512, 1024, 1024 * 1024};

        for (uint32_t i = 0; i < sizes.size(); i++) {
            int testSize = sizes[i];

            for (int iteration = 0; iteration < 16; ++iteration) {
                std::vector<void*> allocatedPointers;
                allocatedPointers.reserve(1024);

                const int maxAllocBytes   = 1 + (int)(testGetRandom() % testSize);
                int       bytesToAllocate = (int)(testSize * (1 + (testGetRandom() % 10)));
                while (bytesToAllocate > 0) {
                    // Allocate
                    int   len = (int)(1 + (testGetRandom() % maxAllocBytes));
                    void* ptr = tlsf.malloc(len);
                    CHECK(ptr);
                    bytesToAllocate -= len;
                    allocatedPointers.push_back(ptr);

                    if ((int)allocatedPointers.size() == 2 * testSize) { break; }
                }
                checkConsistency(&tlsf);

                // Deallocate half in random order
                size_t halfSize = std::max((size_t)1, allocatedPointers.size() / 2);
                while (allocatedPointers.size() > halfSize) {
                    int j = (int)(testGetRandom() % allocatedPointers.size());
                    tlsf.free((uint8_t*)allocatedPointers[j]);
                    allocatedPointers[j] = allocatedPointers.back();
                    allocatedPointers.pop_back();
                }

                checkConsistency(&tlsf);
                CHECK(tlsf.getAllocatedBytes() > 0);

                tlsf.reset();
                CHECK(tlsf.getAllocatedBytes() == 0);

                checkConsistency(&tlsf);

            }  // End of iterations

        }  // End of loop on test sizes
    }

}  // End of test suite
