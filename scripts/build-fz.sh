#!/bin/sh
# libfilezilla, Argon2 and fzssh for AmigaOS 3 (68040), static, into the
# sysroot build-crypto.sh made. Each again only when its tarball or its
# patches change.
#   TC=.../prefix WORK=build SYS=build/sysroot scripts/build-fz.sh
# Copyright (c) 2026 AmiCradle Team
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$HERE/filezilla.lock"
TC=${TC:?}; WORK=${WORK:?}; SYS=${SYS:?}; JOBS=${JOBS:-12}
PATH="$TC/bin:$PATH"; D="$WORK/dl"
HOSTB=$(gcc -dumpmachine)
SPECS="-specs=$HERE/scripts/m68k-soft-m68040.specs -specs=$HERE/scripts/libm.specs"
OPT="-noixemul -m68040 -O2"
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
fail() {
    echo "== $1 failed"; tail -15 "$2"
    [ -f config.log ] && { echo "-- config.log errors:"; grep -n -B3 -E "error:|Error " config.log | tail -40; }
    exit 1; }

# ---- libfilezilla
if [ ! -f "$SYS/lib/libfilezilla.a" ] || [ ! -f "$SYS/lib/libfilezilla.la" ] || [ "$(cat "$WORK/.lfz" 2>/dev/null)" != "$LFZ_SHA256$(stamp libfilezilla)" ]; then
    fetch "$LFZ_URL" "$LFZ_SHA256" libfilezilla.tar.xz; unpack libfilezilla.tar.xz libfilezilla
    (cd "$WORK/libfilezilla" && CC="m68k-amigaos-gcc $SPECS" CXX="m68k-amigaos-g++ $SPECS" \
        CFLAGS="$OPT -include fzamiga.h -I$SYS/include" CXXFLAGS="$OPT -include fzamiga.h -I$SYS/include" \
        LDFLAGS="-L$SYS/lib" LIBS="-lfzamiga -lsocket -lpthread -latomic" PKG_CONFIG="pkg-config --static" \
        ac_cv_func_timegm=yes \
        ./configure --host=m68k-amigaos --build="$HOSTB" --prefix="$SYS" --disable-shared --enable-static \
        --disable-locales --disable-doxygen-doc --disable-dependency-tracking > configure.log 2>&1 \
        || fail libfilezilla-configure configure.log
     grep -E '^#define (HAVE_|FZ_)' config.h 2>/dev/null | head -40 || :
     nice -n 19 make -k -j"$JOBS" -C lib > make.log 2>&1 || { grep -E 'error|Error' make.log | cut -c1-300 | head -60; exit 1; }
     make -C lib install > /dev/null 2>&1 || fail libfilezilla-install make.log
     # its libtool .la hands the link Unix-only -lcrypt (impersonation, replaced here)
     sed -i 's/ -lcrypt//g' "$SYS/lib/libfilezilla.la")
    echo "$LFZ_SHA256$(stamp libfilezilla)" > "$WORK/.lfz"
    echo "libfilezilla $LFZ_VERSION: $(ls -l "$SYS/lib/libfilezilla.a" | awk '{print $5}') bytes"
fi

