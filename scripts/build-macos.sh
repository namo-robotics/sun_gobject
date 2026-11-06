#!/usr/bin/env bash
# Build Apple Silicon bundles, test native GTK clients, and package the release.
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ $(uname -s) != Darwin || $(uname -m) != arm64 ]]; then
  echo 'Requires Apple Silicon macOS' >&2
  exit 1
fi

brew_prefix=$(brew --prefix)
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
export PATH="$(brew --prefix coreutils)/libexec/gnubin:$PATH"
export PKG_CONFIG_PATH="${PKG_CONFIG_PATH:-}:$(brew --prefix libffi)/lib/pkgconfig:$brew_prefix/lib/pkgconfig"

# Keep Sun's archive paths aligned with CMake and the test fixtures.
compiler=("${SUN_EXECUTABLE:-sun}" --path-var SUN_BUILD=build)
sun_flags=(--dynamic --lib-path build)

# Build native libraries and regenerate the Sun bundles for Apple Silicon.
build_bundles() {
  mkdir -p build/generated build/generated/sources tmp
  "${compiler[@]}" tools/build.sun -- prepare
  cmake -S src/native -B build/native \
    -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
  cmake --build build/native --parallel 2

  "${compiler[@]}" "${sun_flags[@]}" -L"$brew_prefix/lib" -lglib-2.0 \
    -c -o build/sun_gir src/generator/main.sun

  # Homebrew keeps transitive GIR files inside their individual kegs.
  local directory
  local gir_paths=()
  for directory in "$brew_prefix"/opt/*/share/gir-1.0; do
    if [[ -d "$directory" ]]; then
      gir_paths+=(--gir-dir "$directory")
    fi
  done
  build/sun_gir \
    --selection src/bindings/selection.xml \
    --overrides src/bindings/overrides.xml \
    --output-dir build/generated --source-dir build/generated/sources \
    --target arm64-apple-darwin \
    --gir-dir "${SUN_GTK_PREFIX:-$(brew --prefix gtk4)}/share/gir-1.0" \
    --gir-dir "$(brew --prefix glib)/share/gir-1.0" \
    "${gir_paths[@]}"

  "${compiler[@]}" "${sun_flags[@]}" \
    --emit-moon -o build/sun_gobject.moon src/sun_gobject.sun
  "${compiler[@]}" "${sun_flags[@]}" \
    --emit-moon -o build/sun_gtk.moon src/sun_gtk.sun
}

# Compile a test program, linking its native adapter before GTK dependencies.
build_test_program() {
  local output=$1 source=$2 native_library=$3
  "${compiler[@]}" "${sun_flags[@]}" \
    -Lbuild/native "-l$native_library" "${gtk_flags[@]}" \
    -c -o "$output" "$source"
}

# Run the widget client with a stack capture and deadline for stalled tests.
check_widgets() {
  build/widgets-client &
  local widget_pid=$!
  (
    sleep 20
    sample "$widget_pid" 1 -file build/widgets-sample.txt >/dev/null 2>&1 || true
    kill -TERM "$widget_pid" 2>/dev/null || true
  ) &
  local watchdog_pid=$!
  local status=0
  wait "$widget_pid" || status=$?
  kill "$watchdog_pid" 2>/dev/null || true
  if [[ $status != 0 ]]; then
    cat build/widgets-sample.txt 2>/dev/null || true
    return "$status"
  fi
}

# Build and run ownership tests and native GUI checks before packaging.
run_tests() {
  local gtk_flags
  # Homebrew's pkg-config linker arguments contain no spaces within paths.
  read -r -a gtk_flags <<< "$(pkg-config --libs gtk4 librsvg-2.0)"
  build_test_program build/sun_gobject_tests tests/main.sun sun_gobject_test_support
  build/sun_gobject_tests_test --test-filter runtime_tests --test-sequential
  build_test_program build/gtk-demo examples/gtk-demo.sun sun_gobject_native
  build_test_program build/widgets-client tests/widgets-client.sun sun_gobject_native

  # Allow Cocoa presentation warnings, but keep critical API failures fatal.
  export G_DEBUG=fatal-criticals GTK_A11Y=none GSK_RENDERER=cairo
  build/native/media-seek-tests

  # Native file panels synthesize a click when focused. Keep it off the menu bar.
  swift -e '
    import CoreGraphics
    let bounds = CGDisplayBounds(CGMainDisplayID())
    CGWarpMouseCursorPosition(CGPoint(x: bounds.midX, y: bounds.midY))
  '
  gtimeout 30s build/gtk-demo --self-test
  check_widgets
}

# Assemble the tested bundles, native library, and provenance into a release archive.
package_release() {
  local package_dir=build/macos
  mkdir -p "$package_dir/lib/sun" "$package_dir/share/sun_gobject"
  cp build/sun_gobject.moon build/sun_gtk.moon "$package_dir/lib/sun/"
  cp build/native/libsun_gobject_native.a "$package_dir/lib/"
  cp LICENSE build/sun-version.txt build/native/native-versions.txt \
    build/generated/provenance.json "$package_dir/share/sun_gobject/"
  tar -C "$package_dir" -czf build/sun_gobject-arm64-apple-darwin.tar.gz .
}

build_bundles
if [[ ${1:-} == --bundles ]]; then
  exit 0
fi
run_tests
package_release
./scripts/package-sun.sh
cp build/sun_gobject-arm64-apple-darwin.tar.gz build/release/
