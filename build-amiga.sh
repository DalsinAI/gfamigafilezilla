#!/bin/sh
# Builds FileZilla 3.71.0 for AmigaOS 3: 68040, the FPU when there is one
# (fpuauto), on the GFOpenWX Toolkit. Stages, each again only when its
# inputs change, so a run that stops at the time cap goes on next run:
#   1 the stove probe, fpuauto, the GFOpenTLS and GFOpenCrypto link stubs (scripts/build-crypto.sh)
#   2 libfilezilla, fzssh, SQLite (scripts/build-fz.sh)
#   3 the toolkit SDK, then FileZilla (scripts/build-filezilla.sh)
#   STOVE=.../stoves/os32-gcc16-open6 OUT=out ./build-amiga.sh
# Everything third-party is fetched at the versions filezilla.lock gives.
# Copyright (c) 2026 AmiCradle Team
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$HERE/filezilla.lock"
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16-open6"}
# the work tree outside the checkout: Cradle cleans the checkout before each job
WORK="${WORK:-$HOME/.cache/gfamigafilezilla/build}"; OUT="${OUT:-$WORK/out}"; JOBS=${JOBS:-12}
TC="$STOVE/prefix"; SYS="$WORK/sysroot"
mkdir -p "$WORK/deps" "$WORK/dl" "$OUT" "$SYS/lib"
t0=$(date +%s)
echo "stove: $("$TC/bin/m68k-amigaos-gcc" --version | head -1)"

