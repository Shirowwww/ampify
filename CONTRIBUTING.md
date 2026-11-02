# Contributing to Ampify

Thanks for helping. This page lists the rules of the project, why they exist, and how to make a change that can be merged.

## Rules that do not change

These rules come first. A change that breaks one will not be merged.

1. **Never hook FairPlay, CoreFP, CoreADI64 or anything related to DRM.** Ampify changes how Apple Music looks and adds
   features. It does not circumvent copy protection. Every hook goes through `src/core/drm_guard.cpp`, which refuses any target
   in those components. Do not add a way around it, and do not add features that sit next to the protected stream path (for
   example, choosing protected stream variants).
2. **The volume bar never moves.** Apple Music's slider shows the player volume. Ampify reads the volume and never writes it
   (never `AVCFPlayerSetVolume`). A gain is applied to the decoded samples instead, so the slider stays where the user put
   it.
3. **An unknown Apple Music version means the kernel stays inactive.** The kernel loads only on versions in the tested list
   (`src/core/version_gate.hpp`, plus `versions.txt`), and only if the expected exports of `AVFoundationCF.dll` exist. Resolve
   functions by name, never by a fixed address. An Apple Music update then cannot break anything: Ampify stays quiet until
   the new version is tested. The bench build (`AMPIFY_BENCH=ON`) accepts any version and must never be distributed.
4. **Nothing is written to Apple Music's package.** All data goes to `%USERPROFILE%\Ampify`. Removing Ampify must leave
   Apple Music as it was.
5. **Community code stays in the sandbox.** Extensions are JavaScript with declared permissions. There are no native
   community extensions, no raw file access, and no network beyond declared hosts. Services that reach outside the process
   (Discord, tray, remote, MIDI) stay off until the user switches them on.
6. **Nothing downloaded runs during an install.** Installs check HTTPS, SHA-256, the file list and the manifest before
   anything is put in place. An installed element starts off.
7. **No personal data and no secrets in the repository.** Logs and reports that leave a PC have the user name replaced.

## Set up

See [docs/development/building.md](docs/development/building.md). The short version:

```powershell
pwsh -File scripts\build-msvc.ps1        # Windows, MSVC, XAML included, into build-msvc\release
git config core.hooksPath scripts/hooks  # fast checks before each push
```

## Tests come before the merge

- Add or update tests with the change. The portable C++ tests are in `tests/`, the CLI and validator tests in `tools/`, and
  the example extensions are tested in `tools/test_examples.mjs` against a fake `Ampify` object.
- Run `ampify_tests`, `ampify_ui_thread_test` and the Python and Node tests ([building.md](docs/development/building.md#tests))
  and make sure everything passes. CI runs them again on Linux and Windows, with the bench.
- When a test fails, fix the implementation. Change the test only if the test is wrong.
- Anything that touches Apple Music itself (injection, XAML, audio) cannot be tested by unit tests. Run the Windows bench
  ([docs/development/bench-and-ci.md](docs/development/bench-and-ci.md)) or check it on a PC
  ([docs/development/testing-on-a-pc.md](docs/development/testing-on-a-pc.md)), and look at a screenshot. A log line that
  says "applied" is not proof that something is visible.
- A bug fix comes with a test that fails before the fix.

## Code style

- Comments explain why, not what. Keep them short, and write them in English.
- Functions under about 50 lines, files under about 800, nesting at most four levels deep.
- Do not mutate shared state when you can return a new value. Handle errors explicitly, and never swallow them.
- Validate input at the edges: manifests, archives, paths, URLs, settings.
- Name constants. Leave no debug output behind.
- In a process you do not control, hold every XAML object you create with a strong reference and release it on the UI
  thread. Read [docs/development/xaml-lessons.md](docs/development/xaml-lessons.md) before changing `src/win/`.
- Windows text files keep their line endings. The repository index uses LF.

## Commits and pull requests

- Format: `type: description`, with a type from `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`, `ci`.
- The title is in the imperative mood, about 50 characters. The body, 3 to 6 lines, says what changes and why.
- One commit per coherent change.
- A pull request describes the change, lists the tests you ran, and says what you could not check (for example, what needs a
  PC with Apple Music).
- Releases are made by the maintainers.

## Add an API module

The `Ampify` object is made of modules. Each lives in its own files, so several people can add modules without editing the
same lines. For a module `foo`:

1. **JavaScript:** `src/core/prelude/foo.cpp`, with the constant `kFoo`. It fills the `Ampify` object and, if it receives
   events, `dispatch.foo`.
2. **Natives:** `src/core/natives/foo.cpp`, with `DefineFoo`, plus `StartFoo` or `StopFoo` if the module must load or give
   something back. Use `Require` for the permission check.
3. **Registration:** one line in the `kModules` table of `src/core/api/modules.cpp`, and the declaration of `kFoo` and `DefineFoo`
   just above it. The order of the table is the order of the prelude.
4. **Build:** list the files in a `cmake/sources/<package>.cmake` file, with `list(APPEND AMPIFY_CORE_SOURCES ...)`. Add the
   tests with `list(APPEND AMPIFY_TESTS_SOURCES ...)`. Always `list(APPEND)`, never `set()`. The convention is described at
   the top of `CMakeLists.txt`.
5. **Host service:** a service that only some hosts offer is a capability, read with `host.Capability<FooHost>()`. Do not
   add pure virtual methods to `Host`.
6. **Permission:** add it to `kFixedPermissions` in `src/core/manifest.cpp` and to `schemas/manifest.schema.json` (keep both in
   step), and document it in [docs/extensions/api.md](docs/extensions/api.md), section 4.
7. **Tests:** `tests/foo_tests.cpp`. `tests/api_module_tests.cpp` freezes the existing members of the `Ampify` object, so a
   new member leaves it alone and a change to an existing member makes it fail.
8. **Types and docs:** add the members to section 5 of `docs/extensions/api.md` and to `src/cli/sdk_types.cpp` (`ampify types`).
   `tools/test_cli.py` checks that every member in section 5 appears in the types.

## Add an element to the catalog

The Marketplace lists what is in `catalog.json` of [ampify-catalog](https://github.com/Shirowwww/ampify-catalog). See
[docs/extensions/catalog.md](docs/extensions/catalog.md): check the manifest, pack the archive with `tools/pack.py`, and
open a pull request on that repository. A maintainer reviews the code before the merge.

## Documentation

Documentation is in English, in plain words, with short sentences. Keep it next to the change: if you change behavior, update
the page that describes it. Relative links between pages must resolve.
