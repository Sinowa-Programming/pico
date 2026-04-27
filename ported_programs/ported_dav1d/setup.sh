# git clone https://github.com/videolan/dav1d.git # Currently on commit 60507bffc0b13e7a81753a51005dbbeba4b23018 when origionally cloned.

cd dav1d
rm -rf build
meson setup build \
    --cross-file ../rp2350.cross \
    -Denable_asm=false \
    -Denable_tools=false \
    -Denable_tests=false \
    -Ddefault_library=static \
    -Dbitdepths=8 \
    -Dbuildtype=release

ninja -C build