# Building and packaging

## Layout

| Folder | Content |
|---|---|
| `src/core/` | Portable C++: settings, manifests, the JavaScript host and API modules (`prelude/`, `natives/`, `api/`), the theme engine (`theme/`, with the built-in layouts in `theme/layouts/*.css`), audio math, safe mode, version gate |
| `src/win/` | Windows code: hooks, XAML backend, UI thread, theme service, SMTC, WinHTTP, Discord, tray, MIDI, remote server |
| `src/kernel/`, `src/launcher/`, `src/setup/` | `ampify_kernel.dll`, `ampify_launcher.exe`, `ampify_setup.exe` (the installer window is `setup_window*`) |
| `src/cli/` | `ampify.exe` (terminal colours in `console_style*`) |
| `themes/`, `extensions/`, `apps/`, `examples/` | Shipped themes, elements and the Marketplace; example extensions. `extensions/atmos-leveler/` is built into the kernel |
| `tools/` | `validate.py`, `pack.py`, the Python and Node tests |
| `schemas/`, `facts/` | JSON schemas; the two fact files described below |
| `tests/` | C++ unit tests (`ampify_tests`); `tests/win/` holds the Windows-only ones |
| `bench/windows/`, `scripts/` | The Windows bench ([testing.md](testing.md)); build, install, packaging and listening-test scripts, `hooks/pre-push` |
| `cmake/` | `sources/<package>.cmake` lists each package's files; `layouts.cmake`; templates for the version resource and facts headers |
| `knowledge/`, `vendor/` | What is known about Apple Music's internals; quickjs-ng and Signalsmith Stretch, unchanged |

The catalog lives in the `ampify-catalog` repository. Its tests run when a clone sits next to this one (or where
`AMPIFY_CATALOG` points) and are skipped otherwise.

## Windows build

You need Visual Studio 2022 or later (C++ desktop workload, Windows SDK) and CMake. Close Apple Music first, because a
loaded kernel locks the DLL.

```powershell
pwsh -File scripts\build-msvc.ps1                        # release build into build-msvc\release
pwsh -File scripts\build-msvc.ps1 -Bench                 # bench build into build-msvc\bench
pwsh -File scripts\build-msvc.ps1 -Target ampify_tests   # one target only
```

The script opens the Visual Studio environment, downloads the WinUI 1.8 metadata from NuGet once (SHA-256 checked,
`scripts/msvc-winui.ps1`), generates the C++/WinRT projection with the newest SDK that has an x64 `cppwinrt.exe`, then
configures with Ninja and `AMPIFY_XAML=ON` and builds. Later runs compile only what changed. It builds the tests but
does not run them.

Targets: `ampify_kernel`, `ampify_launcher`, `ampify_setup`, `ampify`, `ampify_tests`.

## CMake options

| Option | Default | Effect |
|---|---|---|
| `AMPIFY_BUILD_TESTS` | `ON` | Builds `ampify_tests` and registers `ctest` entries `ampify_tests` and `ampify_tools` |
| `AMPIFY_BENCH` | `OFF` | The kernel and launcher accept any Apple Music version. Never distribute this build; `BUILD_INFO.txt` says so |
| `AMPIFY_XAML` | `OFF` | Builds the XAML theme backend and native interface. Needs the C++/WinRT projection in `AMPIFY_WINAPPSDK_INCLUDE` |

With MSVC and no compiler launcher, the XAML files use a precompiled header (`src/win/xaml_pch.hpp`). CI uses ccache,
which cannot cache files built on a precompiled header, so it turns the header off.

## Facts files

Two data files define a kit. `facts/release.json` changes with a release or a rename: version, extension API version,
kit folder, binary names, the elements that ship, languages, project addresses. `facts/apple.json` changes when Apple
ships a build: package, process names, tested builds, the exports the kit needs, and `protectedModules`, which no hook
may touch (read by `src/core/drm_guard.cpp` and the bench). Configure turns them into `release_facts.hpp` and
`apple_facts.hpp` in the build folder. `scripts/install.ps1` runs without a checkout, so it keeps copies that
`tools/test_facts.py` checks.

## Tests

```powershell
build-msvc\release\ampify_tests.exe                  # C++ unit tests
npm run test:js                                      # example extensions and the Marketplace page, against a fake Ampify
npm run lint:js                                      # oxlint (.oxlintrc.json)
$env:AMPIFY_BIN = "$PWD\build-msvc\release\ampify.exe"
python -m unittest discover -s tools -p "test_*.py"  # validator, pack.py, facts, the command line end to end
```

Everything except `src/win`, the kernel, the launcher and the installer also builds on Linux or WSL:

```bash
cmake -S . -B build -G Ninja && cmake --build build --target ampify_tests ampify
./build/ampify_tests
AMPIFY_BIN=$PWD/build/ampify python3 -m unittest discover -s tools -p 'test_*.py'
```

`scripts/hooks/pre-push` runs the validator, packer and example tests. Turn it on once per clone with
`git config core.hooksPath scripts/hooks`.

## Running a build

`ampify launch` or `build-msvc\bench\ampify_launcher.exe` starts Apple Music with the kernel. Logs go to
`%USERPROFILE%\Ampify\logs\`: `launcher.log` or `background.log`, `attach.log`, `AppleMusic.log`,
`AMPLibraryAgent.log`. To lay out the kit as CI does and run bench cases:

```powershell
pwsh -File scripts\bench-kit.ps1
pwsh -File bench\windows\run-bench.ps1 -KitRoot build-msvc\kit -Only <case> -SkipInstall
```

## Adding a source file

List it in `cmake/sources/<package>.cmake` with `list(APPEND ...)`, never `set()`. A new API module is described in
[CONTRIBUTING.md](../../CONTRIBUTING.md#add-an-api-module). A new built-in layout is a `.css` file in
`src/core/theme/layouts/` plus its name in `AMPIFY_LAYOUT_NAMES` in `cmake/layouts.cmake`, which turns each file into a
C++ constant served as `@import "ampify:<name>"`. A layout file stays under 16000 bytes, the limit of an MSVC string
literal.

## Packaging

```bash
bash scripts/package.sh --msvc build-msvc/kit
```

Needs `python3`. Writes `dist/ampify-<version>-windows-x64.zip` and `dist/SHA256SUMS.txt`, the two files
`ampify update` and `scripts/install.ps1` look for in a GitHub release. Optional: `--out <dir>`, `--commit <sha>`.
It refuses a bench build.

The zip is flat: `ampify.exe`, `ampify_launcher.exe`, `ampify_kernel.dll` and `ampify_setup.exe` at the root. The
shipped elements, example extensions and themes go under `examples\`; the installer and `ampify update --apply` copy
them into the user's Ampify folder (`src/core/kit_layout.hpp`). Text files get CRLF and a BOM for Notepad. Binaries
carry a version resource and the logo (`cmake/version.rc.in`).

The manual workflow `package.yml` packages the kit artifacts of a green CI run as a workflow artifact, never as a
release. The install line reads `scripts/install.ps1` from `main` and installs the release GitHub marks as latest. The
binaries are not code-signed.
