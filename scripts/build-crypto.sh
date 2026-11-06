#!/bin/sh
# GMP, Nettle and GnuTLS for AmigaOS 3 (68040), static, into one sysroot,
# with our getentropy() (deps/amiga) as their seed. Each again only when
# its pinned tarball changes.
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
for c in getentropy netcompat libccompat iconv dircompat dosrun; do
    "$TC/bin/m68k-amigaos-gcc" $CF -Wall -I"$HERE/deps/amiga/include" -c "$HERE/deps/amiga/$c.c" -o "$A/$c.o" \
        || { echo "deps/amiga/$c.c failed"; exit 1; }
done
rm -f "$SYS/lib/libfzamiga.a"; "$TC/bin/m68k-amigaos-ar" rcs "$SYS/lib/libfzamiga.a" "$A"/*.o
cp "$HERE/deps/amiga/include/fzamiga.h" "$HERE/deps/amiga/include/fzdecl.h" "$HERE/deps/amiga/include/iconv.h" "$SYS/include/"

# ---- GMP (generic C for now: its m68k assembly is a later speed step)
if [ ! -f "$SYS/lib/libgmp.a" ] || [ "$(cat "$WORK/.gmp" 2>/dev/null)" != "$GMP_SHA256$(stamp gmp)" ]; then
    fetch "$GMP_URL" "$GMP_SHA256" gmp.tar.xz; unpack gmp.tar.xz gmp
    # Debian's tarball leaves out the manual; an empty doc/ for configure
    if [ ! -f "$WORK/gmp/doc/Makefile.in" ]; then
        mkdir -p "$WORK/gmp/doc"
        printf 'all install install-data install-exec install-strip uninstall check clean distclean mostlyclean maintainer-clean:\n\t@:\n.PHONY: all\n' > "$WORK/gmp/doc/Makefile.in"
    fi
    # C17: its configure checks call functions declared with ()
    (cd "$WORK/gmp" && CC="$XCC" CFLAGS="$CF -std=gnu17" LDFLAGS="$LF" CC_FOR_BUILD=gcc ./configure --host=m68k-amigaos \
        --build="$HOSTB" --prefix="$SYS" --disable-shared --enable-static --disable-assembly --disable-cxx \
        > configure.log 2>&1 || fail gmp-configure config.log
     nice -n 19 make -j"$JOBS" > make.log 2>&1 || fail gmp make.log
     make install > /dev/null 2>&1 || fail gmp-install make.log)
    echo "$GMP_SHA256$(stamp gmp)" > "$WORK/.gmp"
    echo "gmp $GMP_VERSION: $(ls -l "$SYS/lib/libgmp.a" | awk '{print $5}') bytes"
fi

# ---- Nettle and hogweed (no assembly for the 68k in Nettle)
if [ ! -f "$SYS/lib/libhogweed.a" ] || [ "$(cat "$WORK/.nettle" 2>/dev/null)" != "$NETTLE_SHA256$(stamp nettle)" ]; then
    fetch "$NETTLE_URL" "$NETTLE_SHA256" nettle.tar.gz; unpack nettle.tar.gz nettle
    (cd "$WORK/nettle" && CC="$XCC" CFLAGS="$CF -I$SYS/include" LDFLAGS="$LF" CC_FOR_BUILD=gcc ./configure \
        --host=m68k-amigaos --build="$HOSTB" --prefix="$SYS" --libdir="$SYS/lib" --disable-shared --enable-static \
        --disable-assembler --disable-documentation --disable-openssl --disable-fat \
        --with-include-path="$SYS/include" --with-lib-path="$SYS/lib" > configure.log 2>&1 || fail nettle-configure config.log
     nice -n 19 make -j"$JOBS" libnettle.a libhogweed.a > make.log 2>&1 || fail nettle make.log
     make install-headers install-static install-pkgconfig > /dev/null 2>&1 || fail nettle-install make.log)
    echo "$NETTLE_SHA256$(stamp nettle)" > "$WORK/.nettle"
    echo "nettle $NETTLE_VERSION: $(ls -l "$SYS/lib/libnettle.a" "$SYS/lib/libhogweed.a" | awk '{printf "%s ", $5}')bytes"
fi

# ---- GnuTLS: the TLS library alone (no tools, p11-kit, IDN or compression)
if [ ! -f "$SYS/lib/libgnutls.a" ] || [ "$(cat "$WORK/.gnutls" 2>/dev/null)" != "$GNUTLS_SHA256$(stamp gnutls)" ]; then
    fetch "$GNUTLS_URL" "$GNUTLS_SHA256" gnutls.tar.xz; unpack gnutls.tar.xz gnutls
    # _GNU_SOURCE: the stove's headers declare the BSD types only with it,
    # and gnulib replaces what it can't see
    (cd "$WORK/gnutls" && CC="$XCC" CFLAGS="$CF -D_GNU_SOURCE -I$SYS/include" LDFLAGS="$LF" \
        LIBS="-lfzamiga -lsocket -lpthread" ./configure --host=m68k-amigaos --build="$HOSTB" --prefix="$SYS" \
        --disable-shared --enable-static --with-included-libtasn1 --with-included-unistring --without-p11-kit \
        --without-idn --without-zlib --without-brotli --without-zstd --without-tpm --without-tpm2 \
        --disable-tools --disable-tests --disable-doc --disable-manpages --disable-nls --disable-cxx \
        --disable-hardware-acceleration --disable-padlock --disable-libdane --disable-ktls \
        --disable-full-test-suite --disable-valgrind-tests --with-default-trust-store-file= \
        --disable-dependency-tracking > configure.log 2>&1 || fail gnutls-configure config.log
     grep -E 'getentropy|getrandom' config.log | grep -E 'result|cv_func' | head -4
     # the library alone: gnulib's copy for it (gl) and lib, not src/ and its gnulib
     { nice -n 19 make -k -j"$JOBS" -C gl && nice -n 19 make -k -j"$JOBS" -C lib; } > make.log 2>&1 \
         || { grep -E "error:|undefined|\*\*\*" make.log | sort -u | cut -c1-300 | head -60; tail -25 make.log | cut -c1-300; exit 1; }
     make -C lib install > /dev/null 2>&1 || fail gnutls-install make.log)
    echo "$GNUTLS_SHA256$(stamp gnutls)" > "$WORK/.gnutls"
    echo "gnutls $GNUTLS_VERSION: $(ls -l "$SYS/lib/libgnutls.a" | awk '{print $5}') bytes"
fi
