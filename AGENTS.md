# Agent Development Guide

A file for guiding coding agents.

## Commands

- Never use git commands except readonly ones like `git status` and `git diff`.
- Run all commands from the workspace root. Create any temp files in `${workspaceRoot}/tmp`.
- `sun fmt` for formating sun source files
- Full build and tests: `./build.sh`.
- Native build: `cmake -S native --preset dev-fast`, then `cmake --build build/native -j$(($(nproc)/2))`. Never use all cores; a full-speed build exhausts the machine's memory.
- Sun build: `sun tools/build.sun -- build` after building native dependencies.
- Tests: `sun tools/build.sun -- test` runs the native Sun test functions in parallel. Use `./scripts/sun-test-compiler.sh test tests/main.sun --test-filter <name>` to rebuild and run selected tests.
- Keep production Sun source in `src/`, C source and CMake configuration in `native/`, XML binding configuration in `bindings/`, and generated artifacts in `build/`.
- Keep GTK bindings in `src/gtk/` and SVG bindings in `src/svg/`, with at most one class per file. Shared native declarations, enums, and helpers belong in support files. Update the generator's bundle source list when adding or moving binding files.

## Sun Code Conventions

- Implement project functionality, tooling, and test logic in Sun where practical. Use C for native interoperability that Sun cannot reasonably provide.
- Sun does not allow implicit copies. Move owned values or use explicit copies as required.
- Sun does not have macros.
- Keep allocations explicit and minimize `unsafe` blocks, especially in the public GTK API.

## Code Comments (all code, including .sun files)
- Write concise comments in plain English for a general audience of software engineers.
- Every namespace, function, and type definition must have a documentation comment describing what it does or what it is for. This applies to all first-party source code, including C++ and `.sun` files, regardless of visibility.
- Use `/** ... */` blocks in C++, Sun, and JavaScript/TypeScript. Use the language's native equivalent elsewhere, such as Python docstrings or a shell comment block.
- Cover private and internal helpers, methods, constructors, destructors, anonymous namespaces, classes, structs, interfaces, enums, and type aliases, as well as public APIs. Document modules too.
- Put the comment immediately before the declaration or definition it documents. For a function declared in a header and implemented separately, document the declaration; add implementation comments when they explain details beyond that contract.
- Explain the purpose or behavior instead of merely restating the symbol name. Mention ownership, side effects, constraints, or failure behavior when needed to understand correct use.
- Preserve useful existing documentation and keep it accurate when changing code. Do not edit generated code or third-party dependencies to add comments.
- Do not hard-code numeric values that are subject to change in comments.

## Commit messages
- Subject: `<scope>: <description>` (scope = subsystem/package/area; imperative; no `feat`/`fix` types).
- Body: blank line, then one concise bullet per key change if not already captured in the subject.
- Do not add AI attribution to commits or PRs (no Co-Authored-By, Generated-with, tool names, or session links).

## Planning

- Write plains in plain english, avoiding unnecessary jargon and abreviations.
- Use simple, concrete examples in explanations when it makes sense to do so.

## Issues and PR Guidelinese

- Never create PRs or issues unless specifically asked by the user.

## Roadmap

- Roadmap items that are entirely completed should be removed from the roadmap doc.