# ---- Argon2 and fzssh, with our make file
if [ ! -f "$SYS/lib/libfzssh.a" ] || [ "$(cat "$WORK/.fzssh" 2>/dev/null)" != "$FZSSH_SHA256$ARGON2_SHA256$(stamp fzssh)$(sha256sum < "$HERE/scripts/Makefile.fzssh" | cut -c1-16)" ]; then
    fetch "$ARGON2_URL" "$ARGON2_SHA256" argon2.tar.xz; unpack argon2.tar.xz argon2
    fetch "$FZSSH_URL" "$FZSSH_SHA256" fzssh.tar.xz; unpack fzssh.tar.xz fzssh
    O="$WORK/obj/fzssh"; rm -rf "$O"; mkdir -p "$O/include/fzssh"
    V=$FZSSH_VERSION; MA=${V%%.*}; R=${V#*.}; MI=${R%%.*}; MC=${R#*.}
    sed -e "s/@PACKAGE_VERSION@/$V/; s/@PACKAGE_VERSION_MAJOR@/$MA/; s/@PACKAGE_VERSION_MINOR@/$MI/" \
        -e "s/@PACKAGE_VERSION_MICRO@/$MC/; s/@PACKAGE_VERSION_NANO@/0/; s/@PACKAGE_VERSION_SUFFIX@//" \
        "$WORK/fzssh/lib/fzssh/version.hpp.in" > "$O/include/fzssh/version.hpp"
    cat > "$O/config.hpp" <<CFG
#define PACKAGE_VERSION $V
#define PACKAGE_VERSION_S "$V"
#define PACKAGE_NAME libfzssh
#define PACKAGE_VERSION_MAJOR $MA
#define PACKAGE_VERSION_MINOR $MI
#define PACKAGE_VERSION_MICRO $MC
#define PACKAGE_VERSION_NANO 0
#define PACKAGE_VERSION_SUFFIX
#define PACKAGE_VERSION_SUFFIX_S ""
CFG
    nice -n 19 make -k -j"$JOBS" -f "$HERE/scripts/Makefile.fzssh" SRC="$WORK/fzssh" A2="$WORK/argon2" OBJ="$O" \
        SYS="$SYS" TC="$TC" > "$WORK/fzssh.log" 2>&1 || { grep -E 'error|Error' "$WORK/fzssh.log" | cut -c1-300 | head -60; exit 1; }
    echo "$FZSSH_SHA256$ARGON2_SHA256$(stamp fzssh)$(sha256sum < "$HERE/scripts/Makefile.fzssh" | cut -c1-16)" > "$WORK/.fzssh"
    # one library for fzssh's three (crypt, core, client), under the name
    # FileZilla's configure asks pkg-config for
    printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n\nName: libfzssh-client\nDescription: fzssh client\nVersion: %s\nRequires: libfilezilla nettle hogweed\nLibs: -L${libdir} -lfzssh -largon2\nCflags: -I${includedir}\n' "$SYS" "$FZSSH_VERSION" > "$SYS/lib/pkgconfig/libfzssh-client.pc"
    echo "fzssh $FZSSH_VERSION: $(ls -l "$SYS/lib/libfzssh.a" | awk '{print $5}') bytes"
fi

# ---- SQLite (FileZilla's queue and settings), with our file system layer
if [ ! -f "$SYS/lib/libsqlite3.a" ] || [ "$(cat "$WORK/.sqlite" 2>/dev/null)" != "$SQLITE_COMMIT$(stamp sqlite)$(sha256sum < "$HERE/deps/amiga/sqlite_amiga.c" | cut -c1-16)" ]; then
    Q="$WORK/sqlite-src"
    if [ "$(cat "$Q/.commit" 2>/dev/null)" != "$SQLITE_COMMIT" ]; then
        rm -rf "$Q"; mkdir -p "$Q"; git -C "$Q" init -q
        git -C "$Q" fetch -q --depth 1 --filter=blob:none "$SQLITE_GIT" "$SQLITE_COMMIT"
        git -C "$Q" checkout -q FETCH_HEAD -- lib-src/sqlite
        echo "$SQLITE_COMMIT" > "$Q/.commit"
    fi
    rm -rf "$WORK/sqlite"; cp -R "$Q/lib-src/sqlite" "$WORK/sqlite"
    for p in "$HERE"/patches/sqlite-*.patch; do
        patch -s -d "$WORK/sqlite" -p1 < "$p" || { echo "patch failed: $p"; exit 1; }
    done
    O="$WORK/obj/sqlite"; rm -rf "$O"; mkdir -p "$O"
    # no mmap (no shared-memory WAL index: a rollback journal), temporary
    # tables in memory (no /tmp), our layer made the default at start
    QF="$OPT -DSQLITE_OMIT_WAL -DSQLITE_MAX_MMAP_SIZE=0 -DSQLITE_TEMP_STORE=3 -DSQLITE_THREADSAFE=1 \
        -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_FOREIGN_KEYS=1 -DSQLITE_EXTRA_INIT=fz_amiga_sqlite_init"
    m68k-amigaos-gcc $QF -include "$HERE/deps/amiga/include/sqlite_pre.h" -c "$WORK/sqlite/sqlite3.c" -o "$O/sqlite3.o" > "$WORK/sqlite.log" 2>&1 || fail sqlite "$WORK/sqlite.log"
    m68k-amigaos-gcc $OPT -I"$WORK/sqlite" -c "$HERE/deps/amiga/sqlite_amiga.c" -o "$O/sqlite_amiga.o" >> "$WORK/sqlite.log" 2>&1 || fail sqlite "$WORK/sqlite.log"
    rm -f "$SYS/lib/libsqlite3.a"; m68k-amigaos-ar rcs "$SYS/lib/libsqlite3.a" "$O"/*.o
    cp "$WORK/sqlite/sqlite3.h" "$WORK/sqlite/sqlite3ext.h" "$SYS/include/" 2>/dev/null || cp "$WORK/sqlite/sqlite3.h" "$SYS/include/"
    SV=$(sed -n 's/^#define SQLITE_VERSION *"\(.*\)"/\1/p' "$WORK/sqlite/sqlite3.h")
    printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n\nName: SQLite\nDescription: SQL database engine\nVersion: %s\nLibs: -L${libdir} -lsqlite3\nCflags: -I${includedir}\n' "$SYS" "$SV" > "$SYS/lib/pkgconfig/sqlite3.pc"
    echo "$SQLITE_COMMIT$(stamp sqlite)$(sha256sum < "$HERE/deps/amiga/sqlite_amiga.c" | cut -c1-16)" > "$WORK/.sqlite"
    echo "sqlite $SV: $(ls -l "$SYS/lib/libsqlite3.a" | awk '{print $5}') bytes"
fi
