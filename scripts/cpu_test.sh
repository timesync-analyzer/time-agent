#!/bin/bash
while true; do
    sudo stress-ng --cpu 0 --timeout 10s --temp-path /tmp
    sleep 10

    sleep 5

    sudo stress-ng --cpu 0 --timeout 5s --temp-path /tmp
    sleep 7

    sleep 3
done


