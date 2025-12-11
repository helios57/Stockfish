# This Dockerfile creates an image that builds Stockfish at runtime for
# optimal performance on the host machine.

FROM ubuntu:latest

# Install build and runtime dependencies
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       ca-certificates \
       curl \
       bash \
       make \
       g++ \
       pkg-config \
       protobuf-compiler \
       protobuf-compiler-grpc \
       libgrpc++-dev \
       libprotobuf-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the entire repository content. This includes the source code,
# Makefile, and scripts needed for the build.
COPY . .

# Default runtime environment can be overridden
ENV USE_TLS=true \
    SERVER_PORT=443

# Create an entrypoint script that will build and run Stockfish.
# Using a RUN command with a "here document" is a clean way to create the script
# without adding a file to your local project.
RUN <<EOF > ./entrypoint.sh
#!/bin/bash
set -e
echo "Building Stockfish with native optimizations for this system..."
# The 'profile-build' target uses profile-guided optimization for best performance
# and automatically detects the host architecture.
make -j\$(nproc) -C src profile-build
echo "Build complete. Starting Stockfish..."
./src/stockfish
EOF

# Make the entrypoint script executable
RUN chmod +x ./entrypoint.sh

ENTRYPOINT ["./entrypoint.sh"]
