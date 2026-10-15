# sun_gobject

Sun-native GIR generation and a curated GLib/GObject/GIO/GTK4 foundation for a
future Linux Sun UI library. This repository contains the generator, raw bindings,
explicit ownership helpers, a small signal adapter, and a GTK4 example.

## Development

Open the repository in its devcontainer, then run:

```sh
./build.sh
```

The Ubuntu 26.04 container installs the **Sun dev release Debian package**, including
its matching standard library. It never compiles Sun from source. LLVM 20, CMake,
Ninja, ccache, GTK4 development files, GIR metadata, Xvfb, and D-Bus are included.
The current release tested here reports `sun 0.dev (cc7686189ee2)`.

Outside a container, install the same system dependencies listed in `Dockerfile`.
`./scripts/bootstrap-sun.sh` installs the package if Sun is absent; `--update`
explicitly downloads the current dev package. Existing installations are retained
and their version is printed. `SUN_EXECUTABLE=/absolute/path/to/sun ./build.sh`
selects another installation; it must have a matching standard library.

The `dev` release is mutable. Rebuild the image without cache to update it. Build
provenance records the compiler version, generator revision, selection/override
hashes and every input GIR hash; CI also uploads these records. No compiler source
checkout or source-build fallback is needed.

```sh
cmake -S native --preset dev-debug # unoptimized native debugging
cmake --build build/native -j$(($(nproc)/2))
sun tools/build.sun -- build
sun tools/build.sun -- test
LD_LIBRARY_PATH="$PWD/build/native" ./build/gtk-demo
```

The last command needs a desktop display. For automated display testing:

```sh
LD_LIBRARY_PATH="$PWD/build/native" GTK_A11Y=none GSK_RENDERER=cairo \
  dbus-run-session -- xvfb-run -a ./build/gtk-demo --self-test
```

The application opens a window and button; clicking changes its label. The
self-test activates the real signal, checks the resulting label, closes the
window, and verifies callback cleanup. The native Sun integration test runs this
scenario under Xvfb with a timeout.

All automated checks are native Sun `test_function` tests, registered through
`tests/main.sun` in `sun-config.json`. CMake only builds the C libraries under
`native/`; `tools/build.sun` generates bindings, compiles Sun artifacts, and runs
the companion Sun test binary. The generator tests cover XML
validation, GIR metadata and golden output, while the integration tests build an
isolated installed consumer and run the GTK scenario. Scratch files stay under
`tmp/` and are removed after each test.

After configuring and building, the Sun extension discovers individual tests in
VS Code's Testing view. It uses the compiled test binary when current. The
`scripts/sun-test-compiler.sh` launcher rebuilds it when sources change, because
`sun test` currently does not accept this suite's native linker flags. The launcher
only handles build/run setup; test logic and assertions are Sun code. To run an
individual test from the workspace root:

```sh
./scripts/sun-test-compiler.sh test tests/main.sun --test-filter generator_tests.valid_xml
```

## Architecture and supported surface

- `src/generator/`: Sun XML reader, GIR dependency loader, ABI translator and CLI.
- `bindings/selection.xml`: reviewed functions selected from GLib, GObject, GIO,
  and GTK4. Add declarations here only when their ABI mapping is supported.
- `bindings/overrides.xml`: explicit GIR corrections. A `<set symbol="..."
  attribute="..." value="..."/>` updates an attribute on exactly one callable.
- `src/support.sun`: owned objects/strings/errors, boolean/string GValue
  properties, and signal connection lifetime helpers.
- `native/`: C sources, CMake configuration, and native build presets; compiled
  artifacts go in `build/native/`. This subproject builds independently of Sun.
- `tools/build.sun`: Sun build, test, and installation orchestration.
- `native/signals.c`: typed adapters for void signals with no parameters and
  `notify` signals. GObject erases function-pointer types that Sun cannot currently
  cast. The adapter checks the runtime signature before connecting.

