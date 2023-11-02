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

#include <cstdio>

#include "litecask.h"

int
main(int argc, char** argv)
{
    using namespace litecask;
    LogLevel              logLevel        = LogLevel::Warn;
    bool                  doDisplaySyntax = false;
    lcString              command;
    std::filesystem::path dbDirectoryPath;
    litecask::Config      config;
    Status                status = Status::Ok;

    // Parse the command line
    // ======================
    int paramIdx = 0;
    int i        = 1;
    while (i < argc && !doDisplaySyntax) {
        lcString arg = argv[i++];
        if (arg == "-v") {
            logLevel = LogLevel::Info;
        } else if (arg == "-vv") {
            logLevel = LogLevel::Debug;
        } else if (arg.size() >= 3 && arg.substr(0, 3) == "-s=") {
            uint32_t dataFileMaxBytes = (uint32_t)strtoll(arg.substr(3).c_str(), nullptr, 0);
            if (dataFileMaxBytes > 0) {
                config.dataFileMaxBytes = dataFileMaxBytes;
                printf("Setting dataFileMaxBytes to %u\n", dataFileMaxBytes);
            } else {
                printf("Error: wrong value for dataFileMaxBytes (%s)\n", arg.substr(3).c_str());
                doDisplaySyntax = true;
            }
        } else if (!arg.empty() && arg.data()[0] == '-') {
            printf("Error: unknown option '%s'\n", arg.c_str());
            doDisplaySyntax = true;
        } else if (paramIdx == 0) {
            command = arg;
            ++paramIdx;
        } else if (paramIdx == 1) {
            dbDirectoryPath = arg;
            ++paramIdx;
        } else {
            printf("Error: too much parameters.\n");
            doDisplaySyntax = true;
        }
    }
    if (paramIdx != 2) { doDisplaySyntax = true; }
    if (doDisplaySyntax) {
        printf("Litecask utility to dump statistics or fully merge a datastore\n\n");
        printf("Syntax: %s (stat | file | merge) <db path> [ options ]\n\n", argv[0]);
        printf("  Options:\n");
        printf("   -v    verbose (in datastore log file)\n");
        printf("   -vv   more verbose logs\n");
        printf("   -s=<dataFileMaxBytes>   Used by the merge command. Default is %u\n", config.dataFileMaxBytes);
        printf("\n");
        printf("  Commands:\n");
        printf("   'stats' provides a summary of the database figures (size, items, ...)\n");
        printf("   'file'  dumps the high level statistics of each data file\n");
        printf("   'merge' performs an offline full merge of the datastore.\n");

        exit(1);
    }

    if (command != "stat" && command != "file" && command != "merge") {
        printf("Error: the first parameter is the command name, to select among (stat | file | merge)\n");
        exit(1);
    }

    // Prepare the configuration for full merge
    config.mergeTriggerDataFileFragmentationPercentage = 1;
    config.mergeTriggerDataFileDeadByteThreshold       = 0;
    config.mergeSelectDataFileFragmentationPercentage  = 1;
    config.mergeSelectDataFileDeadByteThreshold        = 0;

    // Open the database
    Datastore store;
    store.setLogLevel(logLevel);
    status = store.open(dbDirectoryPath, false);
    if (status != Status::Ok) {
        printf("Unable to open the datastore %s: %s\n", dbDirectoryPath.string().c_str(), Datastore::toString(status));
        exit(1);
    }

    // Set the configuration
    status = store.setConfig(config);
    if (status != Status::Ok) {
        printf("Unable to set the configuration: %s\n", Datastore::toString(status));
        exit(1);
    }

    // Apply commands
    // ==============

    if (command == "stat") {
        DataFileStats s = store.getFileStats();
        printf("Data files         : %" PRId64 "\n", s.fileQty);
        printf("Valid entries      : %-7" PRId64 " in %7.1f MB\n", s.entries - s.tombEntries - s.deadEntries,
               1e-6 * (double)(s.entryBytes - s.tombBytes - s.deadBytes));
        printf("Dead & tomb entries: %-7" PRId64 " in %7.1f MB\n", s.tombEntries + s.deadEntries,
               1e-6 * (double)(s.tombBytes + s.deadBytes));
        printf("Compactness        : %" PRId64 " %%\n",
               100 * (s.entryBytes - s.tombBytes - s.deadBytes) / std::max((uint64_t)1, s.entryBytes));
    }

    if (command == "file") {
        printf("Database content:\n");
        store.dumpFd();
        printf("\n");
    }

    if (command == "merge") {
        if (!store.requestMerge() && !store.isMergeOnGoing()) {
            printf("Error: unable to start the merge\n");
            exit(1);
        }
        printf("Start merging |");
        fflush(stdout);
        int round = 0;
        while (store.isMergeOnGoing()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if ((++round) % 10 == 0) {
                printf("\rStart merging %c", "|/-\\"[(round / 10) % 4]);
                fflush(stdout);
            }
        }
        printf("\rMerge finished  \n");
    }

    store.close();
    return 0;
}
