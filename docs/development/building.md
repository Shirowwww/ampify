# Building, testing and packaging

## Repository layout

| Folder | Content |
|---|---|
| `core/` | Portable C++ for the kernel: settings, manifests, the JavaScript host and API modules (`core/prelude`, `core/natives`, `core/api`), the theme engine (`core/theme`), audio math, safe mode, version gate, `drm_guard.cpp` |
| `win/` | Windows code: hooks, XAML backend, UI thread, theme service, SMTC, WinHTTP, Discord, tray, MIDI, remote server |
| `kernel/`, `launcher/`, `setup/`, `testkit/` | The entry points of `ampify_kernel.dll`, `ampify_launcher.exe`, `ampify_setup.exe` and `Ampify-Test.exe` |
| `cli/` | `ampify.exe` |
| `tools/` | `ampify_theme_check`, the validator (`validate.py`), `pack.py`, and the Python and Node tests |
| `tests/` | The C++ unit tests (`ampify_tests`) |
| `apps/`, `extensions/`, `examples/`, `themes/`, `catalog/`, `schemas/` | The Marketplace app, the built-in Atmos leveler, example elements, example themes and snippets, the catalog, the JSON schemas |
| `bench/windows/` | The Windows bench (PowerShell) |
| `scripts/` | Build, bench-kit, listening test, packaging, and the all-in-one check |
| `knowledge/`, `reference/` | What was learned about Apple Music, and the reference Atmos mod and loudness meter |
| `vendor/quickjs/` | quickjs-ng, vendored without changes |
| `cmake/sources/` | One `.cmake` file per package, listing its sources and tests |

## Everything that needs no Apple Music (Linux or WSL)

```bash
scripts/check-all.sh
```

It runs, in order: the Linux build and `ampify_tests`, the Python and Node tests, catalog and manifest validation, the
MinGW cross-build, the unit tests under Wine, the Wine hook harness (with a null PulseAudio sink, a fake Atmos stream and
a fake `AVFoundationCF.dll`), and the installer and test kit end-to-end under Wine. It prints one line per step and exits
with 1 if any step fails. Logs go to `build-check/` (override with `AMPIFY_CHECK_DIR`). `--linux-only` skips the Windows
part.

Needs: cmake, ninja, g++, python3, node. For the Windows part: g++-mingw-w64-x86-64, wine and pulseaudio.

Run the pieces by hand:

```bash
cmake -S . -B build && cmake --build build
./build/ampify_tests                                                    # C++ unit tests
AMPIFY_BIN=$PWD/build/ampify python3 -m unittest discover -s tools -v   # validator, pack.py, CLI end-to-end
node --test tools/test_examples.mjs                                     # example extensions against a fake Ampify
```

Under Wine, `AMPIFY_WINE=1 AMPIFY_BIN=build-win/ampify.exe` runs the CLI tests that do not need `tar.exe` or `curl.exe`.

## CMake options

| Option | Default | Effect |
|---|---|---|
| `AMPIFY_BUILD_TESTS` | `ON` | Builds the portable unit tests. Forced off for a bench build, because the tests check the shipping version policy |
| `AMPIFY_BENCH` | `OFF` | Bench build: the kernel and launcher accept any Apple Music version. **Never distribute it.** The configure step prints a warning and `BUILD_INFO.txt` says so |
| `AMPIFY_XAML` | `OFF` | Builds the XAML theme backend and native interface. Needs the Windows App SDK C++/WinRT headers (`AMPIFY_WINAPPSDK_INCLUDE`) |
| `AMPIFY_BUILD_HARNESS` | `OFF` | Builds the Wine hook harness |

The version lives in `CMakeLists.txt` (`project(Ampify VERSION ...)`). The configure step checks that the C++ constants
agree with it.

## Windows build with MSVC (XAML included)

Needs Visual Studio 2022 (the C++ desktop workload and a Windows SDK) and CMake. Ninja comes with Visual Studio.

```powershell
pwsh -File scripts\build-msvc.ps1 -Bench     # bench build into build-msvc\bench
pwsh -File scripts\build-msvc.ps1            # release build into build-msvc\release
```

Close Apple Music first: a loaded kernel locks the DLL and blocks the link.

The script:

1. Opens the Visual Studio environment (`vcvars64.bat`) for the process.
2. Downloads the WinUI 1.8 metadata packages from NuGet once, checks the SHA-256 of `Microsoft.UI.Xaml.winmd`, and
   generates the C++/WinRT projection in `build-msvc\winrt-gen`.
3. Picks the newest Windows SDK that has an x64 `cppwinrt.exe`. An older SDK (17763) ships C++/WinRT 1.0, which fails.
4. Configures with `AMPIFY_XAML=ON` and compiles with `/MP`.

Later runs compile only what changed, in a few seconds to a minute.

To lay out the kit folder as the CI does and run the bench cases:

```powershell
pwsh -File scripts\bench-kit.ps1
pwsh -File bench\windows\run-bench.ps1 -KitRoot build-msvc\kit -Only <case> -SkipInstall
```

`scripts\bench-kit.ps1` builds `build-msvc\kit` with `release\` (launcher, kernel, setup, test kit, `ampify.exe`), `cli\`,
`bench\` (the bench build) and `ui\` (the bench build plus `ampify_theme_check.exe`, the example themes and
`examples\react-panel`). See [bench-and-ci.md](bench-and-ci.md).

Start by hand: `build-msvc\bench\ampify_launcher.exe`. Logs are in `%USERPROFILE%\Ampify\logs\`.

## Adding a source file or a module

Each package lists its files in `cmake/sources/<package>.cmake`, always with `list(APPEND ...)`, never `set()`. See
[CONTRIBUTING.md](../../CONTRIBUTING.md#add-an-api-module).

## Packaging

```bash
bash scripts/package.sh --msvc build-msvc/kit
```

It needs a `python3` command. It builds `dist/ampify-<version>-windows-x64.zip` and `dist/SHA256SUMS.txt`, the two names
`ampify update` looks for in a release. Nothing is published by the script.

- `--msvc <kit>` takes the MSVC kit (the CI artifact `ampify-msvc-kit` or `build-msvc\kit`). Prefer it: only the MSVC build
  has the XAML binding (theme choice, the "Ampify" card in Settings, panels). A MinGW kernel injects, levels Atmos and
  runs extensions, but draws nothing in the interface, so a MinGW package only checks the packaging.
- `--mingw [<dir>]` takes a MinGW build folder (default `build-check/win`) or the `ampify-builds` artifact.
- `--out <dir>` (default `dist`) and `--commit <sha>` (written to `BUILD_INFO.txt`).
- Only the release build is taken. A bench build is refused, and so is a kit whose `BUILD_INFO.txt` does not match the
  compiler named on the command line.
- The script checks that the installer contains the version number, and that each binary is a Windows executable.

The zip is flat, as `ampify update` and `ampify_setup.exe` need: `ampify.exe`, `ampify_launcher.exe` and `ampify_kernel.dll`
at the root, next to `ampify_setup.exe` (the installer copies from its own folder). Example extensions, themes and apps go
under `examples\`, which `ampify update --apply` ignores (it copies only root files). The text files get CRLF line endings
and a BOM so Notepad opens them correctly, and `prepare-test.cmd` gets no BOM.

A manual GitHub workflow packages from a green bench run: it takes the `ampify-msvc-kit` artifact of that run and publishes
the zip as a workflow artifact, never as a release.

## Releases

A release needs two files: `ampify-<version>-windows-x64.zip` and `SHA256SUMS.txt`. The binaries are not code-signed yet,
so Windows SmartScreen asks for confirmation.
