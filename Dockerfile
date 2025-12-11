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

# Make the entrypoint script executable
RUN chmod +x ./entrypoint.sh

ENTRYPOINT ["./entrypoint.sh"]
