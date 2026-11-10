#!/usr/bin/env bash
# Verify GIR cache reuse and invalidation with a generator that never compiles code.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$repo/tmp"
fixture=$(mktemp -d "$repo/tmp/gir-cache.XXXXXX")
trap 'rm -rf "$fixture"' EXIT
mkdir -p "$fixture/src/bindings" "$fixture/build/host" "$fixture/bin"
printf 'selection\n' > "$fixture/src/bindings/selection.xml"
printf 'overrides\n' > "$fixture/src/bindings/overrides.xml"
cat > "$fixture/bin/compiler" <<'TOOL'
#!/bin/sh
exit 0
TOOL
cat > "$fixture/bin/pkg-config" <<'TOOL'
#!/bin/sh
exit 0
TOOL
cat > "$fixture/bin/uname" <<'TOOL'
#!/bin/sh
echo Linux
TOOL
cat > "$fixture/build/host/sun_gir" <<'TOOL'
#!/bin/sh
set -eu
[ ! -f fail-generator ] || exit 1
echo generated >> calls
mkdir -p build/generated/sources
printf 'bindings\n' > build/generated/sources/example.sun
printf '{}\n' > build/generated/provenance.json
TOOL
chmod +x "$fixture/bin/"* "$fixture/build/host/sun_gir"
cd "$fixture"
export PATH="$fixture/bin:$PATH"
export SUN_EXECUTABLE="$fixture/bin/compiler" SUN_BUILD="$fixture/build" SUN_TARGET=x86_64-linux-gnu

# Run the configured script and assert how often it invokes generation.
check_calls() {
  bash "$repo/scripts/build-configured-library.sh" > "$fixture/output"
  [[ $(wc -l < calls) -eq $1 ]]
}
check_calls 1
check_calls 1
grep -q 'Up to date: GIR bindings' output
printf 'changed selection\n' >> src/bindings/selection.xml
check_calls 2
printf 'changed overrides\n' >> src/bindings/overrides.xml
check_calls 3
printf '# changed generator\n' >> build/host/sun_gir
check_calls 4
rm build/generated/sources/example.sun
check_calls 5
printf 'corrupt\n' > build/generated/provenance.json
check_calls 6
printf 'another selection\n' >> src/bindings/selection.xml
touch fail-generator
if bash "$repo/scripts/build-configured-library.sh" > output 2>&1; then
  echo 'Expected generation failure' >&2
  exit 1
fi
rm fail-generator
check_calls 7
check_calls 7
# Hash failures must stop the build rather than produce a reusable partial stamp.
cp build/host/gir-generation.sha256 previous-stamp
mv src/bindings/selection.xml selection-backup
if bash "$repo/scripts/build-configured-library.sh" > output 2>&1; then
  echo 'Expected missing input failure' >&2
  exit 1
fi
cmp previous-stamp build/host/gir-generation.sha256
mv selection-backup src/bindings/selection.xml
check_calls 7
echo 'GIR cache checks passed'
