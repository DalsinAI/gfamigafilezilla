#!/bin/sh
# Our Amiga glue (deps/amiga) and the GFOpenTLS and GFOpenCrypto link stubs
# for AmigaOS 3 (68040) into one sysroot: libfilezilla and fzssh call
# gfopentls.library and gfopencrypto.library through them. The stubs again
# only when their pinned commit changes.
#   TC=.../prefix WORK=build SYS=build/sysroot scripts/build-crypto.sh
# Copyright (c) 2026 AmiCradle Team
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$HERE/filezilla.lock"
TC=${TC:?}; WORK=${WORK:?}; SYS=${SYS:?}; JOBS=${JOBS:-12}
PATH="$TC/bin:$PATH"; D="$WORK/dl"
mkdir -p "$SYS/include" "$SYS/lib/pkgconfig" "$D"
HOSTB=$(gcc -dumpmachine)
CF="-noixemul -m68040 -O2"
# the soft-float link in the compiler itself: configure links its tests with
# CC and CFLAGS alone
XCC="m68k-amigaos-gcc -specs=$HERE/scripts/m68k-soft-m68040.specs -specs=$HERE/scripts/libm.specs"
LF="-noixemul -m68040 -specs=$HERE/scripts/m68k-soft-m68040.specs -L$SYS/lib"
export PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR="$SYS/lib/pkgconfig" PKG_CONFIG_SYSROOT_DIR=

fetch() { # url sha file
    if [ ! -f "$D/$3" ] || ! echo "$2  $D/$3" | sha256sum -c - >/dev/null 2>&1; then
        curl -fsSL -o "$D/$3.part" "$1"
        echo "$2  $D/$3.part" | sha256sum -c - >/dev/null || { echo "sha256 mismatch: $1"; exit 1; }
        mv "$D/$3.part" "$D/$3"
    fi
}
unpack() { # file dir
    rm -rf "$WORK/$2"; mkdir -p "$WORK/$2"; tar xf "$D/$1" -C "$WORK/$2" --strip-components=1
    for p in "$HERE"/patches/"$2"-*.patch; do
        [ -f "$p" ] || continue
        patch -s -d "$WORK/$2" -p1 < "$p" || { echo "patch failed: $p"; exit 1; }
    done
}
stamp() { cat "$HERE"/patches/"$1"-*.patch 2>/dev/null | sha256sum | cut -c1-16; }
fail() { # what, log: the log's end and, from configure, its checks that failed
    echo "== $1 failed"; tail -15 "$2"
    [ -f config.log ] && { echo "-- config.log errors:"; grep -n -B3 -E "error:|Error " config.log | tail -40; }
    [ -f configure.log ] && { echo "-- configure.log:"; tail -10 configure.log; }
    exit 1; }

# ---- our getentropy() and the rest libnix lacks
A="$WORK/fzamiga"; mkdir -p "$A"
for c in getentropy netcompat libccompat iconv dircompat dosrun idna wordexp volumes hook; do
    "$TC/bin/m68k-amigaos-gcc" $CF -Wall -I"$HERE/deps/amiga/include" -c "$HERE/deps/amiga/$c.c" -o "$A/$c.o" \
        || { echo "deps/amiga/$c.c failed"; exit 1; }
done
rm -f "$SYS/lib/libfzamiga.a"; "$TC/bin/m68k-amigaos-ar" rcs "$SYS/lib/libfzamiga.a" "$A"/*.o
cp "$HERE/deps/amiga/include/fzamiga.h" "$HERE/deps/amiga/include/fzdecl.h" "$HERE/deps/amiga/include/iconv.h" "$HERE/deps/amiga/include/idna.h" "$HERE/deps/amiga/include/idn-free.h" "$SYS/include/"

# ---- GFOpenTLS and GFOpenCrypto: headers and link stubs (no floating point)
if [ ! -f "$SYS/lib/libgfopentls.a" ] || [ "$(cat "$WORK/.gfopentls" 2>/dev/null)" != "$GFOPENTLS_COMMIT" ]; then
    G="$WORK/gfopentls"
    if [ "$(cat "$G/.commit" 2>/dev/null)" != "$GFOPENTLS_COMMIT" ]; then
        rm -rf "$G"; git clone -q "$GFOPENTLS_REPO" "$G" && git -C "$G" fetch -q origin "$GFOPENTLS_COMMIT" 2>/dev/null || :
        git -C "$G" checkout -q "$GFOPENTLS_COMMIT" || { echo "gfopentls $GFOPENTLS_COMMIT not found"; exit 1; }
        echo "$GFOPENTLS_COMMIT" > "$G/.commit"
    fi
    rm -rf "$G/out"
    (cd "$G" && STOVE="$(dirname "$TC")" OUT="$G/out" ./build-amiga.sh) > "$WORK/gfopentls.log" 2>&1 || fail gfopentls "$WORK/gfopentls.log"
    grep "FPU check" "$WORK/gfopentls.log" || :
    rm -f "$SYS/lib/libgfopentls.a" "$SYS/lib/libgfopencrypto.a"
    cp "$G/out/lib/soft-float/libgfopentls.a" "$G/out/lib/soft-float/libgfopencrypto.a" "$SYS/lib/"
    for d in clib gfopencrypto inline libraries proto; do
        mkdir -p "$SYS/include/$d"; cp "$G/include/$d/"*.h "$SYS/include/$d/"
    done
    printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n\nName: gfopencrypto\nDescription: GFOpenCrypto link stub (gfopencrypto.library 2.4)\nVersion: 2.4\nLibs: -L${libdir} -lgfopencrypto\nCflags: -I${includedir}\n' "$SYS" > "$SYS/lib/pkgconfig/gfopencrypto.pc"
    printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n\nName: gfopentls\nDescription: GFOpenTLS link stub (gfopentls.library 1.5)\nVersion: 1.5\nRequires: gfopencrypto\nLibs: -L${libdir} -lgfopentls\nCflags: -I${includedir}\n' "$SYS" > "$SYS/lib/pkgconfig/gfopentls.pc"
    echo "$GFOPENTLS_COMMIT" > "$WORK/.gfopentls"
    echo "gfopentls stubs: $(ls -l "$SYS/lib/libgfopentls.a" "$SYS/lib/libgfopencrypto.a" | awk '{printf "%s ", $5}')bytes"
fi
