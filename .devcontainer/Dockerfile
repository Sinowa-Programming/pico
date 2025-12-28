FROM mcr.microsoft.com/devcontainers/base:jammy
LABEL org.opencontainers.image.source = "https://github.com/dodancs/pico-sdk-dev-container"

# Updated to the latest stable version (2.1.1 or 2.2.0)
ENV PICO_SDK_VERSION 2.2.0

RUN apt update \
    && apt install -y --no-install-recommends \
    build-essential \
    cmake \
    python3 \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    && rm -rf /var/lib/apt/lists/* /var/log/apt/* /var/log/dpkg.log /var/log/alternatives.log /var/cache/ \
    coccinelle

RUN git clone -b ${PICO_SDK_VERSION} https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk \
    && git -C /opt/pico-sdk submodule update --init \
    && mkdir -p /opt/pico-sdk/build \
    && cd /opt/pico-sdk/build \
    && cmake ..

ENV PICO_SDK_PATH /opt/pico-sdk