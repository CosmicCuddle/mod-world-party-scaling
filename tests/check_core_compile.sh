#!/usr/bin/env bash
# Compiles the module translation units (to object files, not linked) against REAL AzerothCore
# headers. Proves the module's use of core APIs/enums/hook signatures matches the given core.
# It does NOT link a worldserver, start a server, or touch a database.
#
#   tests/check_core_compile.sh /path/to/azerothcore-wotlk [extra include dirs for boost/mysql...]
#
# Needs Boost and MySQL client headers (e.g. Debian: libboost-dev default-libmysqlclient-dev).
set -euo pipefail

core="${1:?usage: check_core_compile.sh <azerothcore-src> [extra-include-dir...]}"
shift || true
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
out="${WPS_TEST_OUT:-$root/.wps-test-out}/core-compile"
mkdir -p "$out"

includes=()
while IFS= read -r d; do includes+=("-I$d"); done < <(find "$core/src/common" "$core/src/server/game" "$core/src/server/shared" "$core/src/server/database" -type d)
for d in recastnavigation/Detour/Include recastnavigation/Recast/Include g3dlite/include fmt/include utf8cpp SFMT; do
    includes+=("-I$core/deps/$d")
done
for d in "$@"; do includes+=("-isystem" "$d"); done

if [[ -z "${CXX_LIST:-}" ]]; then
    CXX_LIST=""
    for c in g++ clang++; do command -v "$c" >/dev/null 2>&1 && CXX_LIST="$CXX_LIST $c"; done
fi

status=0
for cxx in $CXX_LIST; do
    for mode in plain asan-ubsan; do
        flags=(-O2)
        [[ "$mode" == "asan-ubsan" ]] && flags=(-O1 -fsanitize=address,undefined)
        tag="$(basename "$cxx")-$mode"
        ok=1
        for tu in WorldPartyScaling mod_world_party_scaling_loader; do
            if ! "$cxx" -std=gnu++20 -Wall -Wextra "${flags[@]}" "${includes[@]}" -I"$root/src" \
                -c "$root/src/$tu.cpp" -o "$out/${tu}_$tag.o" 2> "$out/${tu}_$tag.log"; then
                ok=0
                cat "$out/${tu}_$tag.log"
            fi
        done
        warnings=$(cat "$out"/*_"$tag".log | grep -c "warning:" || true)
        if [[ $ok == 1 ]]; then echo "$tag: core-header compile PASS (warnings=$warnings)"; else echo "$tag: core-header compile FAIL"; status=1; fi
    done
done
echo "core: $(git -C "$core" rev-parse HEAD 2>/dev/null || echo unknown)"
exit $status
