#!/usr/bin/env bash
set -euo pipefail

TEST_TIME="${1:-30m}"
SLEEP_TIME="${2:-30m}"
SCENARIO="${3:-sequential}"   # sequential | parallel

HOSTS=("zip-bookworm" "alpha" "hawk-d12")

# hawk-d12 нагружаем одновременно со slave-узлами.
# Поправь список slave-узлов под свой стенд при необходимости.
TARGET="hawk-d12"
SLAVES=("zip-bookworm" "alpha")

STRESS_CMD="stress-ng --cpu 0 --timeout ${TEST_TIME} --temp-path /tmp"

log() { echo "[$(date '+%F %T')] $*"; }

run_sequential() {
    for i in "${!HOSTS[@]}"; do
        host="${HOSTS[$i]}"
        log "running stress-ng on ${host} for ${TEST_TIME}"
        ssh -t "$host" "$STRESS_CMD"

        if (( i < ${#HOSTS[@]} - 1 )); then
            log "sleeping ${SLEEP_TIME} before next node"
            sleep "$SLEEP_TIME"
        fi
    done
}

run_parallel() {
    local nodes=("$TARGET" "${SLAVES[@]}")
    local pids=() host rc=0

    log "running stress-ng simultaneously on: ${nodes[*]} for ${TEST_TIME}"
    for host in "${nodes[@]}"; do
        (
            ssh "$host" "$STRESS_CMD" 2>&1 | sed "s/^/[${host}] /"
            exit "${PIPESTATUS[0]}"
        ) &
        pids+=("$!")
    done

    for pid in "${pids[@]}"; do
        wait "$pid" || rc=1
    done

    if (( rc == 0 )); then
        log "parallel run finished OK"
    else
        log "parallel run: one or more nodes failed"
    fi
    return "$rc"
}

case "$SCENARIO" in
    sequential) run_sequential ;;
    parallel)   run_parallel ;;
    *) echo "unknown scenario: $SCENARIO (use sequential|parallel)" >&2; exit 2 ;;
esac

log "done"