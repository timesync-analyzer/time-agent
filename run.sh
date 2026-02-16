#!/bin/bash

if ! command -v yq &> /dev/null; then
  echo "Error: yq is not installed. Need to parse yaml. Or other way to run agent (app)"
  exit 1
fi

CONFIG=${1:-config.yaml}

get() {
  yq "$1" "$CONFIG"
}

REGIME=$(get '.global.sync_regime' | tr -d '"' | xargs)
NET_INTERFACE=$(get '.network.interface_name' | tr -d '"' | xargs)
NEED_AGENT=$(get '.global.need_agent')

if [[ "$REGIME" != "master" && "$REGIME" != "slave" ]]; then
    echo "Unknown agent app regime $REGIME. Exit"
    exit 1
fi

echo "Run agent in $REGIME regime"

AGENT_PID=""
if [[ "$NEED_AGENT" == true ]]; then
    ./build/time_agent "$CONFIG" &
    AGENT_PID=$!
    echo "time_agent started (PID $AGENT_PID)"
else
    echo "time_agent disabled by config"
fi

if [[ "$REGIME" == "master" ]]; then
    ptp4l -i "$NET_INTERFACE" -l 6 &
    PTP4L_PID=$!
    phc2sys -s CLOCK_REALTIME -c "$NET_INTERFACE" -w &
    PHC2SYS_PID=$!
elif [[ "$REGIME" == "slave" ]]; then
    ptp4l -i "$NET_INTERFACE" -s -l 6 &
    PTP4L_PID=$!
    phc2sys -s "$NET_INTERFACE" -c CLOCK_REALTIME -w &
    PHC2SYS_PID=$!
fi

cleanup() {
    echo "Stopping all processes..."
    [[ -n "$AGENT_PID" ]] && kill "$AGENT_PID" 2>/dev/null || true
    kill "$PTP4L_PID" "$PHC2SYS_PID" 2>/dev/null || true
    [[ -n "$AGENT_PID" ]] && wait "$AGENT_PID" 2>/dev/null || true
    wait "$PTP4L_PID" "$PHC2SYS_PID" 2>/dev/null || true
    echo "All processes stopped."
    exit 0
}

trap cleanup SIGINT SIGTERM

echo "Agent running. Press Ctrl+C to stop."
while true; do
    sleep 1
done
