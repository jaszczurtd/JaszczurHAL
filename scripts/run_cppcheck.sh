#!/usr/bin/env bash
# cppcheck gate over JaszczurHAL's own code with the pinned cppcheck; vendored
# code is excluded. runalltests.sh and CI both run it.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"
exec scripts/cppcheck.sh --enable=warning,performance,portability \
    --inline-suppr \
    --suppressions-list=tests/cppcheck-suppressions.txt \
    --library=config/tooling/cppcheck-atomics.cfg \
    -i src/hal/impl/rp2040/drivers \
    -i src/hal/codecs/cjson \
    -i src/hal/codecs/jpeg \
    -i src/hal/codecs/lodepng \
    -i src/utils/unity.c \
    --error-exitcode=1 --quiet \
    src
