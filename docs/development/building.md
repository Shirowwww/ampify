# Building, testing and packaging

## Repository layout

| Folder | Content |
|---|---|
| `src/core/` | Portable C++ for the kernel: settings, manifests, the JavaScript host and API modules (`prelude/`, `natives/`, `api/`), the theme engine (`theme/`), audio math, safe mode, version gate, `drm_guard.cpp` |
| `src/win/` | Windows code: hooks, XAML backend, UI thread, theme service, SMTC, WinHTTP, Discord, tray, MIDI, remote server, the background launcher's registration |
| `src/kernel/`, `src/launcher/`, `src/setup/` | The entry points of `ampify_kernel.dll`, `ampify_launcher.exe` and `ampify_setup.exe` |
| `src/cli/` | `ampify.exe` |
| `src/resources/` | `ampify.ico`, the logo every binary carries |
| `themes/` | The themes and palettes of the catalog; `themes/snippets/` for snippets |
| `extensions/`, `apps/` | What ships with Ampify: Now Playing, the built-in Atmos leveler, the Marketplace app |
| `examples/` | Example extensions, for authors |
| `tools/` | The validator (`validate.py`), `pack.py`, and the Python and Node tests |
| `schemas/` | The JSON schemas of manifests, the catalog and the version list |
| `tests/` | The C++ unit tests (`ampify_tests`, and `ampify_ui_thread_test` on Windows) |
| `bench/windows/` | The Windows bench (PowerShell) |
| `scripts/` | Build, install, bench kit, listening test and packaging scripts, and the pre-push hook |
| `knowledge/` | What was learned about Apple Music's internals |
| `vendor/quickjs/` | quickjs-ng, vendored without changes |
| `cmake/sources/` | One `.cmake` file per package, listing its sources and tests |

The catalog the Marketplace reads is in its own repository, `ampify-catalog`. The tests that check it run when a clone
of it sits next to this one (or where `AMPIFY_CATALOG` points), and are skipped otherwise.

## Windows build with MSVC

Needs Visual Studio 2022 or later (the C++ desktop workload and a Windows SDK) and CMake. Ninja comes with Visual
Studio.

```powershell
pwsh -File scripts\build-msvc.ps1            # release build and tests into build-msvc\release
pwsh -File scripts\build-msvc.ps1 -Bench     # bench build into build-msvc\bench
```

Close Apple Music first: a loaded kernel locks the DLL and blocks the link.

The script:

1. Opens the Visual Studio environment (`vcvars64.bat`) for the process.
2. Downloads the WinUI 1.8 metadata packages from NuGet once, checks the SHA-256 of `Microsoft.UI.Xaml.winmd`, and
   generates the C++/WinRT projection in `build-msvc\winrt-gen`.
3. Picks the newest Windows SDK that has an x64 `cppwinrt.exe`. An older SDK (17763) ships C++/WinRT 1.0, which fails.
4. Configures with `AMPIFY_XAML=ON` and builds with Ninja.

Later runs compile only what changed, in a few seconds to a minute.

## Tests

```powershell
build-msvc\release\ampify_tests.exe                  # C++ unit tests
build-msvc\release\ampify_ui_thread_test.exe         # the interface thread hook, with a real window
node --test tools/test_examples.mjs                  # example extensions and the Marketplace page, against a fake Ampify
$env:AMPIFY_BIN = "$PWD\build-msvc\release\ampify.exe"
python -m unittest discover -s tools -p "test_*.py"  # validator, pack.py, the command line end to end
```

The portable part (everything but `src/win`, the kernel, the launcher and the installer) also builds and tests on
Linux or WSL, as the CI does:

```bash
cmake -S . -B build -G Ninja && cmake --build build --target ampify_tests ampify
./build/ampify_tests
AMPIFY_BIN=$PWD/build/ampify python3 -m unittest discover -s tools -p 'test_*.py'
```

`scripts/hooks/pre-push` runs the validator, packer and example tests in a few seconds before each push. Turn it on
once per clone: `git config core.hooksPath scripts/hooks`.

## CMake options

