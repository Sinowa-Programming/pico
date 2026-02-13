#!/bin/bash

# Configuration
REPO_URL="https://chromium.googlesource.com/webm/libvpx"
SOURCE_DIR="libvpx"
BUILD_DIR="vp9_vmm_transformed"
COCCI_FILE="memory_wrapper.cocci"

echo "=== Step 1: Cloning VP9 (libvpx) Source ==="
if [ ! -d "$SOURCE_DIR" ]; then
    git clone "$REPO_URL"
else
    echo "Source already exists, skipping clone."
fi

echo "=== Step 2: Configuring for Bare-Metal (No Assembly) ==="
cd "$SOURCE_DIR"
# --disable-optimizations ensures only C code is used, allowing Coccinelle to see all memory accesses
# --disable-multithread because we are wrapping this in a single FreeRTOS task
./configure --disable-vp8 --enable-vp9 --disable-examples --disable-unit-tests \
            --disable-docs --disable-optimizations --disable-multithread --target=generic-gnu
cd ..

echo "=== Step 3: Creating Transformed Workspace ==="
rm -rf "$BUILD_DIR"
cp -r "$SOURCE_DIR" "$BUILD_DIR"

echo "=== Step 4: Applying Coccinelle Transformations ==="
# We target the vp9 folder specifically to save time
find "$BUILD_DIR/vp9" -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) | while read file; do
    echo "Transforming: $file"
    # 1. Replace malloc with static .vheap arrays
    # 2. Wrap all pointer accesses with VPtr
    spatch --sp-file "$COCCI_FILE" "$file" --in-place --c++-mode --recursive-includes
done

# echo "=== Step 5: Converting Main to FreeRTOS Task1 ==="
# # Search for files containing main (usually in examples or tools)
# # MAIN_FILE=$(grep -r "int main" "$BUILD_DIR" | cut -d: -f1 | head -n 1)
# MAIN_FILE=$BUILD_DIR/examples/simple_decoder.c

# if [ -n "$MAIN_FILE" ]; then
#     echo "Refactoring $MAIN_FILE into task1.cpp..."
#     cp "$MAIN_FILE" "$BUILD_DIR/task1.cpp"

#     # Use sed to transform the function signature and remove exit codes
#     sed -i 's/int main\s*(.*)/void task1(void* pvParameters)/g' "$BUILD_DIR/task1.cpp"
#     sed -i 's/return 0;//g' "$BUILD_DIR/task1.cpp"
#     sed -i 's/return EXIT_SUCCESS;//g' "$BUILD_DIR/task1.cpp"

#     # Add a safety loop at the end if one doesn't exist
#     # (FreeRTOS tasks must never return)
#     echo -e "\n\n// Safety loop added by transformation script\nwhile(1){ vTaskDelay(pdMS_TO_TICKS(1000)); }" >> "$BUILD_DIR/task1.cpp"
# else
#     echo "Warning: No main() function found to convert."
# fi

# # Configuration for your existing hardware entry point
HARDWARE_MAIN="program/main/main.cpp"

echo "=== Step 6: Linking Task1 to Hardware Main ==="

# 1. Add the extern declaration so main knows task1 exists
if ! grep -q "extern void task1" "$HARDWARE_MAIN"; then
    # Insert at the top of the file without the extra quotes
    sed -i '1i extern void task1(void* pvParameters);' "$HARDWARE_MAIN"
fi

# 2. Add the Task Creation call before the scheduler starts
# This looks for the vTaskStartScheduler() line and inserts the task creation before it
if ! grep -q "xTaskCreate(task1" "$HARDWARE_MAIN"; then
    sed -i '/vTaskStartScheduler/i \    xTaskCreate(task1, "VP9_Task", 8192, NULL, tskIDLE_PRIORITY + 1, NULL);' "$HARDWARE_MAIN"
    echo "Injected xTaskCreate into $HARDWARE_MAIN"
fi

echo "=== Done! Transformed source is in $BUILD_DIR ==="