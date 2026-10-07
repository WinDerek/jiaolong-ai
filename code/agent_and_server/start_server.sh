#!/bin/bash

podman run --rm --add-host=host.containers.internal:host-gateway -p ${JIAOLONG_SERVER_PORT}:${JIAOLONG_SERVER_PORT} -v "${JIAOLONG_WORKSPACE}":/workspace -v "$(echo ~)/.ssh":/root/.ssh -v "$(echo ~)/.jiaolong":/root/.jiaolong jiaolong-builder ${JIAOLONG_SERVER_PATH} --port ${JIAOLONG_SERVER_PORT}
