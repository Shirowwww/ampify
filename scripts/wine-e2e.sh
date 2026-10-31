#!/usr/bin/env bash
# End-to-end check of the installer, the test kit and the uninstaller under Wine.
# Apple Music is not in a Wine prefix, so the launch step is reported as "not found": the rest is real.
# Usage: scripts/wine-e2e.sh <build-win directory> [wine prefix directory]
set -uo pipefail

build="${1:?usage: wine-e2e.sh <build-win directory> [wine prefix]}"
export WINEPREFIX="${2:-${WINEPREFIX:-$HOME/.wine-ampify}}"
export WINEDEBUG=-all
user_dir="$(ls -d "$WINEPREFIX"/drive_c/users/*/ | grep -v '/Public/' | head -1)"
kit_dir="$user_dir/AppData/Local/Programs/Ampify"
data_dir="$user_dir/Ampify"
shortcut="$user_dir/AppData/Roaming/Microsoft/Windows/Start Menu/Programs/Apple Music with Ampify.lnk"
failures=0

check() {  # check "<description>" <command...>
    local description="$1"; shift
    if "$@" >/dev/null 2>&1; then echo "  ok   $description"; else echo "  FAIL $description"; failures=$((failures + 1)); fi
}
absent() { check "$1" test ! -e "$2"; }

cd "$build"
rm -rf "$kit_dir" "$data_dir" "$shortcut"

echo "1. Silent install"
code=0; wine ampify_setup.exe /silent || code=$?
check "exit code 0 (got $code)" test "$code" -eq 0
check "ampify_launcher.exe installed" test -f "$kit_dir/ampify_launcher.exe"
check "ampify_kernel.dll installed" test -f "$kit_dir/ampify_kernel.dll"
check "ampify_setup.exe (uninstaller) installed" test -f "$kit_dir/ampify_setup.exe"
check "Start menu shortcut created" test -f "$shortcut"
check "install log written" grep -q "Install finished" "$data_dir/logs/install.log"

echo "2. Test kit, automatic mode (no ETW, no Apple Music)"
code=0; wine Ampify-Test.exe /auto /wait:2 /noetl || code=$?
check "exit code 4, not run (got $code)" test "$code" -eq 4
run_dir="$(cat "$data_dir/reports/latest.txt" | tr -d '\r\357\273\277' | sed 's/\\/\//g')"
run_dir="$WINEPREFIX/drive_c/${run_dir#C:/}"
check "report.txt written" test -f "$run_dir/report.txt"
check "report: result NOT RUN" grep -q "NOT RUN" "$run_dir/report.txt"
check "report: Ampify installed [OK]" grep -q "\[OK\] *Ampify installed" "$run_dir/report.txt"
check "steps: install, then launch skipped" grep -q "Launch skipped" "$run_dir/steps.txt"

echo "3. Uninstall keeping the data"
code=0; wine "$kit_dir/ampify_setup.exe" /uninstall /silent /keepdata || code=$?
sleep 3
check "exit code 0 (got $code)" test "$code" -eq 0
absent "install folder deleted" "$kit_dir"
absent "shortcut deleted" "$shortcut"
check "data kept (reports)" test -d "$data_dir/reports"
check "uninstall log written" grep -q "data kept" "$data_dir/logs/uninstall.log"

echo "4. Reinstall, then uninstall deleting everything"
wine ampify_setup.exe /silent
code=0; wine "$kit_dir/ampify_setup.exe" /uninstall /silent /purgedata || code=$?
sleep 3
check "exit code 0 (got $code)" test "$code" -eq 0
absent "install folder deleted" "$kit_dir"
absent "data folder deleted" "$data_dir"
absent "shortcut deleted" "$shortcut"

echo
if [ "$failures" -eq 0 ]; then echo "Wine E2E: all passed"; else echo "Wine E2E: $failures failure(s)"; fi
exit "$failures"
