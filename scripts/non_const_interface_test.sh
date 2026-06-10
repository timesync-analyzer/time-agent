#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# Sequential sweep that runs the original randomized iperf3 load loop over all
# ordered (client -> server) pairs of the PTP nodes.
#
# For each pair it:
#   1. starts an iperf3 server on the server node (over ssh),
#   2. resolves the server's primary IP,
#   3. runs the randomized load loop on the client (over ssh), bounded by
#      EXP_TIME via `timeout`,
#   4. tears the server down and rests REST_TIME before the next pair.
#
# EXP_TIME / REST_TIME accept suffixes (s, m, h) e.g. 30m, 1h, 90s — passed
# straight to `timeout` and `sleep`. They may also be given positionally:
#   ./run_iperf_load_sweep.sh 30m 5m
# ---------------------------------------------------------------------------

SSH_USER="${SSH_USER:-zloyaloha}"
EXP_TIME="${EXP_TIME:-10m}"      # how long to load each pair
REST_TIME="${REST_TIME:-1m}"     # idle gap between pairs
IPERF_PORT="${IPERF_PORT:-5201}"
KILL_GRACE="${KILL_GRACE:-5}"    # seconds for timeout to escalate TERM -> KILL

# Ordered (client server) pairs. The hawk-d12 links run first, then alpha<->zip.
PAIRS=(
    "zip-bookworm hawk-d12"
    "alpha hawk-d12"
    "hawk-d12 zip-bookworm"
    "hawk-d12 alpha"
    "alpha zip-bookworm"
    "zip-bookworm alpha"
)

usage() {
    cat <<EOF
Usage: $0 [-u ssh_user] [-p iperf_port] [exp_time] [rest_time]
       $0 [-e exp_time] [-r rest_time] [-u ssh_user] [-p iperf_port]

  exp_time   experiment time per pair  (positional 1, default ${EXP_TIME})
  rest_time  rest time between pairs    (positional 2, default ${REST_TIME})

  -e  experiment time per pair   (default ${EXP_TIME};  e.g. 30m, 1h, 90s)
  -r  rest time between pairs     (default ${REST_TIME}; same format)
  -u  ssh user                    (default ${SSH_USER})
  -p  iperf3 port                 (default ${IPERF_PORT})

Examples:
  $0 30m 5m
  $0 -e 1h -r 2m -u admin

Environment overrides: SSH_USER, EXP_TIME, REST_TIME, IPERF_PORT, KILL_GRACE.
EOF
}

while getopts ":e:r:u:p:h" opt; do
    case "${opt}" in
        e) EXP_TIME="${OPTARG}" ;;
        r) REST_TIME="${OPTARG}" ;;
        u) SSH_USER="${OPTARG}" ;;
        p) IPERF_PORT="${OPTARG}" ;;
        h) usage; exit 0 ;;
        *) usage; exit 1 ;;
    esac
done
shift $((OPTIND - 1))

# Positional args (if given) override: $1 = exp_time, $2 = rest_time.
[[ $# -ge 1 ]] && EXP_TIME="$1"
[[ $# -ge 2 ]] && REST_TIME="$2"

ssh_run() {
    local host="$1"; shift
    ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
        "${SSH_USER}@${host}" "$@"
}

SERVER_SSH_PID=""
SERVER_HOST=""
CLIENT_HOST=""

start_server() {
    local host="$1"
    echo "----- [$(date '+%F %T')] starting iperf3 server on ${host} -----"

    # Drop any stale server holding the port.
    ssh_run "${host}" \
        "pkill -f 'iperf3 -s -p ${IPERF_PORT}' >/dev/null 2>&1 || true" || true

    # Keep the iperf3 server in the foreground of a backgrounded ssh so its
    # output streams live and failures are visible immediately.
    ssh -tt -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
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

stop_client_load() {
    # Make sure no client-side iperf3 from this pair lingers after timeout.
    if [[ -n "${CLIENT_HOST}" ]]; then
        ssh_run "${CLIENT_HOST}" \
            "pkill -f 'iperf3 -c' >/dev/null 2>&1 || true" || true
    fi
    CLIENT_HOST=""
}

cleanup() { stop_client_load; stop_server; }
trap cleanup EXIT INT TERM

resolve_ip() {
    local host="$1"
    ssh_run "${host}" "hostname -I | awk '{print \$1}'"
}

# Original randomized load loop, run verbatim on the client. $1 = server IP,
# $PORT = iperf3 port. `timeout` bounds the whole loop; the trap kills the
# current iperf3 child so the loop stops promptly instead of being orphaned.
read -r -d '' LOAD_BODY <<'LOAD' || true
SERVER="${1:-127.0.0.1}"
PORT="${PORT:-5201}"
trap 'pkill -P $$ iperf3 2>/dev/null; exit 0' TERM INT

while true; do
    iperf3 -c "$SERVER" -p "$PORT" -t 5 -P $(( RANDOM % 8 + 1 ))

    sleep $(( RANDOM % 4 + 1 ))

    RATE=$(( RANDOM % 900 + 100 ))M
    iperf3 -c "$SERVER" -p "$PORT" -u -b "$RATE" -t $(( RANDOM % 10 + 3 ))

    sleep $(( RANDOM % 5 + 1 ))

    iperf3 -c "$SERVER" -p "$PORT" -R -t $(( RANDOM % 8 + 4 )) -P $(( RANDOM % 4 + 1 ))

    sleep $(( RANDOM % 6 + 1 ))
done
LOAD

run_load() {
    local client="$1" server_ip="$2"
    CLIENT_HOST="${client}"
    echo "----- [$(date '+%F %T')] randomized load on ${client} -> ${server_ip}:${IPERF_PORT} for ${EXP_TIME} -----"

    # `timeout` sends TERM after EXP_TIME, then KILL after KILL_GRACE.
    # --foreground lets signals reach the remote command through ssh.
    set +e
    ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new \
        "${SSH_USER}@${client}" \
        "PORT='${IPERF_PORT}' timeout --foreground -k ${KILL_GRACE} ${EXP_TIME} bash -s -- '${server_ip}'" \
        <<<"${LOAD_BODY}"
    local rc=$?
    set -e
    # 124 = timed out as expected; 0 = clean exit via trap.
    if [[ $rc -ne 124 && $rc -ne 0 ]]; then
        echo "!! client load on ${client} exited with ${rc}" >&2
    fi
    stop_client_load
}

run_pair() {
    local client="$1" server="$2"
    echo "===== [$(date '+%F %T')] experiment ${client} -> ${server} ====="

    start_server "${server}"
    local server_ip
    server_ip="$(resolve_ip "${server}")"
    if [[ -z "${server_ip}" ]]; then
        echo "!! failed to resolve ip of ${server}" >&2
        stop_server
        return 1
    fi

    run_load "${client}" "${server_ip}"

    stop_server
    echo "===== [$(date '+%F %T')] finished ${client} -> ${server} ====="
}

echo "===== sweep start: ${#PAIRS[@]} pairs, ${EXP_TIME} each, ${REST_TIME} rest ====="
for i in "${!PAIRS[@]}"; do
    read -r client server <<<"${PAIRS[$i]}"
    run_pair "${client}" "${server}" \
        || echo "!! pair ${client} -> ${server} failed, continuing" >&2
    if [[ $i -lt $((${#PAIRS[@]} - 1)) ]]; then
        echo "----- resting ${REST_TIME} before next pair -----"
        sleep "${REST_TIME}"
    fi
done

echo "===== all done ====="