#!/bin/bash
# This file is the main handler of the code
# Delete the cache and the generated build files
find . -name "CMakeCache.txt" -delete && find . -name "CMakeFiles" -type d -exec rm -rf {} +
rm -rf build/

# Remove the old build files
find . -type f \( \
    -name "*.elf" -o \
    -name "*.hex" -o \
    -name "*.uf2" -o \
    -name "*.elf.map" \
\) -delete


# ./compile.sh

# Re-run cmake
cmake .