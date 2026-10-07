#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

if [ ! -d "${BUILD_DIR}" ]; then
  echo "Build directory not found: ${BUILD_DIR}"
  echo "Please build first."
  exit 1
fi

echo "Running all unit tests via CTest in ${BUILD_DIR}..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure
# ./build/jiaolong_agent_test --gtest_filter=JiaolongAgentTest.ReadToolsRejectPathsOutsideTaskWorkingDirectoryEvenWithHack
