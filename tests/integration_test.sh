#!/bin/bash

# Configuration
API_KEY="58a0c48bda56bf1e40e71c50605f7ee0f1fb543be652ebdeaacf4336fe59b8d1"
SERVER="grpc.adjudicator.ch"

# Build the agent
echo "Building agent..."
make -C src build ARCH=x86-64-avx2 > /dev/null 2>&1

echo "Starting Agent 1 (BananeBot3_X)..."
export API_KEY="$API_KEY"
export AGENT_NAME="BananeBot3_X"
export SERVER="$SERVER"
export TIME_CONTROL="60+1"
export GAME_MODE="standard"
unset WAIT_FOR_CHALLENGE
unset SPECIFIC_OPPONENT_AGENT_ID

./src/stockfish > agent_1.log 2>&1 &
PID_1=$!

echo "Starting Agent 2 (BananeBot3_Y)..."
export AGENT_NAME="BananeBot3_Y"
./src/stockfish > agent_2.log 2>&1 &
PID_2=$!

echo "Agents running with PIDs: $PID_1, $PID_2"
echo "Monitoring logs for 120 seconds..."

# Monitor logs
FOUND_PONDER=0
GAME_STARTED=0

start_time=$(date +%s)
while true; do
    current_time=$(date +%s)
    elapsed=$((current_time - start_time))

    if [ $elapsed -gt 120 ]; then
        echo "Timeout reached (120s)."
        break
    fi

    if grep -q "Game started" agent_1.log; then
        if [ $GAME_STARTED -eq 0 ]; then
            echo "SUCCESS: Game Started detected for Agent 1!"
            GAME_STARTED=1
        fi
    fi
    if grep -q "Game started" agent_2.log; then
        if [ $GAME_STARTED -eq 0 ]; then
             echo "SUCCESS: Game Started detected for Agent 2!"
             GAME_STARTED=1
        fi
    fi

    if grep -q "Starting ponder on:" agent_1.log; then
        echo "SUCCESS: Agent 1 started pondering!"
        FOUND_PONDER=1
    fi
    if grep -q "Starting ponder on:" agent_2.log; then
        echo "SUCCESS: Agent 2 started pondering!"
        FOUND_PONDER=1
    fi

    if [ $FOUND_PONDER -eq 1 ]; then
        break
    fi

    sleep 2
done

if [ $FOUND_PONDER -eq 1 ]; then
    echo "Test PASSED: Pondering detected."
    EXIT_CODE=0
else
    echo "Test FAILED: Pondering not detected."
    EXIT_CODE=1
fi

echo "=== Agent 1 Logs (Tail) ==="
tail -n 20 agent_1.log
echo "=== Agent 2 Logs (Tail) ==="
tail -n 20 agent_2.log

# Cleanup
kill $PID_1 $PID_2 2>/dev/null
wait $PID_1 $PID_2 2>/dev/null
rm -f agent_1.log agent_2.log

exit $EXIT_CODE
