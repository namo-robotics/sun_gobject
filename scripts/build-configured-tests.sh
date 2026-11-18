#!/usr/bin/env bash
# Prepare native fixtures and path metadata, then compile the configured test suite.
set -euo pipefail
: "${SUN_EXECUTABLE:?Run through sun build}"
: "${SUN_BUILD:?Missing build directory}"

native="$SUN_BUILD/native-tests"
cmake -S src/native -B "$native" -DBUILD_TESTING=ON
cmake --build "$native" --target sun_gobject_test_support -j "${SUN_JOBS:-2}"
mkdir -p tmp
python3 - "$SUN_BUILD" "$SUN_EXECUTABLE" "$PWD" <<'PY'
"""Write test paths as escaped Sun string literals using the shared template."""
from pathlib import Path
import sys
text = Path('tests/build_config.sun.in').read_text()
for key, value in zip(('BUILD_DIR', 'SUN_COMPILER', 'WORKSPACE_ROOT'), sys.argv[1:]):
    text = text.replace('@' + key + '@', value.replace('\\', '\\\\').replace('"', '\\"'))
path = Path(sys.argv[1]) / 'test-config.sun'
if not path.exists() or path.read_text() != text:
    path.write_text(text)
PY
read -r -a flags <<< "$(pkg-config --libs gtk4 gio-2.0 librsvg-2.0)"
# Fixtures consume the same configured bundles and compiler as the suite.
for fixture in gtk-demo event-client widgets-client; do
  source="tests/$fixture.sun"
  library=sun_gobject_native
  if [[ $fixture == gtk-demo ]]; then source=examples/gtk-demo.sun; fi
  if [[ $fixture == event-client ]]; then library=sun_gobject_test_support; fi
  env -u SUN_BUILD_CONTEXT "$SUN_EXECUTABLE" --dynamic -c --no-test \
    --lib-path "$SUN_BUILD" --path-var "SUN_BUILD=$SUN_BUILD" \
    -L"$native" -L"$SUN_BUILD/native" -l"$library" -lm "${flags[@]}" \
    -o "$SUN_BUILD/$fixture" "$source"
done
"$SUN_EXECUTABLE" build-compile --dynamic -L"$native" \
  -lsun_gobject_test_support -lm "${flags[@]}"
