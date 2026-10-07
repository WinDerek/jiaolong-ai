#!/bin/bash

podman run --rm -v "$(pwd)":/workspace jiaolong-builder bash -c "bash ./test.sh"
