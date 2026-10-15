#!/usr/bin/env bash
# Rebuild native test dependencies when the editor cannot use its cached binary.
# The Sun test subcommand does not accept the native link flags this suite needs.
set -euo pipefail
workspace_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${1:-} != test || $(realpath -m -- "${2:-.}") != "$workspace_root/tests/main.sun" ]]; then
  exec "${SUN_EXECUTABLE:-sun}" "$@"
fi
shift 2
build_dir="$workspace_root/build"
parallel_jobs=$(($(nproc) / 2))
if ((parallel_jobs < 1)); then parallel_jobs=1; fi
cmake --build "$build_dir/native" -j"$parallel_jobs"
"${SUN_EXECUTABLE:-sun}" "$workspace_root/tools/build.sun" -- build
exec "$build_dir/sun-gobject-tests_test" "$@"
