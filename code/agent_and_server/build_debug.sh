#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

if [ ! -d "${BUILD_DIR}" ]; then
    mkdir -p "${BUILD_DIR}"
    echo "Created build directory: ${BUILD_DIR}"
fi

cd "${BUILD_DIR}"

echo "Running CMake..."
cmake -DCMAKE_BUILD_TYPE=Debug ..

echo ""
echo "Building..."
cmake --build . -j 4

echo ""
echo "Files in build directory:"
ls -la

cd "${SCRIPT_DIR}"
echo ""
echo "Build complete. Returned to: $(pwd)"
