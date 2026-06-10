#!/usr/bin/env bash
set -euo pipefail

TEST_TIME="${1:-30m}"
SLEEP_TIME="${2:-30m}"

HOSTS=("zip-bookworm" "alpha" "hawk-d12")

for i in "${!HOSTS[@]}"; do
    host="${HOSTS[$i]}"
    cmd="stress-ng --cpu 0 --timeout ${TEST_TIME} --temp-path /tmp"

    echo "[$(date '+%F %T')] running stress-ng on ${host} for ${TEST_TIME}"
    ssh -t "$host" "$cmd"

    if (( i < ${#HOSTS[@]} - 1 )); then
        echo "[$(date '+%F %T')] sleeping ${SLEEP_TIME} before next node"
        sleep "$SLEEP_TIME"
    fi
done

echo "[$(date '+%F %T')] done"