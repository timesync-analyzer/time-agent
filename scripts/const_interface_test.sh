#!/usr/bin/env bash
set -euo pipefail

SSH_USER="${SSH_USER:-zloyaloha}"
DURATION="${DURATION:-600}"
SLEEP_BETWEEN="${SLEEP_BETWEEN:-60}"
IPERF_PORT="${IPERF_PORT:-5201}"
IPERF_CLIENT_ARGS="${IPERF_CLIENT_ARGS:--u -b 750M --pacing-timer 10 -P 4}"

# Pairs of "client server" — iperf3 -s is started on server, -c runs on client.
PAIRS=(
    "zip-bookworm hawk-d12"
    "hawk-d12 zip-bookworm"
    "alpha hawk-d12"
    "hawk-d12 alpha"
    "zip-bookworm alpha"
    "alpha zip-bookworm"
)

usage() {
    cat <<EOF
Usage: $0 [-d duration_sec] [-s sleep_sec] [-u ssh_user] [-p iperf_port]

Environment overrides: SSH_USER, DURATION, SLEEP_BETWEEN, IPERF_PORT, IPERF_CLIENT_ARGS.
EOF
}

while getopts ":d:s:u:p:h" opt; do
    case "${opt}" in
        d) DURATION="${OPTARG}" ;;
        s) SLEEP_BETWEEN="${OPTARG}" ;;
        u) SSH_USER="${OPTARG}" ;;
        p) IPERF_PORT="${OPTARG}" ;;
        h) usage; exit 0 ;;
        *) usage; exit 1 ;;
    esac
done

ssh_run() {
    local host="$1"; shift
    ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
        "${SSH_USER}@${host}" "$@"
}

SERVER_SSH_PID=""
SERVER_HOST=""

start_server() {
    local host="$1"
    echo "----- [$(date '+%F %T')] starting iperf3 server on ${host} -----"

    # Make sure no stale server is holding the port.
    ssh_run "${host}" \
        "pkill -f 'iperf3 -s -p ${IPERF_PORT}' >/dev/null 2>&1 || true" || true

    # Hold the iperf3 server in the foreground of a local background ssh.
    # stdout/stderr stream live to the controlling terminal so failures
    # (port busy, missing binary, ...) are visible immediately.
    ssh -tt -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
        -o ExitOnForwardFailure=yes \
        "${SSH_USER}@${host}" \
        "iperf3 -s -p ${IPERF_PORT}" \
        2>&1 | sed "s/^/[${host}:iperf3-s] /" &
    SERVER_SSH_PID=$!
    SERVER_HOST="${host}"

    # Wait for the listener to come up.
    local tries=0
    until ssh_run "${host}" \
              "ss -lnt 'sport = :${IPERF_PORT}' | grep -q LISTEN" 2>/dev/null; do
        tries=$((tries + 1))
        if (( tries > 10 )); then
            echo "!! iperf3 server did not start listening on ${host}:${IPERF_PORT}" >&2
            stop_server
            return 1
        fi
        sleep 1
    done
    echo "----- iperf3 server up on ${host}:${IPERF_PORT} -----"
}

stop_server() {
    if [[ -n "${SERVER_HOST}" ]]; then
        ssh_run "${SERVER_HOST}" \
            "pkill -f 'iperf3 -s -p ${IPERF_PORT}' >/dev/null 2>&1 || true" || true
    fi
    if [[ -n "${SERVER_SSH_PID}" ]] && kill -0 "${SERVER_SSH_PID}" 2>/dev/null; then
        kill "${SERVER_SSH_PID}" 2>/dev/null || true
        wait "${SERVER_SSH_PID}" 2>/dev/null || true
    fi
    SERVER_SSH_PID=""
    SERVER_HOST=""
}

trap 'stop_server' EXIT INT TERM

resolve_ip() {
    local host="$1"
    ssh_run "${host}" \
        "hostname -I | awk '{print \$1}'"
}

run_pair() {
    local client="$1" server="$2"
    echo "===== [$(date '+%F %T')] experiment ${client} -> ${server} ====="

    start_server "${server}"
    local server_ip
    server_ip="$(resolve_ip "${server}")"
    if [[ -z "${server_ip}" ]]; then
        echo "!! failed to resolve ip of ${server}" >&2
        stop_server "${server}"
        return 1
    fi

    echo "----- [$(date '+%F %T')] starting iperf3 client on ${client} (-> ${server_ip}:${IPERF_PORT}) -----"
    if ! ssh_run "${client}" \
            "iperf3 -c ${server_ip} -p ${IPERF_PORT} -t ${DURATION} ${IPERF_CLIENT_ARGS}"; then
        echo "!! iperf3 client on ${client} failed" >&2
    fi

    stop_server "${server}"
    echo "===== [$(date '+%F %T')] finished ${client} -> ${server} ====="
}

for i in "${!PAIRS[@]}"; do
    read -r client server <<<"${PAIRS[$i]}"
    # Don't let one bad pair abort the whole sweep.
    run_pair "${client}" "${server}" || echo "!! pair ${client} -> ${server} failed, continuing" >&2
    if [[ $i -lt $((${#PAIRS[@]} - 1)) ]]; then
        echo "----- sleeping ${SLEEP_BETWEEN}s before next pair -----"
        sleep "${SLEEP_BETWEEN}"
    fi
done

echo "===== all done ====="