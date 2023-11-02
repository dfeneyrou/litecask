This folder contains the tests of the litecask library.

Build and run
=============

On Linux:
```
mkdir build
cd build
cmake ..
make -j $(nproc)

./bin/litecask_test
```

On Windows (with MSVC):
```
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 -S .. -B "build64"
cmake --build build64 --config Release

build64\bin\Release\litecask_test.exe
```

Advanced testing
================

The executable `litecask_test` is configurable.  
By default, short (c.a. 1 second) sanity tests are run.
```
./build/bin/litecask_test --help
=====================================================================================================
This executable tests the litecask library 1.0.0
Parameters have two dimensions:
 - the selection of the kind of tests
 - the selection of the depth of the tests
Default parameters focus on development cycle, i.e. short sanity tests
=====================================================================================================
The available keywords are:
 sanity                               add sanity tests (default if no other keyword)
 benchmark                            add benchmark tests
 stress                               add stress tests
=====================================================================================================
The available option durations are:
 -l                                   longer test duration
 -ll                                  even longer test duration
=====================================================================================================
The available misc options are:
 -?, --help, -h                       prints this message and exit
 -v, --version                        prints the litecask version and exit
 -ss                                  show names of skipped tests
The available option filters are (union with selected kinds of tests):
 -tc,  --test-case=<filters>          filters     tests by their name
 -tce, --test-case-exclude=<filters>  filters OUT tests by their name
 -ts,  --test-suite=<filters>         filters     tests by their test suite
 -tse, --test-suite-exclude=<filters> filters OUT tests by their test suite
=====================================================================================================
```

Longer tests can be performed with:
 - `-l` increases the dimensioning of the tests
 - `-ll` increases even more the dimensioning of the tests. This is the reference for official results.

Due to their duration, benchmarks are excluded by default. They are enabled with the argument `benchmark`.  
Example of test parameters for a release on Linux:
```
./build/bin/litecask_test -ll sanity benchmark && ../ci/benchmark.py -n"
```

Note: the temporary datastores for testing are created at location `/tmp/litecask_test`.
Note: the call to `benchmark.py` generates the reporting with graph images.

Example of testing with ASAN (Linux):
```
cd build-asan
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=1 ..
make -j $(nproc)
./bin/litecask_test sanity benchmark
```

Example of testing with TSAN (Linux):
```
cd build-tsan
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=1 ..
make -j $(nproc)
TSAN_OPTIONS="suppressions=../ci/tsan_suppression.supp" ./bin/litecask_test sanity benchmark
```

Note: some suppressions are needed due to "optimistic locking" mechanism or other harmless datarace which are "under control".
