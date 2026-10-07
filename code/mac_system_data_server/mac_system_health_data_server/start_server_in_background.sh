#!/bin/bash

set -euo pipefail

BEGIN=$(date +"%Y%m%d_%H%M%S")
mkdir -p ~/.jiaolong/mac_system_health_data_server/log/${BEGIN}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT="${MAC_SYSTEM_HEALTH_SERVER_PORT:-8988}"

nohup "${SCRIPT_DIR}/build/mac_system_health_data_server" --port "${PORT}" > ~/.jiaolong/mac_system_health_data_server/log/${BEGIN}/log.txt 2>&1 </dev/null & process_id=$!
disown
echo "pid: ${process_id}"
echo "${process_id}" > ~/.jiaolong/mac_system_health_data_server/log/${BEGIN}/pid.txt
echo "Mac System Health Data Server started on port ${PORT} with PID ${process_id}"
