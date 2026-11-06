#!/usr/bin/env bash
# Builds the package: dist/ampify-<version>-windows-x64.zip and dist/SHA256SUMS.txt, the two names `ampify update`
# and scripts/install.ps1 look for in a release (src/cli/marketplace.cpp). Nothing is published: the zip is attached to a
# release by hand, or uploaded as a workflow artifact by .github/workflows/package.yml. The version, the file names and
# the elements that ship with the kit come from facts/release.json.
#
# Usage:
#   scripts/package.sh --msvc <dir>      the ampify-msvc-kit artifacts of ci.yml, or an MSVC build folder
#   options: --out <dir> (default dist), --commit <sha> (written to BUILD_INFO.txt)
#
# Only the release build is taken (tested Apple Music versions only). A bench build (AMPIFY_BENCH=ON) is refused.
#
# The zip is flat, as `ampify update` and ampify_setup.exe both need: ampify.exe, ampify_launcher.exe and ampify_kernel.dll at the
# root, next to ampify_setup.exe (the installer copies the launcher and the kernel from its own folder). The elements
# that ship with the kit (the Marketplace, Now Playing) and example extensions and themes go under examples/. The
# installer and `ampify update --apply` put the first ones in the user's Ampify folder (src/core/kit_layout.hpp).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/dist"
source_dir=""
commit=""

die() { echo "package: $*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --msvc) source_dir="${2:-}"; [[ -n "$source_dir" ]] || die "--msvc needs a folder"; shift 2 ;;
        --out) out="${2:-}"; [[ -n "$out" ]] || die "--out needs a folder"; shift 2 ;;
        --commit) commit="${2:-}"; shift 2 ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1 (see --help)" ;;
    esac
done
[[ -n "$source_dir" ]] || die "say where the binaries come from: --msvc <folder>"
[[ -d "$source_dir" ]] || die "folder not found: $source_dir"
source_dir="$(cd "$source_dir" && pwd)"
command -v python3 >/dev/null || die "python3 is needed (zip and hashes)"

# Python on Windows ends its lines with CR LF; the CR goes.
CR=$'\r'
# One text of facts/release.json, by its path, such as files.setup.
fact() {
    python3 -I - "$root/facts/release.json" "$1" <<'PY' | tr -d "$CR"
import json, sys
value = json.load(open(sys.argv[1], encoding="utf-8"))
for key in sys.argv[2].split("."):
    value = value[key]
print(value)
PY
}
version="$(fact version)" || die "no version in facts/release.json"
package_name="$(fact package.prefix)$version$(fact package.suffix)"
name="${package_name%.zip}"
checksums="$(fact package.checksums)"
setup_file="$(fact files.setup)"
cli_file="$(fact files.cli)"
binaries=("$setup_file" "$(fact files.launcher)" "$(fact files.kernel)")
# The bundled elements that ship as a folder, as <folder>/<id> (the built-in ones live in the kernel).
mapfile -t bundled < <(python3 -I - "$root/facts/release.json" <<'PY' | tr -d "$CR"
import json, sys
for element in json.load(open(sys.argv[1], encoding="utf-8"))["elements"]:
    if not element["builtIn"]:
        print(f"{element['folder']}/{element['id']}")
PY
)

# Where each binary is, for the two layouts: a kit (release/ and cli/, the CI artifacts) or a build folder.
if [[ -f "$source_dir/release/$setup_file" ]]; then
    layout=kit
    bin_dir="$source_dir/release"
    cli_dir="$source_dir/cli"
    info="$source_dir/BUILD_INFO.txt"
    if [[ -f "$info" ]]; then
        [[ -z "$commit" ]] && commit="$(sed -n 's/^Commit: //p' "$info" | head -n 1)"
        grep -q 'MSVC' "$info" || die "$info does not mention MSVC: this is not the ampify-msvc-kit kit"
    fi
else
    layout=build
    bin_dir="$source_dir"
    cli_dir="$source_dir"
    cache="$source_dir/CMakeCache.txt"
    if [[ -f "$cache" ]]; then
        grep -q '^AMPIFY_BENCH:BOOL=ON' "$cache" && die "$source_dir is a bench build (AMPIFY_BENCH=ON): it is never shipped"
    else
        echo "package: warning, no CMakeCache.txt in $source_dir: cannot check that this is not a bench build" >&2
    fi
