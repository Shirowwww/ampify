#!/usr/bin/env bash
# End-to-end check of the installer, the test kit and the uninstaller under Wine.
# Apple Music is not in a Wine prefix, so the launch step is reported as "introuvable": the rest is real.
# Usage: scripts/wine-e2e.sh <build-win directory> [wine prefix directory]
set -uo pipefail

build="${1:?usage: wine-e2e.sh <build-win directory> [wine prefix]}"
export WINEPREFIX="${2:-${WINEPREFIX:-$HOME/.wine-amk}}"
export WINEDEBUG=-all
user_dir="$(ls -d "$WINEPREFIX"/drive_c/users/*/ | grep -v '/Public/' | head -1)"
kit_dir="$user_dir/AppData/Local/Programs/AppleMusicModKit"
data_dir="$user_dir/AppleMusicModKit"
shortcut="$user_dir/AppData/Roaming/Microsoft/Windows/Start Menu/Programs/Apple Music avec mods.lnk"
failures=0

check() {  # check "<description>" <command...>
    local description="$1"; shift
    if "$@" >/dev/null 2>&1; then echo "  ok   $description"; else echo "  FAIL $description"; failures=$((failures + 1)); fi
}
absent() { check "$1" test ! -e "$2"; }

cd "$build"
rm -rf "$kit_dir" "$data_dir" "$shortcut"

echo "1. Installation silencieuse"
code=0; wine amk_setup.exe /silent || code=$?
check "code de sortie 0 (got $code)" test "$code" -eq 0
check "amk_launcher.exe installé" test -f "$kit_dir/amk_launcher.exe"
check "amk_kernel.dll installé" test -f "$kit_dir/amk_kernel.dll"
check "amk_setup.exe (désinstallateur) installé" test -f "$kit_dir/amk_setup.exe"
check "raccourci du menu Démarrer créé" test -f "$shortcut"
check "journal d'installation écrit" grep -q "Installation terminée" "$data_dir/journal/installation.log"

echo "2. Kit de test, mode automatique (sans ETW, sans Apple Music)"
code=0; wine AMK-Test.exe /auto /attente:2 /sansetl || code=$?
check "code de sortie 4, non exécuté (got $code)" test "$code" -eq 4
run_dir="$(cat "$data_dir/rapports/dernier.txt" | tr -d '\r\357\273\277' | sed 's/\\/\//g')"
run_dir="$WINEPREFIX/drive_c/${run_dir#C:/}"
check "rapport.txt écrit" test -f "$run_dir/rapport.txt"
check "rapport : résultat NON EXÉCUTÉ" grep -q "NON EXÉCUTÉ" "$run_dir/rapport.txt"
check "rapport : kit installé [OK]" grep -q "\[OK\] *Kit installé" "$run_dir/rapport.txt"
check "déroulement : installation puis lancement ignoré" grep -q "Lancement ignoré" "$run_dir/deroulement.txt"

echo "3. Désinstallation en gardant les données"
code=0; wine "$kit_dir/amk_setup.exe" /uninstall /silent /keepdata || code=$?
sleep 3
check "code de sortie 0 (got $code)" test "$code" -eq 0
absent "dossier d'installation supprimé" "$kit_dir"
absent "raccourci supprimé" "$shortcut"
check "données gardées (rapports)" test -d "$data_dir/rapports"
check "journal de désinstallation écrit" grep -q "données gardées" "$data_dir/journal/desinstallation.log"

echo "4. Réinstallation puis désinstallation en supprimant tout"
wine amk_setup.exe /silent
code=0; wine "$kit_dir/amk_setup.exe" /uninstall /silent /purgedata || code=$?
sleep 3
check "code de sortie 0 (got $code)" test "$code" -eq 0
absent "dossier d'installation supprimé" "$kit_dir"
absent "dossier de données supprimé" "$data_dir"
absent "raccourci supprimé" "$shortcut"

echo
if [ "$failures" -eq 0 ]; then echo "E2E Wine : tout passe"; else echo "E2E Wine : $failures échec(s)"; fi
exit "$failures"
