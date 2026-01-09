FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install all dependencies
RUN apt-get update && apt-get install -y \
  build-essential \
  cmake \
  libfftw3-dev \
  libmbedtls-dev \
  libpcsclite-dev \
  libboost-program-options-dev \
  libboost-system-dev \
  libconfig++-dev \
  libsctp-dev \
  libuhd-dev \
  libzmq3-dev \
  uhd-host \
  && rm -rf /var/lib/apt/lists/*

# Download UHD images
RUN uhd_images_downloader

WORKDIR /app

# Copy source code
COPY . .

# Build
RUN mkdir build && \
  cd build && \
  cmake -DRF_FOUND=True .. && \
  make -j$(nproc)

# Copy config
COPY srsue/ue.conf /app/ue.conf

# Set UHD images directory (default location after download)
ENV UHD_IMAGES_DIR=/usr/share/uhd/images

# Run
WORKDIR /app/build/srsue/src
CMD ["./srsue", "/app/ue.conf"]
