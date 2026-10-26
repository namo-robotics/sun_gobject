#!/usr/bin/env bash
# Package the tested Linux GTK bundles and native archive for release.
set -euo pipefail
cd "$(dirname "$0")/.."
"${SUN_EXECUTABLE:-sun}" tools/package.sun
