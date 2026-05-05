#!/bin/bash
# worker.sh NAME — бесконечно работает, пишет в stdout
NAME="${1:-worker}"
while true; do
    echo "$(date '+%H:%M:%S') $NAME alive"
    sleep 2
done
