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
"$compiler" --version > build/sun-version.txt 2>&1
cmake -S native -B build/native -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel 2
"$compiler" --dynamic -L"$(brew --prefix)/lib" -lglib-2.0 -c -o build/sun-gir src/generator/main.sun
build/sun-gir --selection bindings/selection.xml --overrides bindings/overrides.xml \
  --output-dir build/generated --source-dir src/generated --target arm64-apple-darwin \
  --gir-dir "${SUN_GTK_PREFIX:-$(brew --prefix gtk4)}/share/gir-1.0" \
  --gir-dir "$(brew --prefix glib)/share/gir-1.0" \
  --gir-dir "$(brew --prefix gtk4)/share/gir-1.0"
"$compiler" --dynamic --emit-moon -o build/sun_gobject.moon src/sun_gobject.sun
"$compiler" --dynamic --lib-path build --emit-moon -o build/sun_gtk.moon src/sun_gtk.sun
if [[ ${1:-} == --bundles ]]; then exit 0; fi
# pkg-config returns shell-separated linker arguments without spaces in Homebrew paths.
read -r -a gtk_flags <<< "$(pkg-config --libs gtk4 librsvg-2.0)"
"$compiler" --dynamic --lib-path build -Lbuild/native -lsun_gobject_test_support "${gtk_flags[@]}" -c -o build/sun-gobject-tests tests/main.sun
build/sun-gobject-tests_test --test-filter runtime_tests --test-sequential
"$compiler" --dynamic --lib-path build -Lbuild/native -lsun_gobject_native "${gtk_flags[@]}" -c -o build/gtk-demo examples/gtk-demo.sun
"$compiler" --dynamic --lib-path build -Lbuild/native -lsun_gobject_native "${gtk_flags[@]}" -c -o build/widgets-client tests/widgets-client.sun
export G_DEBUG=fatal-warnings GTK_A11Y=none GSK_RENDERER=cairo
 gtimeout 30s build/gtk-demo --self-test
 gtimeout 30s build/widgets-client
mkdir -p build/macos/lib/sun build/macos/lib build/macos/share/sun_gobject
cp build/sun_gobject.moon build/sun_gtk.moon build/macos/lib/sun/
cp build/native/libsun_gobject_native.a build/macos/lib/
cp LICENSE build/sun-version.txt build/native/native-versions.txt build/generated/provenance.json build/macos/share/sun_gobject/
tar -C build/macos -czf build/sun_gobject-arm64-apple-darwin.tar.gz .
