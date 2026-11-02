# Building, testing and packaging

## Repository layout

| Folder | Content |
|---|---|
| `src/core/` | Portable C++ for the kernel: settings, manifests, the JavaScript host and API modules (`prelude/`, `natives/`, `api/`), the theme engine (`theme/`), audio math, safe mode, version gate |
| `src/win/` | Windows code: hooks, XAML backend, UI thread, theme service, SMTC, WinHTTP, Discord, tray, MIDI, remote server |
| `src/kernel/`, `src/launcher/`, `src/setup/` | Entry points of `ampify_kernel.dll`, `ampify_launcher.exe` and `ampify_setup.exe` |
| `src/cli/` | `ampify.exe` |
| `themes/`, `themes/snippets/` | Themes and snippets |
| `extensions/`, `apps/` | What ships with Ampify: Now Playing, the Atmos leveler, the playback guard, the Marketplace |
| `examples/` | Example extensions (MIT) |
| `tools/` | The validator (`validate.py`), `pack.py`, the Python and Node tests |
| `schemas/` | JSON schemas of manifests, the catalog and the version list |
| `tests/` | C++ unit tests (`ampify_tests`, and `ampify_ui_thread_test` on Windows) |
| `bench/windows/` | The Windows bench ([testing.md](testing.md)) |
| `scripts/` | Build, install, bench kit, listening test and packaging scripts, the pre-push hook |
| `knowledge/` | What is known about Apple Music's internals |
| `vendor/quickjs/` | quickjs-ng, unchanged |
| `cmake/sources/` | One `.cmake` file per package, listing its sources and tests |

The catalog lives in its own repository, `ampify-catalog`. The tests that check it run when a clone sits next to this
one (or where `AMPIFY_CATALOG` points), and are skipped otherwise.

## Windows build

You need Visual Studio 2022 or later (C++ desktop workload and a Windows SDK) and CMake. Close Apple Music first: a
loaded kernel locks the DLL.

```powershell
pwsh -File scripts\build-msvc.ps1            # release build and tests, into build-msvc\release
pwsh -File scripts\build-msvc.ps1 -Bench     # bench build, into build-msvc\bench
```

The script opens the Visual Studio environment, downloads the WinUI 1.8 metadata from NuGet once (SHA-256 checked),
generates the C++/WinRT projection with the newest SDK that has an x64 `cppwinrt.exe`, then builds with Ninja and
`AMPIFY_XAML=ON`. Later runs only compile what changed.

## Tests

```powershell
build-msvc\release\ampify_tests.exe                  # C++ unit tests
build-msvc\release\ampify_ui_thread_test.exe         # the interface thread, with a real window
npm run test:js                                      # example extensions and the Marketplace page, against a fake Ampify
npm run lint:js                                      # oxlint on the JavaScript (config in .oxlintrc.json)
$env:AMPIFY_BIN = "$PWD\build-msvc\release\ampify.exe"
python -m unittest discover -s tools -p "test_*.py"  # validator, pack.py, the command line end to end
```

The oxlint configuration knows the `Ampify` global and allows browser or Node globals only in `tools/` and
`docs/assets/`. CI runs the linter too.

The portable part (everything but `src/win`, the kernel, the launcher and the installer) also builds on Linux or WSL:

```bash
cmake -S . -B build -G Ninja && cmake --build build --target ampify_tests ampify
./build/ampify_tests
AMPIFY_BIN=$PWD/build/ampify python3 -m unittest discover -s tools -p 'test_*.py'
```

`scripts/hooks/pre-push` runs the validator, packer and example tests before each push. Turn it on once per clone:
`git config core.hooksPath scripts/hooks`.

## CMake options

| Option | Default | Effect |
|---|---|---|
| `AMPIFY_BUILD_TESTS` | `ON` | Builds the unit tests (forced off in a bench build) |
| `AMPIFY_BENCH` | `OFF` | The kernel and launcher accept any Apple Music version. Never distribute this build; `BUILD_INFO.txt` says so |
| `AMPIFY_XAML` | `OFF` | Builds the XAML theme backend and native interface (needs the C++/WinRT projection, `AMPIFY_WINAPPSDK_INCLUDE`) |

The version lives in `CMakeLists.txt` (`project(Ampify VERSION ...)`); the configure step checks the C++ constants
against it.

## Running a build

`ampify launch`, or `build-msvc\bench\ampify_launcher.exe`, starts Apple Music with the kernel. Logs go to
`%USERPROFILE%\Ampify\logs\` (`launcher.log` or `background.log`, `AppleMusic.log`, `AMPLibraryAgent.log`). To lay
out the kit as CI does and run bench cases:

```powershell
pwsh -File scripts\bench-kit.ps1
pwsh -File bench\windows\run-bench.ps1 -KitRoot build-msvc\kit -Only <case> -SkipInstall
```

## Adding a source file or a module

Each package lists its files in `cmake/sources/<package>.cmake` with `list(APPEND ...)`, never `set()`. See
[CONTRIBUTING.md](../../CONTRIBUTING.md#add-an-api-module).

## Packaging and releases

```bash
bash scripts/package.sh --msvc build-msvc/kit
```

It needs `python3` and writes `dist/ampify-<version>-windows-x64.zip` and `dist/SHA256SUMS.txt`, the two files
`ampify update` and `scripts/install.ps1` look for in a GitHub release. `--out <dir>` and `--commit <sha>` are optional.
It takes the release build only and refuses a bench build.

The zip is flat: `ampify.exe`, `ampify_launcher.exe`, `ampify_kernel.dll` and `ampify_setup.exe` at the root. The
Marketplace, Now Playing and the playback guard ship under `examples\` with the example extensions and themes; the
installer and `ampify update --apply` copy them into the user's Ampify folder (`src/core/kit_layout.hpp`). Text files
get CRLF and a BOM for Notepad. Binaries carry a version resource and the logo (`cmake/version.rc.in`).

The manual workflow `package.yml` packages the kit artifacts of a green CI run as a workflow artifact, never as a
release. The install line reads `scripts/install.ps1` from `main`, which installs the release GitHub marks as latest.
The binaries are not code-signed yet.
