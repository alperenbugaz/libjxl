FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    git \
    cmake \
    g++ \
    make \
    pkg-config \
    libpng-dev \
    libjpeg-dev \
    libgif-dev \
    libbrotli-dev \
    libopenexr-dev \
    libhwy-dev \
    python3 \
    python3-pip \
    ninja-build \
    gdb \
    libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

CMD ["/bin/bash"]