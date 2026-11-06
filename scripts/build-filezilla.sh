#!/bin/sh
# FileZilla for AmigaOS 3 (68040; the FPU when there is one), with its own
# configure, on the toolkit SDK and the sysroot the earlier stages made.
# Its configure and tree are kept, so the next run's make goes on.
#   TC=.../prefix WORK=build SYS=build/sysroot WXCONFIG=.../wx-config \
#   FPUSPECS=.../fpuauto.specs FPULIB=.../fpuauto OUT=out BUDGET=s scripts/build-filezilla.sh
# Copyright (c) 2026 AmiCradle Team
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$HERE/filezilla.lock"
TC=${TC:?}; WORK=${WORK:?}; SYS=${SYS:?}; WXCONFIG=${WXCONFIG:?}; OUT=${OUT:?}; JOBS=${JOBS:-12}
PATH="$TC/bin:$PATH"; D="$WORK/dl"
HOSTB=$(gcc -dumpmachine)
SPECS="-specs=$HERE/scripts/m68k-soft-m68040.specs -specs=$HERE/scripts/libm.specs"
OPT="-noixemul -m68040 -O2 -fno-delete-null-pointer-checks"
export PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR="$SYS/lib/pkgconfig" PKG_CONFIG_SYSROOT_DIR=

fetch() { # url sha file
    if [ ! -f "$D/$3" ] || ! echo "$2  $D/$3" | sha256sum -c - >/dev/null 2>&1; then
        curl -fsSL -o "$D/$3.part" "$1"
        echo "$2  $D/$3.part" | sha256sum -c - >/dev/null || { echo "sha256 mismatch: $1"; exit 1; }
        mv "$D/$3.part" "$D/$3"
    fi
}
SRC="$WORK/filezilla"
# The tree and its objects are kept between runs. When the patches applied
# so far are unchanged, only new ones go on (make then rebuilds just what
# they touch); a changed or dropped patch, or a new FileZilla, starts afresh.
applied="$SRC/.applied"
want=$(echo "$FZ_SHA256 $WXT_COMMIT"; for p in "$HERE"/patches/filezilla-*.patch; do [ -f "$p" ] && echo "${p##*/} $(sha256sum < "$p" | cut -c1-16)"; done)
have=$(cat "$applied" 2>/dev/null || :)
case "$want" in
    "$have"*) [ -n "$have" ] || fresh=1 ;;
    *) fresh=1 ;;
esac
if [ -n "${fresh:-}" ]; then
    fetch "$FZ_URL" "$FZ_SHA256" filezilla.tar.xz
    rm -rf "$SRC"; mkdir -p "$SRC"; tar xf "$D/filezilla.tar.xz" -C "$SRC" --strip-components=1
    echo "$FZ_SHA256 $WXT_COMMIT" > "$applied"
fi
for p in "$HERE"/patches/filezilla-*.patch; do
    [ -f "$p" ] || continue
    line="${p##*/} $(sha256sum < "$p" | cut -c1-16)"
    grep -qxF "$line" "$applied" && continue
    patch -s -d "$SRC" -p1 < "$p" || { echo "patch failed: $p"; rm -f "$applied"; exit 1; }
    echo "$line" >> "$applied"
    # a patch to configure itself means configuring again
    grep -q '^+++ b/configure' "$p" && rm -f "$SRC/config.status"
done
# Boost.Regex, headers only
BR="$WORK/boost-regex"
if [ "$(cat "$BR/.commit" 2>/dev/null)" != "$BREGEX_COMMIT" ]; then
    rm -rf "$BR"; mkdir -p "$BR"; git -C "$BR" init -q
    git -C "$BR" fetch -q --depth 1 "$BREGEX_GIT" "$BREGEX_COMMIT"
    git -C "$BR" checkout -q FETCH_HEAD -- include
    echo "$BREGEX_COMMIT" > "$BR/.commit"
fi
cd "$SRC"
if [ ! -f config.status ]; then
    CC="m68k-amigaos-gcc $SPECS" CXX="m68k-amigaos-g++ $SPECS" \
    CFLAGS="$OPT -include fzamiga.h -I$SYS/include" CXXFLAGS="$OPT -include fzamiga.h -I$SYS/include -I$BR/include" \
    LDFLAGS="-L$SYS/lib -specs=$FPUSPECS -L$FPULIB" LIBS="-lfzamiga -lsocket -lpthread -latomic" \
    PKG_CONFIG="pkg-config --static" ac_cv_func_timegm=yes ac_cv_func_localtime_r=yes ac_cv_func_gmtime_r=yes ./configure --host=m68k-amigaos --build="$HOSTB" --prefix=/FileZilla \
        --with-wx-config="$WXCONFIG" --disable-shared --enable-static --disable-locales --disable-manualupdatecheck \
        --disable-autoupdatecheck --without-dbus --disable-storj --disable-precomp --with-pugixml=builtin --with-idn-lib=-lfzamiga \
        --disable-dependency-tracking > configure.log 2>&1 || {
        echo "== filezilla configure failed"; tail -15 configure.log
        grep -n -B3 -E "error:|Error " config.log | tail -50; rm -f config.status; exit 1; }
fi
echo "filezilla: make (for ${BUDGET:-?} s)"
st=0
timeout "${BUDGET:-600}" nice -n 19 make -k -j"$JOBS" > make.log 2>&1 || st=$?
if [ "$st" = 124 ]; then echo "filezilla: time's up, the next run goes on"; exit 0; fi
if [ "$st" != 0 ]; then
    grep -E "error:|undefined reference|Error [0-9]" make.log | sort -u | cut -c1-300 | head -80; exit 1
fi
cp src/interface/filezilla "$OUT/FileZilla"
ls -l "$OUT/FileZilla"
