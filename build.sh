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
    -name "*.dis" -o \
    -name "*.elf.map" \
\) -delete


# ./compile.sh

# Re-run cmake
cmake -DCMAKE_BUILD_TYPE=Debug .

echo 'To debug, run: openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000"'
echo 'To decompile, run: arm-none-eabi-objdump -D -bbinary -marm client_payload.bin -Mforce-thumb > client_payload.txt'