#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# SMK-37 Repair: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
# Build SMK-37 Repair for the M-Vave SMK-37 (Pro, Elite, MKE-P37) with fm1-base (base/), the host simulator, and the
# packages into build/repair37/ with their SHA256SUMS. Linux or WSL.
#   ./build.sh [--release X.Y]
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain)
#   AC79_SDK         JieLi AC79 SDK checkout (default: ~/fw-AC79_AIoT_SDK)
#   PYTHON           a Python 3 with Pillow (and libraqm)
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
PY="${PYTHON:-python3}"
for m in pro elite mke; do
    BOARD=smk37 MODEL=$m "$PY" base/tools/build.py app "$@"
done
BOARD=smk37 "$PY" base/tools/build.py app --host
mkdir -p build/repair37
rm -f build/repair37/*.fwsc
cp app/build/smk37/*.fwsc app/build/smk37-elite/*.fwsc app/build/smk37-mke/*.fwsc build/repair37/
(cd build/repair37 && sha256sum *.fwsc > SHA256SUMS && cat SHA256SUMS)