fi
if [[ -z "$commit" ]]; then
    # A local build: the binaries come from the working tree, which may hold changes not yet committed.
    commit="$(git -C "$root" rev-parse HEAD 2>/dev/null || echo unknown)"
    git -C "$root" diff --quiet HEAD 2>/dev/null || commit+=" (with uncommitted changes)"
fi

for file in "${binaries[@]}"; do [[ -f "$bin_dir/$file" ]] || die "missing file: $bin_dir/$file"; done
[[ -f "$cli_dir/$cli_file" ]] || die "missing file: $cli_dir/$cli_file"

# The file version of a binary, from the VS_FIXEDFILEINFO of its version resource (cmake/version.rc.in), as 1.2.3.
file_version() {
    python3 -I - "$1" <<'PY' | tr -d "$CR"
import struct, sys
data = open(sys.argv[1], "rb").read()
at = data.find(struct.pack("<I", 0xFEEF04BD))
if at < 0:
    sys.exit(1)
high, low = struct.unpack_from("<II", data, at + 8)
print(f"{high >> 16}.{high & 0xFFFF}.{low >> 16}")
PY
}
# A Windows PE file starts with MZ. Anything else is a Linux binary or a broken download. Binaries left over from
# another version would install under the wrong number: each one's version resource must be the release's.
for file in "${binaries[@]/#/$bin_dir/}" "$cli_dir/$cli_file"; do
    [[ "$(head -c 2 "$file")" == "MZ" ]] || die "$(basename "$file") is not a Windows executable"
    found="$(file_version "$file")" || die "$(basename "$file") has no version resource"
    [[ "$found" == "$version" ]] ||
        die "$(basename "$file") is version $found, not $version: binaries of another version (older run?)"
done

# Every example extension and every theme of the repository (the snippets are for theme authors, not shipped).
mapfile -t extensions < <(cd "$root/examples" && find . -mindepth 2 -maxdepth 2 -name manifest.json | cut -d/ -f2 | LC_ALL=C sort)
mapfile -t themes < <(cd "$root/themes" && find . -mindepth 2 -maxdepth 2 -name manifest.json | cut -d/ -f2 | LC_ALL=C sort)

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
pkg="$stage/$name"
mkdir -p "$pkg/examples/extensions" "$pkg/examples/themes" "$pkg/examples/apps"

for file in "${binaries[@]}"; do cp "$bin_dir/$file" "$pkg/"; done
cp "$cli_dir/$cli_file" "$pkg/"
for id in "${extensions[@]}"; do cp -R "$root/examples/$id" "$pkg/examples/extensions/"; done
for element in "${bundled[@]}"; do cp -R "$root/$element" "$pkg/examples/${element%/*}/"; done
for id in "${themes[@]}"; do cp -R "$root/themes/$id" "$pkg/examples/themes/"; done

# Text for Windows: CRLF, and a BOM on the files people open in Notepad.
crlf() { sed -e 's/\r$//' -e 's/$/\r/' "$1"; }
bom() { printf '\xEF\xBB\xBF'; }
{ bom; crlf "$root/scripts/package/README.txt" | sed "s/@VERSION@/$version/"; } > "$pkg/README.txt"
{
    echo "Ampify $version"
    echo "Commit: $commit"
    echo "Compiler: MSVC, with the XAML binding (themes, Ampify card, panels)"
    echo "Release build: injects the kernel only on tested Apple Music versions."
    echo "Package made on $(date -u '+%Y-%m-%d %H:%M UTC') by scripts/package.sh ($layout)."
} | sed 's/$/\r/' > "$pkg/BUILD_INFO.txt"

# SHA256SUMS.txt inside the zip lists every file of the package; the one next to the zip lists the zip itself.
(cd "$pkg" && find . -type f ! -name "$checksums" | sed 's|^\./||' | LC_ALL=C sort | xargs -d '\n' sha256sum) > "$stage/sums"
mv "$stage/sums" "$pkg/$checksums"

mkdir -p "$out"
out="$(cd "$out" && pwd)"
rm -f "$out/$name.zip" "$out/$checksums"
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
(cd "$out" && sha256sum "$name.zip" > "$checksums")

echo "Package: $out/$name.zip ($layout, commit $commit)"
echo "Hashes: $out/$checksums"
exit 0
