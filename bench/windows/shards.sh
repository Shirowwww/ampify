#!/usr/bin/env bash
# Splits the cases of bench/windows/cases into N groups for parallel runners and prints the matrix as JSON, for
# ci.yml: {"include":[{"shard":"1","cases":"id,id,..."},...]}. The ids are read from the case files (Register-Case),
# dealt out in file order so the heavy interface cases (60-65) spread over all runners. A new case needs no edit.
#
#   bench/windows/shards.sh [count]     default 4
set -euo pipefail
count="${1:-4}"
cd "$(dirname "$0")/cases"
ids="$(cat ./*.ps1 | sed -n "s/^Register-Case -Id '\([^']*\)'.*/\1/p")"
[[ -n "$ids" ]] || { echo "shards: no Register-Case found in bench/windows/cases" >&2; exit 1; }
printf '%s\n' "$ids" | awk -v count="$count" '
    { group[(NR - 1) % count] = group[(NR - 1) % count] (group[(NR - 1) % count] == "" ? "" : ",") $0 }
    END {
        printf "{\"include\":["
        for (i = 0; i < count && group[i] != ""; i++)
            printf "%s{\"shard\":\"%d\",\"cases\":\"%s\"}", (i ? "," : ""), i + 1, group[i]
        print "]}"
    }'
