#!/usr/bin/env bash
# Builds the V0.5 test package: dist/amk-kit-<version>-windows-x64.zip and dist/SHA256SUMS.txt, the two names
# `amk update` looks for in a release (cli/marketplace.cpp). Nothing is published: the zip is handed over by hand,
# or uploaded as a workflow artifact by .github/workflows/package-v05.yml.
#
# Usage:
#   scripts/package-v05.sh --msvc <amk-msvc-kit>     the MSVC kit artifact of windows-bench.yml (preferred)
#   scripts/package-v05.sh --mingw [<dir>]           a MinGW build: a build folder (default build-check/win, as
#                                                   scripts/check-all.sh makes it) or the amk-builds artifact
#   options: --out <dir> (default dist), --commit <sha> (written to BUILD_INFO.txt)
#
# Prefer the MSVC kit. Only it has the XAML binding: the theme choice in Réglages > Général, the Mods card in
# Réglages > Lecture and the extension panels. A MinGW kernel injects, levels Atmos and runs the extensions, but
# draws nothing in the interface, so a MinGW package is only good for checking the packaging itself.
#
# Only the diffusion build is taken (tested Apple Music versions only). A bench build (AMK_BENCH=ON) is refused.
#
# The zip is flat, as `amk update` and amk_setup.exe both need: amk.exe, amk_launcher.exe and amk_kernel.dll at the
# root, next to amk_setup.exe (the installer copies the launcher and the kernel from its own folder). The example
# extensions and themes go under exemples/, which `amk update --apply` ignores (it only copies root files).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/dist"
source_kind=""
source_dir=""
commit=""

die() { echo "package-v05 : $*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --msvc) source_kind=msvc; source_dir="${2:-}"; [[ -n "$source_dir" ]] || die "--msvc attend un dossier"; shift 2 ;;
        --mingw)
            source_kind=mingw
            if [[ -n "${2:-}" && "${2:0:2}" != "--" ]]; then source_dir="$2"; shift 2; else source_dir="$root/build-check/win"; shift; fi ;;
        --out) out="${2:-}"; [[ -n "$out" ]] || die "--out attend un dossier"; shift 2 ;;
        --commit) commit="${2:-}"; shift 2 ;;
        -h|--help) sed -n '2,21p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "option inconnue : $1 (voir --help)" ;;
    esac
done
[[ -n "$source_kind" ]] || die "dites d'où viennent les binaires : --msvc <amk-msvc-kit> ou --mingw [<dossier>]"
[[ -d "$source_dir" ]] || die "dossier introuvable : $source_dir"
source_dir="$(cd "$source_dir" && pwd)"
command -v python3 >/dev/null || die "python3 est nécessaire (zip et empreintes)"

# The kit version, from CMakeLists.txt (the configure step checks that the C++ constants agree with it).
version="$(sed -n 's/^project(AppleMusicModKit VERSION \([0-9][0-9.]*\) .*/\1/p' "$root/CMakeLists.txt")"
[[ "$version" == 0.5.* ]] || die "CMakeLists.txt porte la version « $version », ce script prépare une 0.5.x"
name="amk-kit-$version-windows-x64"

# Where each binary is, for the two layouts: a kit (diffusion/ and cli/, the CI artifacts) or a build folder.
if [[ -f "$source_dir/diffusion/amk_setup.exe" ]]; then
    layout=kit
    bin_dir="$source_dir/diffusion"
    cli_dir="$source_dir/cli"
    info="$source_dir/BUILD_INFO.txt"
    if [[ -f "$info" ]]; then
        [[ -z "$commit" ]] && commit="$(sed -n 's/^Commit : //p' "$info" | head -n 1)"
        if [[ "$source_kind" == msvc ]] && ! grep -q 'MSVC' "$info"; then
            die "$info ne parle pas de MSVC : ce n'est pas le kit amk-msvc-kit (utilisez --mingw pour amk-builds)"
        fi
        if [[ "$source_kind" == mingw ]] && grep -q 'MSVC' "$info"; then
            die "$info décrit un kit MSVC : utilisez --msvc"
        fi
    fi
else
    layout=build
    bin_dir="$source_dir"
    cli_dir="$source_dir"
    cache="$source_dir/CMakeCache.txt"
    if [[ -f "$cache" ]]; then
        grep -q '^AMK_BENCH:BOOL=ON' "$cache" && die "$source_dir est un build de banc (AMK_BENCH=ON) : il ne se diffuse jamais"
        if [[ "$source_kind" == mingw ]] && ! grep -q '^CMAKE_[A-Z_]*:FILEPATH=.*mingw' "$cache"; then
            die "$source_dir n'est pas un build MinGW (aucun outil mingw dans CMakeCache.txt)"
        fi
        if [[ "$source_kind" == msvc ]] && grep -q '^CMAKE_[A-Z_]*:FILEPATH=.*mingw' "$cache"; then
            die "$source_dir est un build MinGW : utilisez --mingw"
        fi
    else
        echo "package-v05 : attention, pas de CMakeCache.txt dans $source_dir : impossible de vérifier que ce n'est pas un build de banc" >&2
    fi
fi
if [[ -z "$commit" ]]; then
    # A local build: the binaries come from the working tree, which may hold changes not yet committed.
    commit="$(git -C "$root" rev-parse HEAD 2>/dev/null || echo inconnu)"
    git -C "$root" diff --quiet HEAD 2>/dev/null || commit+=" (avec des modifications non commitées)"
