#!/bin/sh
# fzprobe (tests/amiga/fzprobe.cpp): SFTP, FTP and FTPS from the Shell on
# the libfilezilla and fzssh FileZilla links, every log line timed, for
# finding where a connection stops. Goes to $OUT/tests/fzprobe.
#   TC=... WORK=... SYS=... FPUSPECS=... FPULIB=... OUT=out scripts/build-probe.sh
# Copyright (c) 2026 AmiCradle Team
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TC=${TC:?}; WORK=${WORK:?}; SYS=${SYS:?}; OUT=${OUT:?}
PATH="$TC/bin:$PATH"
SPECS="-specs=$HERE/scripts/m68k-soft-m68040.specs -specs=$HERE/scripts/libm.specs"
OPT="-noixemul -m68040 -O2 -fno-delete-null-pointer-checks"
export PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR="$SYS/lib/pkgconfig" PKG_CONFIG_SYSROOT_DIR=
src="$HERE/tests/amiga/fzprobe.cpp"
stamp="$(sha256sum < "$src" | cut -c1-16) $(ls -l --time-style=+%s "$SYS/lib/libfilezilla.a" "$SYS/lib/libfzssh.a" 2>/dev/null | awk '{print $5$6}' | tr '\n' ' ')"
if [ -f "$OUT/tests/fzprobe" ] && [ "$(cat "$WORK/.fzprobe" 2>/dev/null)" = "$stamp" ]; then
    echo "fzprobe: unchanged"; exit 0
fi
mkdir -p "$OUT/tests"
m68k-amigaos-g++ $SPECS $OPT -std=gnu++17 -include fzamiga.h -I"$SYS/include" $(pkg-config --static --cflags libfzssh-client libfilezilla) \
    "$src" -o "$OUT/tests/fzprobe" -L"$SYS/lib" -specs="$FPUSPECS" -L"$FPULIB" \
    $(pkg-config --static --libs libfzssh-client libfilezilla) -lfzamiga -lsocket -lpthread -latomic > "$WORK/fzprobe.log" 2>&1 \
    || { grep -E "error|undefined" "$WORK/fzprobe.log" | head -30; echo "fzprobe failed"; exit 1; }
m68k-amigaos-strip "$OUT/tests/fzprobe"
echo "$stamp" > "$WORK/.fzprobe"
ls -l "$OUT/tests/fzprobe"
