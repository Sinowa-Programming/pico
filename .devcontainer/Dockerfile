FROM mcr.microsoft.com/devcontainers/base:jammy
LABEL org.opencontainers.image.source = "https://github.com/dodancs/pico-sdk-dev-container"

# Updated to the latest stable version (2.1.1 or 2.2.0)
ENV PICO_SDK_VERSION 2.2.0

RUN apt update \
    && apt install -y --no-install-recommends \
    pkg-config autoconf automake libtool libusb-1.0-0-dev git usbutils gdb-multiarch libhidapi-dev libjim-dev \
    build-essential \
    cmake \
    python3 \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    meson \
    ninja-build \
    nasm \
    && rm -rf /var/lib/apt/lists/* /var/log/apt/* /var/log/dpkg.log /var/log/alternatives.log /var/cache/ \
    coccinelle

# Clone the official Raspberry Pi OpenOCD fork
WORKDIR /opt

RUN git clone https://github.com/raspberrypi/openocd.git

# 3. Build and install OpenOCD with CMSIS-DAP support (required for Atmel-ICE)
WORKDIR /opt/openocd
RUN ./bootstrap && \
    ./configure --enable-cmsis-dap && \
    make -j$(nproc) && \
    make install

WORKDIR /workspace

# Install pico sdk
RUN git clone -b ${PICO_SDK_VERSION} https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk \
    && git -C /opt/pico-sdk submodule update --init \
    && mkdir -p /opt/pico-sdk/build \
    && cd /opt/pico-sdk/build \
    && cmake ..

ENV PICO_SDK_PATH /opt/pico-sdk

# Expose GDB (3333), Telnet (4444), and Tcl (6666) ports
EXPOSE 3333 4444 6666