fi

binaries=(amk_setup.exe amk_launcher.exe amk_kernel.dll AMK-Test.exe)
for file in "${binaries[@]}"; do [[ -f "$bin_dir/$file" ]] || die "fichier manquant : $bin_dir/$file"; done
[[ -f "$cli_dir/amk.exe" ]] || die "fichier manquant : $cli_dir/amk.exe"

# Binaries left over from another version would install under the wrong number. The installer writes the kit
# version into the uninstall entry, so its text is in amk_setup.exe (elsewhere the compiler may fold it into code).
grep -aq "$version" "$bin_dir/amk_setup.exe" ||
    die "amk_setup.exe ne contient pas la version $version : binaires d'une autre version (run plus ancien) ?"
# A Windows PE file starts with MZ. Anything else is a Linux binary or a broken download.
for file in "${binaries[@]/#/$bin_dir/}" "$cli_dir/amk.exe"; do
    [[ "$(head -c 2 "$file")" == "MZ" ]] || die "$(basename "$file") n'est pas un exécutable Windows"
done

# The examples the guide uses: extensions with something to see or hear, and the four example themes.
extensions=(react-panel now-playing-notify hello-extension keyboard-skip sleep-timer)
themes=(minuit texte-agrandi bouton-arrondi masquer-chrome)

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
pkg="$stage/$name"
mkdir -p "$pkg/exemples/extensions" "$pkg/exemples/themes"

for file in "${binaries[@]}"; do cp "$bin_dir/$file" "$pkg/"; done
cp "$cli_dir/amk.exe" "$pkg/"
for id in "${extensions[@]}"; do
    [[ -f "$root/examples/$id/manifest.json" ]] || die "extension d'exemple introuvable : examples/$id"
    cp -R "$root/examples/$id" "$pkg/exemples/extensions/"
done
for id in "${themes[@]}"; do
    [[ -d "$root/themes/examples/$id" ]] || die "thème d'exemple introuvable : themes/examples/$id"
    cp -R "$root/themes/examples/$id" "$pkg/exemples/themes/"
done

# Text for Windows: CRLF, and a BOM on the files people open in Notepad. The .cmd file gets no BOM (cmd.exe would
# read it as part of the first command).
crlf() { sed -e 's/\r$//' -e 's/$/\r/' "$1"; }
bom() { printf '\xEF\xBB\xBF'; }
{ bom; crlf "$root/scripts/package/LISEZ-MOI.txt" | sed "s/@VERSION@/$version/"; } > "$pkg/LISEZ-MOI.txt"
{ bom; crlf "$root/docs/V0.5-GUIDE.md"; } > "$pkg/GUIDE-DU-TEST.txt"
crlf "$root/scripts/package/preparer-le-test.cmd" > "$pkg/preparer-le-test.cmd"
if [[ "$source_kind" == msvc ]]; then
    compiler="MSVC, avec la liaison XAML (thèmes, carte Mods, panneaux)"
else
    compiler="MinGW, SANS la liaison XAML : ni thèmes, ni carte Mods, ni panneaux. Pour vérifier l'emballage seulement."
fi
{
    echo "Apple Music Mod Kit $version"
    echo "Commit : $commit"
    echo "Compilateur : $compiler"
    echo "Build de diffusion : n'injecte le noyau que sur les versions d'Apple Music testées."
    echo "Paquet fait le $(date -u '+%Y-%m-%d %H:%M UTC') par scripts/package-v05.sh ($layout)."
} | sed 's/$/\r/' > "$pkg/BUILD_INFO.txt"

# SHA256SUMS.txt inside the zip lists every file of the package; the one next to the zip lists the zip itself.
(cd "$pkg" && find . -type f ! -name SHA256SUMS.txt | sed 's|^\./||' | LC_ALL=C sort | xargs -d '\n' sha256sum) > "$stage/sums"
mv "$stage/sums" "$pkg/SHA256SUMS.txt"

mkdir -p "$out"
out="$(cd "$out" && pwd)"
rm -f "$out/$name.zip" "$out/SHA256SUMS.txt"
# Entries are stored at the root of the zip (no top folder), sorted, with a fixed date, so the same inputs give
# the same zip.
python3 -I - "$pkg" "$out/$name.zip" <<'PY'
import os, sys, zipfile
src, dest = sys.argv[1], sys.argv[2]
files = []
for base, dirs, names in os.walk(src):
    for n in names:
        full = os.path.join(base, n)
        files.append(os.path.relpath(full, src).replace(os.sep, "/"))
with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for rel in sorted(files):
        info = zipfile.ZipInfo(rel, date_time=(2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o644 << 16
        with open(os.path.join(src, rel), "rb") as f:
            z.writestr(info, f.read())
PY
(cd "$out" && sha256sum "$name.zip" > SHA256SUMS.txt)

echo "Paquet : $out/$name.zip ($source_kind, $layout, commit $commit)"
echo "Empreinte : $out/SHA256SUMS.txt"
[[ "$source_kind" == mingw ]] && echo "Attention : paquet MinGW, sans interface (ni thèmes, ni carte Mods). Pour l'utilisateur, utilisez --msvc." >&2
exit 0
