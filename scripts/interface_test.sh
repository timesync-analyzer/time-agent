#!/bin/bash

SERVER="${1:-127.0.0.1}"
PORT="${PORT:-5201}"

while true; do
    # короткий burst с высокой нагрузкой
    iperf3 -c "$SERVER" -p "$PORT" -t 5 -P $(( RANDOM % 8 + 1 ))

    sleep $(( RANDOM % 4 + 1 ))

    # UDP с рандомным bitrate
    RATE=$(( RANDOM % 900 + 100 ))M
    iperf3 -c "$SERVER" -p "$PORT" -u -b "$RATE" -t $(( RANDOM % 10 + 3 ))

    sleep $(( RANDOM % 5 + 1 ))

    # TCP reverse: нагрузка в обратную сторону
    iperf3 -c "$SERVER" -p "$PORT" -R -t $(( RANDOM % 8 + 4 )) -P $(( RANDOM % 4 + 1 ))

    sleep $(( RANDOM % 6 + 1 ))
done