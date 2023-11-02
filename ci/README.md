This folder contains workflow tools both for local and `ci` usage.

 - `format.py` checks and fixes the formatting according to the defined style
   - it covers C++, CMake and Python files
 - `tidy.py` checks some C++ linting according to the defined rules.
 - `benchmark.py` is a frontend to create the throughput graph pictures.
   - it can reuse already generated benchmark results CVS files from the tests or launch the test by itself.

