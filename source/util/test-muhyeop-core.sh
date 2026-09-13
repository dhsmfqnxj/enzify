#!/usr/bin/env bash
# Focused tests for P0 records and the extracted probability calculation.
# This is NOT a replacement for `make catch2-tests` or a playable game build.
set -euo pipefail
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_build_dir="${1:-$source_dir/muhyeop-core-build}"
mkdir -p -- "$test_build_dir"
test_build_dir="$(cd -- "$test_build_dir" && pwd)"
cd -- "$source_dir"
if [[ ! -f config.h ]]; then
    make config.h
fi
# Link only exercised core functions. Engine assertions are not enabled in
# this focused build; Catch checks and runtime range validation remain active.
# Unused shell adapter functions and random_var::roll are discarded, so this
# target makes no claim about shell lifecycle, RNG sequences or UI behavior.
"${CXX:-g++}" -std=c++14 -O1 -ffunction-sections -fdata-sections -I. -Iutil \
    catch2-tests/catch_amalgamated.cc \
    catch2-tests/test_mercenary_record.cc catch2-tests/test_attack-delay.cc \
    mercenary.cc attack-delay.cc random-var.cc \
    -Wl,--gc-sections -o "$test_build_dir/muhyeop-core-tests"
"$test_build_dir/muhyeop-core-tests"
