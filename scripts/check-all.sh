#!/usr/bin/env bash
# Every check that runs without Apple Music, in one go: Linux build and tests, Python and Node tests, catalog and
# manifest validation, MinGW cross-build, unit tests under Wine, the Wine hook harness (with a null PulseAudio sink),
# and the installer / test kit end-to-end run under Wine.
#
# Usage: scripts/check-all.sh [--linux-only]
# Needs: cmake, ninja, g++, python3, node; for the Windows part g++-mingw-w64-x86-64, wine, pulseaudio.
# Exit code: 0 when everything passed, 1 otherwise. A summary line per step is printed at the end.
set -uo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
work="${AMK_CHECK_DIR:-$root/build-check}"
mkdir -p "$work"
summary=()
failed=0

step() {  # step "<name>" <command...>
    local name="$1"; shift
    local log="$work/$(echo "$name" | tr ' /' '__').log"
    if "$@" >"$log" 2>&1; then
        summary+=("ok    $name")
    else
        summary+=("FAIL  $name (voir $log)")
        failed=1
    fi
}

cd "$root"
step "linux: build" bash -c "cmake -S . -B '$work/linux' -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build '$work/linux'"
step "linux: amk_tests" "$work/linux/amk_tests"
step "python: tools" env AMK_BIN="$work/linux/amk" python3 -m unittest discover -s tools
step "node: examples" node --test tools/test_examples.mjs
step "catalog: validate" python3 tools/validate.py catalog catalog/catalog.json
step "examples: manifests" bash -c 'for m in examples/*/manifest.json; do python3 tools/validate.py manifest "$m" || exit 1; done'

if [[ "${1:-}" != "--linux-only" ]]; then
    export WINEDEBUG=-all
    export WINEPREFIX="${WINEPREFIX:-$work/wine}"
    step "mingw: build (tests + harness)" bash -c "cmake -S . -B '$work/win' -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release -DAMK_BUILD_HARNESS=ON && cmake --build '$work/win'"
    step "mingw: bench build" bash -c "cmake -S . -B '$work/win-bench' -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release -DAMK_BENCH=ON && cmake --build '$work/win-bench'"
    step "wine: amk_tests" wine "$work/win/amk_tests.exe"
    step "wine: hook harness" bash -c "cd '$work/win' && wine amk_hook_harness.exe"

    # The Glue test opens a real audio stream, so Wine needs a sound server. A null sink is enough. PULSE_SERVER, when
    # set, is used as is. Otherwise a private daemon is started; PulseAudio allows one per user, so a daemon already
    # running without our socket is stopped first (fine in a container, set PULSE_SERVER on a desktop).
    pulse_server="${PULSE_SERVER:-}"
    if [[ -z "$pulse_server" ]]; then
        pulse="$work/pulse"
        mkdir -p "$pulse"
        if [[ ! -S "$pulse/native" ]] && command -v pulseaudio >/dev/null; then
            pulseaudio -k >/dev/null 2>&1 && sleep 1
            (cd "$pulse" && pulseaudio --daemonize=yes --exit-idle-time=-1 -n --load="module-null-sink sink_name=nul" \
                --load="module-native-protocol-unix socket=$pulse/native auth-anonymous=1" >/dev/null 2>&1)
            sleep 2
        fi
        pulse_server="unix:$pulse/native"
    fi
    step "wine: hook harness Glue" bash -c "cd '$work/win' && PULSE_SERVER='$pulse_server' wine amk_hook_harness.exe Glue"
    step "wine: installer and test kit" bash scripts/wine-e2e.sh "$work/win" "$WINEPREFIX"
fi

printf '%s\n' "${summary[@]}"
exit "$failed"
