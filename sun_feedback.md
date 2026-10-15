# Sun compiler and standard-library feedback

Issue-ready drafts only; no upstream issues have been created. Tested against
`sun 0.dev (cc7686189ee2)` from the `dev` Debian package. Re-run the probes when
updating the package. Each item remains open until an upstream issue or fix is linked.

## Provide XML parsing in the Sun standard library

- **Status/link:** Draft; not filed.
- **Use case:** Consume GObject Introspection XML entirely in Sun, including namespace
  resolution, UTF-8, entities, attributes, text, comments, and CDATA.
- **Reproduction:** Build this repository and run `build/sun-gir --check-xml tests/fixtures/xml-valid.xml`.
  The application currently needs the reader in `generator/xml.sun`; there is no
  XML reader in the inspected standard library.
- **Expected:** A reusable streaming or indexed XML reader with structured
  filename/line/column errors, namespace URI resolution, and external entities
  disabled by default.
- **Current workaround:** A local indexed tree reader. It owns input and decoded
  strings, rejects DTDs, and stops on malformed input. Its CLI diagnostics terminate
  the process; a stdlib version should return typed errors instead.
- **Acceptance:** Port the XML fixtures, expose error values, document the supported
  XML profile, and let this generator remove its local parser.

## Support explicit unsafe C function-pointer casts for callback adapters

- **Status/link:** Draft; not filed.
- **Reproduction:** `sun --emit-obj -o tmp/callback-cast.o tests/fixtures/callback-cast.sun`.
- **Actual:** `No overload of 'typed' matches expected type 'function () void'`.
- **Expected:** An explicit unsafe way to erase and recover C function pointers,
  with no implicit signature coercion. A mismatched invocation must remain unsafe.
- **Impact:** GObject's `GCallback` erases signal argument types; a typed Sun module
  function cannot be passed directly to `g_signal_connect_data`.
- **Workaround:** `native/signals.c` validates the runtime signal signature before
  connecting a typed trampoline. It also converts GClosure's destroy callback to
  the userdata-only cleanup callback and handles exactly-once cleanup.
- **Acceptance:** A typed callback can cross an explicitly erased C API and be
  invoked through its original compatible ABI; mismatched direct calls still fail.

## Represent nullable C function pointers

- **Status/link:** Draft; not filed.
- **Reproduction:** `sun --emit-obj -o tmp/callback-null.o tests/fixtures/callback-null.sun`.
- **Actual:** `No matching overload of 'accepts_optional' for argument types (null)`.
- **Expected:** A nullable C function-pointer type with checked narrowing before
  invocation and the native one-word representation.
- **Impact:** Many GObject callbacks and destroy-notify parameters are optional.
- **Workaround:** Require a named no-op destroy callback where no cleanup is needed;
  do not silently reinterpret null as a non-null function pointer.
- **Acceptance:** A nullable callback can pass to C as null, and invocation requires
  proving it is present.
