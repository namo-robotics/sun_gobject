#!/usr/bin/env bash
# Build native libraries, then delegate Sun compilation and tests to Sun.
set -euo pipefail

cd "$(dirname "$0")"
./scripts/bootstrap-sun.sh

# Leave half the CPU cores available and honor a lower job limit.
parallel_jobs=$(($(nproc) / 2))
if ((parallel_jobs < 1)); then
  parallel_jobs=1
fi
if [[ -n ${BUILD_JOBS:-} ]] && ((BUILD_JOBS < parallel_jobs)); then
  parallel_jobs=$BUILD_JOBS
fi

cmake -S src/native --preset "${CMAKE_PRESET:-dev-fast}"
cmake --build build/native -j "$parallel_jobs"

compiler=${SUN_EXECUTABLE:-sun}
"$compiler" tools/build.sun -- build
"$compiler" tools/build.sun -- test | tee build/test-results.log