fetch() {   # name repo commit [paths...]: one commit, its tree (or those paths)
    d="$WORK/deps/$1"; repo=$2; c=$3; shift 3
    if [ "$(cat "$d/.commit" 2>/dev/null)" != "$c" ]; then
        rm -rf "${d:?}"; mkdir -p "$d"
        git -C "$d" init -q
        git -C "$d" fetch -q --depth 1 "$repo" "$c"
        if [ $# -gt 0 ]; then git -C "$d" checkout -q FETCH_HEAD -- "$@"; else git -C "$d" checkout -q FETCH_HEAD; fi
        echo "$c" > "$d/.commit"
    fi
}

# ---- what the stove gives (once per stove compiler)
GCCV="$("$TC/bin/m68k-amigaos-gcc" --version | head -1) $(sha256sum < "$HERE/scripts/probe.sh" | cut -c1-16)"
if [ "$(cat "$WORK/.probe" 2>/dev/null)" != "$GCCV" ] || [ -n "${PROBE:-}" ]; then
    TC="$TC" sh "$HERE/scripts/probe.sh" | tee "$WORK/probe.txt"
    echo "$GCCV" > "$WORK/.probe"
fi

# ---- fpuauto: libfpuauto.a from its fpuauto.S, as its build-amiga.sh makes it
fetch fpuauto "$FPUAUTO_REPO" "$FPUAUTO_COMMIT" common/fpuauto
FA="$WORK/deps/fpuauto/common/fpuauto"; FAL="$WORK/fpuauto"
if [ "$(cat "$FAL/.commit" 2>/dev/null)" != "$FPUAUTO_COMMIT" ]; then
    rm -rf "$FAL"; mkdir -p "$FAL/obj"
    "$TC/bin/m68k-amigaos-gcc" -m68040 -DFPUAUTO_COMMON -c "$FA/fpuauto.S" -o "$FAL/obj/fa_common.o"
    for n in $(sed 's/^--wrap=//' "$FA/fpuauto.wraps"); do
        "$TC/bin/m68k-amigaos-gcc" -m68040 -DR_$n -c "$FA/fpuauto.S" -o "$FAL/obj/fa_$n.o"
    done
    "$TC/bin/m68k-amigaos-ar" rcs "$FAL/libfpuauto.a" "$FAL"/obj/fa_*.o
    echo "$FPUAUTO_COMMIT" > "$FAL/.commit"
fi

# ---- 1: the GFOpenTLS and GFOpenCrypto link stubs
TC="$TC" WORK="$WORK" SYS="$SYS" JOBS=$JOBS sh "$HERE/scripts/build-crypto.sh"
# the two libraries themselves, for the package's Libs/ (the installer puts
# them in LIBS: when the ones there are older): gfopencrypto.library from
# the stub build, gfopentls.library from build-amiga-tls.sh
G="$WORK/gfopentls"
if [ ! -f "$OUT/Libs/gfopentls.library" ] || [ "$(cat "$WORK/.gfopentls-libs" 2>/dev/null)" != "$GFOPENTLS_COMMIT" ]; then
    (cd "$G" && STOVE="$STOVE" OUT="$G/out-tls" sh ./build-amiga-tls.sh) > "$WORK/gfopentls-tls.log" 2>&1 \
        || { tail -20 "$WORK/gfopentls-tls.log"; echo "gfopentls.library failed"; exit 1; }
    mkdir -p "$OUT/Libs"
    cp "$G/out-tls/lib/gfopentls.library" "$G/out/lib/gfopencrypto.library" "$OUT/Libs/"
    echo "$GFOPENTLS_COMMIT" > "$WORK/.gfopentls-libs"
fi
ls -l "$OUT/Libs/"
echo "crypto: $(( $(date +%s) - t0 )) s"

# ---- 2: libfilezilla, fzssh, SQLite
TC="$TC" WORK="$WORK" SYS="$SYS" JOBS=$JOBS sh "$HERE/scripts/build-fz.sh"
echo "libfilezilla and fzssh: $(( $(date +%s) - t0 )) s"
# the Shell probe for SFTP, FTP and FTPS (tests/amiga/fzprobe.cpp)
TC="$TC" WORK="$WORK" SYS="$SYS" FPUSPECS="$FA/fpuauto.specs" FPULIB="$FAL" OUT="$OUT" sh "$HERE/scripts/build-probe.sh"

# ---- 3: the toolkit's SDK (when its commit changed), then FileZilla
fetch gfopenwxtoolkit "$WXT_REPO" "$WXT_COMMIT"
WXT="$WORK/deps/gfopenwxtoolkit"; WXB="$WORK/gfopenwx"
# a toolkit SDK another Cradle job built (WXT_SDK_JOB, its tarball's
# SHA-256 in WXT_SDK_SHA256), when filezilla.lock names one: Cradle keeps
# job outputs in jobs/<id>/out/ beside this checkout
if [ -n "${WXT_SDK_JOB:-}" ] && [ "$(cat "$WXB/.rev" 2>/dev/null)" != "$WXT_COMMIT $WXT_SDK_JOB" ]; then
    tb="${WXT_SDK_DIR:-$HERE/../jobs/$WXT_SDK_JOB/out}/gfopenwx-sdk.tar.gz"
    [ -f "$tb" ] || { echo "toolkit SDK $tb is not there"; exit 1; }
    echo "$WXT_SDK_SHA256  $tb" | sha256sum -c - >/dev/null || { echo "toolkit SDK $tb: sha256 mismatch"; exit 1; }
    rm -rf "$WXB/unpack" "$WXB/out/sdk"; mkdir -p "$WXB/unpack" "$WXB/out"
    tar xzf "$tb" -C "$WXB/unpack"
    wc=$(find "$WXB/unpack" -path '*/bin/wx-config' | head -1)
    [ -n "$wc" ] || { echo "toolkit SDK $tb has no bin/wx-config"; exit 1; }
    mv "$(dirname "$(dirname "$wc")")" "$WXB/out/sdk"; rm -rf "$WXB/unpack"
    echo "$WXT_COMMIT $WXT_SDK_JOB" > "$WXB/.rev"
    echo "toolkit: SDK from job $WXT_SDK_JOB"
fi
if [ -z "${WXT_SDK_JOB:-}" ] && { [ ! -x "$WXB/out/sdk/bin/wx-config" ] || [ "$(cat "$WXB/.rev" 2>/dev/null)" != "$WXT_COMMIT" ]; }; then
    echo "toolkit $WXT_COMMIT: building its SDK"
    # its make goes on where it stopped, so a run that runs out of time
    # ends cleanly here and the next one carries on
    wleft=$(( ${BUDGET_TOTAL:-840} - 20 - ($(date +%s) - t0) ))
    st=0; STOVE="$STOVE" WORK="$WXB" OUT="$WXB/out" JOBS=$JOBS timeout "$wleft" sh "$WXT/build-amiga.sh" > "$WORK/wx.log" 2>&1 || st=$?
    if [ "$st" = 124 ]; then
        echo "toolkit SDK not finished this run ($(find "$WXB" -name '*.o' -newer "$WXT/build-amiga.sh" | wc -l) objects so far)"
        exit 0
    fi
    [ "$st" = 0 ] || { tail -40 "$WORK/wx.log"; exit 1; }
    [ -x "$WXB/out/sdk/bin/wx-config" ] || { tail -20 "$WORK/wx.log"; echo "toolkit SDK not finished this run"; exit 0; }
    echo "$WXT_COMMIT" > "$WXB/.rev"
    echo "toolkit: $(( $(date +%s) - t0 )) s"
fi
left=$(( ${BUDGET_TOTAL:-840} - ($(date +%s) - t0) ))
[ "$left" -gt 60 ] || { echo "no time left for FileZilla this run"; exit 0; }
TC="$TC" WORK="$WORK" SYS="$SYS" JOBS=$JOBS WXCONFIG="$WXB/out/sdk/bin/wx-config" FPUSPECS="$FA/fpuauto.specs" \
    FPULIB="$FAL" OUT="$OUT" BUDGET=$left sh "$HERE/scripts/build-filezilla.sh"
ls -l "$SYS/lib"/*.a
echo "total: $(( $(date +%s) - t0 )) s"
