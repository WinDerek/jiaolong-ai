#!/bin/bash

BEGIN=$(date +"%Y%m%d_%H%M%S")
mkdir -p ~/.jiaolong/server/log/${BEGIN}

nohup stdbuf -i0 -o0 -e0 bash ./start_server.sh > ~/.jiaolong/server/log/${BEGIN}/log.txt 2>&1 </dev/null & process_id=$!
disown
echo "PID: $process_id"
echo $process_id > ~/.jiaolong/server/log/${BEGIN}/pid.txt
echo
tail -f ~/.jiaolong/server/log/${BEGIN}/log.txt
