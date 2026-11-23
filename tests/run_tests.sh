#!/usr/bin/env bash
# Builds and runs the World Party Scaling regression suite in four isolated configurations:
# GCC and Clang, each plain (-O2) and with AddressSanitizer + UndefinedBehaviorSanitizer.
#
#   tests/run_tests.sh                 # all four configurations
#   CXX_LIST="g++" tests/run_tests.sh  # chosen compilers only
#   SANITIZE=0 tests/run_tests.sh      # plain builds only
#
# The real src/WorldPartyScaling.cpp is compiled unchanged against tests/fakecore (a mock core).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
out="${WPS_TEST_OUT:-$root/.wps-test-out}"
mkdir -p "$out"

if [[ -z "${CXX_LIST:-}" ]]; then
    CXX_LIST=""
    for c in g++ clang++; do command -v "$c" >/dev/null 2>&1 && CXX_LIST="$CXX_LIST $c"; done
fi

common=(-std=gnu++20 -Wall -Wextra -g -pthread
    -I"$here/fakecore" -I"$here" -I"$root/src"
    -DWPS_SOURCE_DIR="\"$root\"")
sources=("$root/src/WorldPartyScaling.cpp" "$here/WorldPartyScalingTests.cpp")

status=0
summary=()
for cxx in $CXX_LIST; do
    modes=(plain)
    [[ "${SANITIZE:-1}" == "1" ]] && modes+=(asan-ubsan)
    for mode in "${modes[@]}"; do
        tag="$(basename "$cxx")-$mode"
        flags=(-O2)
        if [[ "$mode" == "asan-ubsan" ]]; then
            flags=(-O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all)
        fi
        echo "=== build $tag ($("$cxx" --version | head -1))"
        if ! "$cxx" "${common[@]}" "${flags[@]}" "${sources[@]}" -o "$out/wps_tests_$tag" 2> "$out/build_$tag.log"; then
            cat "$out/build_$tag.log"
            summary+=("$tag: BUILD FAILED")
            status=1
            continue
        fi
        warnings=$(grep -c "warning:" "$out/build_$tag.log" || true)
        [[ "$warnings" != "0" ]] && cat "$out/build_$tag.log"
        echo "=== run $tag"
        if ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
            "$out/wps_tests_$tag" > "$out/run_$tag.log" 2>&1; then
            result="PASS"
        else
            result="FAIL"
            status=1
        fi
        grep -E "FAIL|EXCEPTION|ERROR|runtime error" "$out/run_$tag.log" || true
        summary+=("$tag: $result, warnings=$warnings, $(tail -1 "$out/run_$tag.log")")
    done
done

echo
printf '%s\n' "${summary[@]}"
exit $status