| Option | Default | Effect |
|---|---|---|
| `AMPIFY_BUILD_TESTS` | `ON` | Builds the unit tests. Forced off for a bench build, because the tests check the shipping version policy |
| `AMPIFY_BENCH` | `OFF` | Bench build: the kernel and launcher accept any Apple Music version. **Never distribute it.** The configure step prints a warning and `BUILD_INFO.txt` says so |
| `AMPIFY_XAML` | `OFF` | Builds the XAML theme backend and native interface. Needs the C++/WinRT projection of WinUI (`AMPIFY_WINAPPSDK_INCLUDE`) |

The version lives in `CMakeLists.txt` (`project(Ampify VERSION ...)`). The configure step checks that the C++ constants
agree with it.

## Running a build

`ampify launch`, or `build-msvc\bench\ampify_launcher.exe`, starts Apple Music with the kernel in the foreground; the
launcher stays open while the app runs. `ampify_launcher.exe --background` is what the installer starts at sign-in:
it waits for Apple Music and loads the kernel each time the app opens (`src/win/background.hpp`). Logs are in
`%USERPROFILE%\Ampify\logs\` (`launcher.log` or `background.log`, then `AppleMusic.log` and `AMPLibraryAgent.log`).

To lay out the kit folder as the CI does and run the bench cases:

```powershell
pwsh -File scripts\bench-kit.ps1
pwsh -File bench\windows\run-bench.ps1 -KitRoot build-msvc\kit -Only <case> -SkipInstall
```

`scripts\bench-kit.ps1` builds `build-msvc\kit` with `release\` (launcher, kernel, setup, `ampify.exe`), `cli\`,
`bench\` (the bench build) and `ui\` (the bench build plus `ampify_theme_check.exe`, the themes and
`examples\react-panel`). See [bench-and-ci.md](bench-and-ci.md).

### Reading a crash

Each MSVC binary gets its `.pdb` beside it (never packaged). When Windows logs a crash of Apple Music in Ampify's
kernel (Event Viewer, Windows Logs > Application, source "Application Error"), note the fault offset and give it to the
debugger of the Windows SDK, with the same build still in place:

```powershell
& "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\cdb.exe" -z build-msvc\bench\ampify_kernel.dll `
    -y build-msvc\bench -c "ln ampify_kernel+0x13a1d; q"
```

The answer is the function that held the instruction. A later build moves the code, so keep the build that crashed.

## Adding a source file or a module

Each package lists its files in `cmake/sources/<package>.cmake`, always with `list(APPEND ...)`, never `set()`. See
[CONTRIBUTING.md](../../CONTRIBUTING.md#add-an-api-module).

## Packaging

```bash
bash scripts/package.sh --msvc build-msvc/kit
```

It needs a `python3` command. It builds `dist/ampify-<version>-windows-x64.zip` and `dist/SHA256SUMS.txt`, the two
names `ampify update` and `scripts/install.ps1` look for in a release. Nothing is published by the script.

- `--msvc <dir>` takes the MSVC kit (the CI artifacts `ampify-msvc-kit-*`, or `build-msvc\kit`) or an MSVC build folder.
- `--out <dir>` (default `dist`) and `--commit <sha>` (written to `BUILD_INFO.txt`).
- Only the release build is taken. A bench build is refused, and so is a kit whose `BUILD_INFO.txt` does not mention
  MSVC.
- The script checks that the installer contains the version number, and that each binary is a Windows executable.

The zip is flat, as `ampify update` and `ampify_setup.exe` need: `ampify.exe`, `ampify_launcher.exe` and
`ampify_kernel.dll` at the root, next to `ampify_setup.exe` (the installer copies from its own folder). The
Marketplace and Now Playing ship with the kit: they go under `examples\` with the example extensions and the themes,
and the installer and `ampify update --apply` put them in the user's Ampify folder, in place of any older copy
(`src/core/kit_layout.hpp`). The text files get CRLF line endings and a BOM so Notepad opens them correctly.

The binaries carry a version resource (Properties > Details: product, publisher, description, version) and the logo,
from `cmake/version.rc.in`.

A manual GitHub workflow (`package.yml`) packages from a green CI run: it takes the `ampify-msvc-kit-*` artifacts of
that run and publishes the zip as a workflow artifact, never as a release.

## Releases

A release needs two files: `ampify-<version>-windows-x64.zip` and `SHA256SUMS.txt`. The install line of the README
reads `scripts/install.ps1` from `main`, and the script installs the release GitHub marks as latest. The binaries are
not code-signed yet, so Windows SmartScreen asks for confirmation.
