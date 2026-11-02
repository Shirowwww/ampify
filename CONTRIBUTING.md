# Contributing to Ampify

Thanks for helping. This page lists the rules of the project and how to make a change that can be merged.

## Project rules

A change that breaks one of these will not be merged.

1. Ampify does not touch FairPlay or any DRM component (CoreFP, CoreADI64). That is a legal limit; every hook goes
   through `src/core/drm_guard.cpp`.
2. The kernel loads only on tested Apple Music versions (`src/core/version_gate.hpp`, plus `versions.txt`) whose
   `AVFoundationCF.dll` has the expected exports. Resolve functions by name, not by a fixed address. The bench build
   (`AMPIFY_BENCH=ON`) accepts any version and is never distributed.
3. Ampify writes nothing to Apple Music's package. All data goes to `%USERPROFILE%\Ampify`.
4. Community code is JavaScript with declared permissions: no native community extensions, no raw file access, no
   network beyond declared hosts. Services that reach outside the process (Discord, tray, remote, MIDI) stay off until
   the user switches them on.
5. An install checks HTTPS, SHA-256, the file list and the manifest before anything is put in place, runs nothing it
   downloaded, and leaves the new element off.
6. The repository holds no personal data and no secrets. Logs and reports that leave a PC have the user name replaced.

## Set up

```powershell
pwsh -File scripts\build-msvc.ps1        # Windows, MSVC, XAML included, into build-msvc\release
git config core.hooksPath scripts/hooks  # fast checks before each push
```

Details: [docs/development/building.md](docs/development/building.md).

## Tests

- Add or update tests with the change: C++ tests in `tests/`, CLI and validator tests in `tools/`, example extensions
  in `tools/test_examples.mjs`. How to run them: [building.md](docs/development/building.md#tests).
- JavaScript is linted with oxlint (`.oxlintrc.json`): run `npm run lint:js` before you push. CI runs it too.
- A bug fix comes with a test that fails before the fix. When a test fails, fix the code, unless the test itself is
  wrong.
- What touches Apple Music itself (injection, XAML, audio) needs the Windows bench or a check on a PC
  ([docs/development/testing.md](docs/development/testing.md)), with a screenshot: a log line that says "applied" can
  be there while nothing shows on screen.

## Code style

- Comments explain why, not what, in English and short.
- Functions under about 50 lines, files under about 800, nesting at most four levels.
- Prefer returning new values to mutating shared state. Handle errors explicitly, never swallow them.
- Validate input at the edges: manifests, archives, paths, URLs, settings. Name constants. Leave no debug output.
- In Apple Music's process, hold every XAML object you create with a strong reference and release it on the UI thread.
  Read [docs/development/xaml-lessons.md](docs/development/xaml-lessons.md) before changing `src/win/`.
- Text files keep their line endings. The repository index uses LF.

## Commits and pull requests

- Format `type: description` (`feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`, `ci`), imperative, about 50
  characters. The body, 3 to 6 lines, says what changes and why. One commit per coherent change.
- A pull request describes the change, lists the tests you ran, and says what you could not check (for example what
  needs a PC with Apple Music). Releases are made by the maintainers.

## Add an API module

The `Ampify` object is made of modules, each in its own files, so several people can add modules without editing the
same lines. For a module `foo`:

1. JavaScript: `src/core/prelude/foo.cpp`, constant `kFoo`. It fills the `Ampify` object and, if it receives
   events, `dispatch.foo`. It sees `native`, the object under construction, `dispatch` and the helpers of
   `src/core/prelude/frame.cpp`.
2. Natives: `src/core/natives/foo.cpp`, with `DefineFoo`, plus `StartFoo` or `StopFoo` if needed. `Require`,
   `HostOf` and `State<T>` are in `src/core/api/bridge.hpp`.
3. Registration: one line in the `kModules` table of `src/core/api/modules.cpp` (the order is the prelude's order).
   Kernel events reach it through `ExtensionLoader::DeliverEvent("foo", payload)`.
4. Build: a `cmake/sources/<package>.cmake` file with `list(APPEND AMPIFY_CORE_SOURCES ...)` and
   `list(APPEND AMPIFY_TESTS_SOURCES ...)`. Always `list(APPEND)`, never `set()`.
5. Host service: a service only some hosts offer is a capability, read with `host.Capability<FooHost>()`. Do not
   add pure virtual methods to `Host`.
6. Permission: add it to `kFixedPermissions` in `src/core/manifest.cpp` and to `schemas/manifest.schema.json`, and
   document it in [docs/extensions/api.md](docs/extensions/api.md), section 4.
7. Tests: `tests/foo_tests.cpp`. `tests/api_module_tests.cpp` freezes the existing members of `Ampify`.
8. Types and docs: section 5 of `docs/extensions/api.md` and `src/cli/sdk_types.cpp` (`ampify types`).
   `tools/test_cli.py` checks that every member of section 5 is in the types.

## Add an element to the catalog

`ampify publish <folder>` checks the element and opens the pull request on
[ampify-catalog](https://github.com/Shirowwww/ampify-catalog) for you. Details:
[docs/extensions/catalog.md](docs/extensions/catalog.md).

## Documentation

Documentation is in English (the French site pages live in `docs/fr/`), in plain words and short sentences. If you
change behavior, update the page that describes it. Relative links between pages must resolve.

## License

Contributions are published under the [GNU GPL v3](LICENSE), with the exception the README describes for themes,
snippets, extensions and apps. Contributions to [examples/](examples) are under the [MIT license](examples/LICENSE).