The generator recursively loads versioned dependencies. The indexed GIR model
retains all attributes, including inheritance, interfaces, ownership, nullability,
array lengths, callback scope/closure/destroy indices, properties and signals.
Raw declarations use native C names in `glib.raw`, `gobject.raw`, `gio.raw`, and
`gtk.raw`. Objects remain opaque `raw_ptr<u8>` values; there are no fabricated
GObject instance layouts. Enum/flag parameters resolve against dependency metadata.

Only curated declarations are emitted. Unknown selected ABI types, missing symbols,
missing dependency versions, and unsupported signatures fail generation before
writing outputs. Unselected symbols are reported as **not evaluated**, not as
supported. Arrays with a C pointer spelling expose that pointer without automatic
length or ownership conversion. Ownership annotations remain in the GIR model;
they do not imply generated safe wrappers.

XML supports UTF-8, namespaces, predefined/numeric entities, quoted attributes,
text, comments, processing instructions and CDATA. DTDs/external entities are
rejected. This is an application XML reader, not a fully validating XML processor.

```sh
mkdir -p build/generated
build/sun-gir --selection bindings/selection.xml \
  --overrides bindings/overrides.xml --gir-dir /usr/share/gir-1.0 \
  --output-dir build/generated --target x86_64-linux-gnu \
  --support-dir "$PWD/src"
```

Generated sources, bundle entrypoints, `coverage.tsv`, `gir-model.json`, and
`provenance.json` live under `build/generated`. The Sun build driver supplies compiler and generator
revision values. Sources are deterministic for identical inputs and options.

## Ownership and consumer use

Native pointer validity, property types, and transferred reference ownership are
caller contracts. These wrappers do **not** make arbitrary foreign pointers safe.
`OwnedObject` adopts one owned reference; `retain_borrowed` adds a reference;
`sink_floating` sinks a floating reference or retains an ordinary object.
`borrow()` never transfers ownership and `release()` does. Sun moves owned handles.

Signal callbacks must be named module functions, with heap/global userdata whose
lifetime covers the connection. Registration takes responsibility for calling the
supplied cleanup exactly once, including rejected signals. A `Connection` retains
the emitter and disconnects before releasing it. Do not capture stack frames or
let callbacks use userdata after disconnecting themselves. GTK operations belong
on the GTK thread.

Install the two bundles and adapter with:

```sh
sun tools/build.sun -- install "$PWD/dist"
sun --dynamic --path-var SUN_GOBJECT_BUILD="$PWD/dist/lib/sun" \
  --lib-path "$PWD/dist/lib/sun" \
  -L"$PWD/dist/lib" -lsun_gobject_native $(pkg-config --libs gtk4) \
  -c -o my-app examples/gtk-demo.sun
LD_LIBRARY_PATH="$PWD/dist/lib" ./my-app
```

Consumers import `sun_gobject.moon`, `sun_gtk.moon` and `stdlib.moon`. Native GTK/GLib
libraries remain system dependencies; do not statically bundle a second GObject
runtime. The tests install into a separate prefix and build a separate consumer
with its own root config, so it cannot accidentally import build-tree bundles.

Initial support is native **x86_64 Linux GNU** only. Bundles must match the Sun
compiler and platform. Custom subclasses, general closure marshalling, libadwaita,
full GTK API coverage and cross-compilation are outside this initial milestone.
Compiler issue drafts and reproducible limitations are in [sun_feedback.md](sun_feedback.md).

## Optional host integrations

The default devcontainer needs no host credential or agent-state directories.
For interactive Linux X11 use, add a local devcontainer override with `DISPLAY`,
a read-only `/tmp/.X11-unix` mount and your Xauthority cookie. For Wayland, forward
the Wayland socket and matching runtime directory. Headless tests need neither.
Agent extensions and host `.ssh`, `.config/gh`, `.claude` or `.codex` mounts may be
added locally; do not commit credentials. Add `--cap-add=PERFMON` only if you need
host-supported profiling. No privileged container is required.

Inspired by [Ghostty's zig-gobject](https://github.com/ghostty-org/zig-gobject).
Development conventions follow [Sun](https://github.com/namo-robotics/sun).
