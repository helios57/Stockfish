mkdir -p output
for config in agent*.env; do
  ./src/stockfish "$config" > "output/$config.log" 2>&1 &
done
