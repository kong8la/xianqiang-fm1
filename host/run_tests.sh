#!/bin/sh
# SPDX-License-Identifier: MIT
# Desktop tests and demo renders. No FM-1 and no JieLi toolchain.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host demos
CC="${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -DPM_NO_POOL -Idsp"
if grep -nE '(^|[^a-zA-Z_])(float|double)([^a-zA-Z_]|$)' dsp/pm.c dsp/pm.h >/dev/null; then
    echo "dsp uses float or double"
    exit 1
fi
$CC -o build/host/test_pm host/test_pm.c dsp/pm.c
build/host/test_pm
if $CC -fsanitize=address,undefined -fno-sanitize-recover=undefined -o build/host/test_pm_san host/test_pm.c dsp/pm.c; then
    build/host/test_pm_san
else
    echo "sanitizer runtime not installed; functional tests already passed"
fi
$CC -o build/host/render_wav host/render_wav.c dsp/pm.c
build/host/render_wav
echo "host dsp ok"
