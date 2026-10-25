#!/usr/bin/env bash
# Build Apple Silicon bundles, then test their ownership and native GTK clients.
set -euo pipefail
cd "$(dirname "$0")/.."
[[ $(uname -s) == Darwin && $(uname -m) == arm64 ]] || { echo 'Requires Apple Silicon macOS' >&2; exit 1; }
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
export PATH="$(brew --prefix coreutils)/libexec/gnubin:$PATH"
export PKG_CONFIG_PATH="${PKG_CONFIG_PATH:-}:$(brew --prefix libffi)/lib/pkgconfig:$(brew --prefix)/lib/pkgconfig"
compiler=${SUN_EXECUTABLE:-sun}
mkdir -p build/generated src/generated tmp
"$compiler" tools/build.sun -- prepare
cmake -S native -B build/native -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel 2
"$compiler" --dynamic -L"$(brew --prefix)/lib" -lglib-2.0 -c -o build/sun-gir src/generator/main.sun
# Homebrew keeps some transitive GIR files inside their individual kegs.
gir_paths=()
for directory in "$(brew --prefix)"/opt/*/share/gir-1.0; do
  if [[ -d "$directory" ]]; then gir_paths+=(--gir-dir "$directory"); fi
done
build/sun-gir --selection bindings/selection.xml --overrides bindings/overrides.xml \
  --output-dir build/generated --source-dir src/generated --target arm64-apple-darwin \
  --gir-dir "${SUN_GTK_PREFIX:-$(brew --prefix gtk4)}/share/gir-1.0" \
  --gir-dir "$(brew --prefix glib)/share/gir-1.0" \
  "${gir_paths[@]}"
"$compiler" --dynamic --emit-moon -o build/sun_gobject.moon src/sun_gobject.sun
"$compiler" --dynamic --lib-path build --emit-moon -o build/sun_gtk.moon src/sun_gtk.sun
if [[ ${1:-} == --bundles ]]; then exit 0; fi
# pkg-config returns shell-separated linker arguments without spaces in Homebrew paths.
read -r -a gtk_flags <<< "$(pkg-config --libs gtk4 librsvg-2.0)"
"$compiler" --dynamic --lib-path build -Lbuild/native -lsun_gobject_test_support "${gtk_flags[@]}" -c -o build/sun-gobject-tests tests/main.sun
build/sun-gobject-tests_test --test-filter runtime_tests --test-sequential
"$compiler" --dynamic --lib-path build -Lbuild/native -lsun_gobject_native "${gtk_flags[@]}" -c -o build/gtk-demo examples/gtk-demo.sun
"$compiler" --dynamic --lib-path build -Lbuild/native -lsun_gobject_native "${gtk_flags[@]}" -c -o build/widgets-client tests/widgets-client.sun
# Cocoa may warn about skipped presentation frames; keep critical API failures fatal.
export G_DEBUG=fatal-criticals GTK_A11Y=none GSK_RENDERER=cairo
# Native file panels synthesize a click when becoming key. Keep the unattended
# runner's pointer away from the menu bar so that click cannot start menu tracking.
swift -e 'import CoreGraphics; let bounds = CGDisplayBounds(CGMainDisplayID()); CGWarpMouseCursorPosition(CGPoint(x: bounds.midX, y: bounds.midY))'
gtimeout 30s build/gtk-demo --self-test
# Capture a native stack before the timeout terminates a stalled widget test.
build/widgets-client &
widget_pid=$!
( sleep 20; sample "$widget_pid" 1 -file build/widgets-sample.txt >/dev/null 2>&1 || true; kill -TERM "$widget_pid" 2>/dev/null || true ) &
watchdog_pid=$!
widget_status=0
wait "$widget_pid" || widget_status=$?
kill "$watchdog_pid" 2>/dev/null || true
if [[ $widget_status != 0 ]]; then
  cat build/widgets-sample.txt 2>/dev/null || true
  exit "$widget_status"
fi
mkdir -p build/macos/lib/sun build/macos/lib build/macos/share/sun_gobject
cp build/sun_gobject.moon build/sun_gtk.moon build/macos/lib/sun/
cp build/native/libsun_gobject_native.a build/macos/lib/
cp LICENSE build/sun-version.txt build/native/native-versions.txt build/generated/provenance.json build/macos/share/sun_gobject/
tar -C build/macos -czf build/sun_gobject-arm64-apple-darwin.tar.gz .
