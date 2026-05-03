#!/bin/bash
while true; do
    stress-ng --cpu 0 --timeout 5s --temp-path /tmp
    sleep 3
    stress-ng --cpu $(( RANDOM % $(nproc) + 1 )) --timeout 8s --temp-path /tmp
    sleep 1
done

