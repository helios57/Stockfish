#!/bin/bash
set -e

echo "Downloading neural network files..."
(cd src && ../scripts/net.sh)

echo "Building Stockfish with native optimizations for this system..."
# The 'build' target compiles with optimizations but skips the PGO benchmark.
make -j$(nproc) -C src build

echo "Build complete. Starting Stockfish..."
./src/stockfish
