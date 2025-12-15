#!/bin/bash
mkdir -p output
./src/stockfish provision.env --provisioner > output/provision.env.log 2>&1 &
echo "Provisioner started. Check output/provision.env.log for logs."
