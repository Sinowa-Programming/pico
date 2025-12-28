#!/bin/bash
# This file is the main handler of the code
# Delete the cache and the generated build files
rm -rf CMakeCache.txt CMakeFiles/ src/CMakeFiles/

# Re-run cmake
cmake .