/* sqlite_amiga.c - SQLite's default file system layer on AmigaOS.
 *
 * AmigaOS has no fcntl() locks and its paths are not Unix paths:
 * "VOLUME:dir/file" is absolute, anything else is relative to the current
 * directory. The layer is SQLite's "unix-none" (no locking: one FileZilla
 * uses its queue database at a time) with its own full path names, made
 * the default when SQLite starts (SQLITE_EXTRA_INIT), as gfamigaaudacity's
 * patch 0014 does for Audacity.
 *
 * Copyright (c) 2026 AmiCradle Team */
#include <string.h>
#include <unistd.h>
#include "sqlite3.h"

static sqlite3_vfs amiga_vfs;

static int amiga_full_pathname(sqlite3_vfs *vfs, const char *name, int n_out, char *out)
{
    size_t len = 0, n;
    (void)vfs;
    if (n_out < 2) return SQLITE_CANTOPEN;
    if (!strchr(name, ':')) {
        if (!getcwd(out, (size_t)n_out - 1)) return SQLITE_CANTOPEN;
        len = strlen(out);
        if (len > 0 && out[len - 1] != ':' && out[len - 1] != '/' && *name) out[len++] = '/';
    }
    n = strlen(name);
    if (len + n + 1 > (size_t)n_out) return SQLITE_CANTOPEN;
    memcpy(out + len, name, n + 1);
    return SQLITE_OK;
}

int fz_amiga_sqlite_init(const char *unused)
{
    sqlite3_vfs *base = sqlite3_vfs_find("unix-none");
    (void)unused;
    if (!base) return SQLITE_OK;
    amiga_vfs = *base;
    amiga_vfs.zName = "amiga";
    amiga_vfs.pNext = NULL;
    amiga_vfs.xFullPathname = amiga_full_pathname;
    return sqlite3_vfs_register(&amiga_vfs, 1);
}
