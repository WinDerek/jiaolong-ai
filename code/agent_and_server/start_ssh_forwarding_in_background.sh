#!/bin/bash

BEGIN=$(date +"%Y%m%d_%H%M%S")
mkdir -p ~/.jiaolong/ssh_forwarding/log/${BEGIN}

nohup stdbuf -i0 -o0 -e0 ssh -N -o ServerAliveInterval=30 -o ServerAliveCountMax=3 -o TCPKeepAlive=yes -o ExitOnForwardFailure=yes -R 127.0.0.1:60666:127.0.0.1:60666 ${JIAOLONG_REMOTE_SERVER_USER}@${JIAOLONG_REMOTE_SERVER_HOST} > ~/.jiaolong/ssh_forwarding/log/${BEGIN}/log.txt 2>&1 </dev/null & process_id=$!
disown
echo "PID: $process_id"
echo $process_id > ~/.jiaolong/ssh_forwarding/log/${BEGIN}/pid.txt
echo
tail -f ~/.jiaolong/ssh_forwarding/log/${BEGIN}/log.txt
