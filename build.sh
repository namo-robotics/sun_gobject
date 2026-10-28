#!/usr/bin/env bash
# Build native libraries, then delegate Sun compilation and tests to Sun.
set -euo pipefail
cd "$(dirname "$0")"
./scripts/bootstrap-sun.sh
parallel_jobs=$(($(nproc) / 2))
if ((parallel_jobs < 1)); then parallel_jobs=1; fi
if [[ -n ${BUILD_JOBS:-} ]] && ((BUILD_JOBS < parallel_jobs)); then parallel_jobs=$BUILD_JOBS; fi
cmake -S src/native --preset "${CMAKE_PRESET:-dev-fast}"
cmake --build build/native -j "$parallel_jobs"
"${SUN_EXECUTABLE:-sun}" tools/build.sun -- build
"${SUN_EXECUTABLE:-sun}" tools/build.sun -- test | tee build/test-results.log
