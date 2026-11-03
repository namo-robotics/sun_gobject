#!/usr/bin/env bash
# Package the tested Linux GTK bundles and native archive for release.
set -euo pipefail
cd "$(dirname "$0")/.."
"${SUN_EXECUTABLE:-sun}" tools/package.sun
./scripts/package-sun.sh
cp build/sun-gobject_0.dev_amd64.deb build/release/
