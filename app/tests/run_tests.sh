#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# SMK-37 Repair: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
# The tests (Linux / WSL), after ./build.sh: the repair's logic on a simulated flash (and on a unit's dumps when
# REPAIR_ORIG / REPAIR_AFTER name them), the update loader over the whole app area, the packages, the flash map.
set -u
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
OUT=app/build/tests
B=app/build/smk37
FAIL=0
mkdir -p "$OUT"
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
run repair sh -c "$CC -O1 -Wall -Wextra -Wno-type-limits -Werror -Ibase/bsp/boards/smk37 -Iapp -o $OUT/repair_test app/tests/repair_test.c && $OUT/repair_test"
run image-size sh -c "test \$(stat -c %s $B/smkrepair.bin) -le 619520"
run no-plat-data sh -c "! grep -E 'call .*<plat_data_(erase|write)>' $B/smkrepair.dis"
run loader-range sh -c "$PY -c \"import os; open('$OUT/full.bin','wb').write(open('$B/smkrepair.bin','rb').read() + os.urandom(0x97400 - os.path.getsize('$B/smkrepair.bin')))\" && \
    head -c 60000 $B/smkrepair.bin > $OUT/old.bin && \
    $PY base/tools/fm1pkg_make.py --board smk37 --product 'SMK-37 Pro_490101' $OUT/full.bin $B/loader/ota.bin $OUT/full.fwsc >/dev/null && \
    $PY base/tools/fm1pkg_make.py --board smk37 --product 'SMK-37 Pro_490101' $OUT/old.bin $B/loader/ota.bin $OUT/old.fwsc >/dev/null && \
    $CC -w -DBOARD_SMK37=1 -o $OUT/ldr_test base/tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc $OUT/full.fwsc"
for m in pro elite mke; do
    case $m in pro) d=smk37; id="SMK-37 Pro_490101";; elite) d=smk37-elite; id="SMK-37 Elite_490101";; mke) d=smk37-mke; id="MKE-P37_490101";; esac
    f=$(ls app/build/$d/*.fwsc 2>/dev/null | head -1)
    if [ -n "$f" ]; then run package-$m "$PY" base/tests/smk_pkg_test.py "$f" "$id"; else echo "  skip package-$m (not built)"; fi
done
run smk-flash-map sh -c "$CC -O2 -w -DBOARD_SMK37=1 -Ibase/bsp/chip -Ibase/bsp/boards/smk37 -Ibase/include -o $OUT/smk_flash_test base/tests/smk_flash_test.c && $OUT/smk_flash_test"
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
