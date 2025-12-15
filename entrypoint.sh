#!/bin/bash
set -e

echo "Downloading neural network files..."
(cd src && ../scripts/net.sh)

echo "Building Stockfish with native optimizations for this system..."
# The 'profile-build' target uses profile-guided optimization for best performance
# and automatically detects the host architecture.
(cd src && make -j$(nproc) profile-build)


echo "Build complete. Starting Stockfish..."
./src/stockfish "$@"
