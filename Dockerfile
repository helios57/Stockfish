# Multi-stage build: compile Stockfish and run Python gRPC agent

FROM debian:bookworm-slim AS builder
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

WORKDIR /build
COPY src/ ./src/
COPY scripts/ ./scripts/
COPY chess_contest.proto ./

# Build the gRPC agent
RUN make -C src build ARCH=x86-64 \
    && strip src/stockfish

FROM debian:bookworm-slim AS runtime
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       ca-certificates \
       libgrpc++-dev \
       libprotobuf-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /build/src/stockfish /app/stockfish
COPY --from=builder /build/src/*.nnue /app/

# Default runtime environment can be overridden
ENV USE_TLS=true \
    SERVER_PORT=443

ENTRYPOINT ["/app/stockfish"]
