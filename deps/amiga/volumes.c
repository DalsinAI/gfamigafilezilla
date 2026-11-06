/* volumes.c - the mounted volumes ("SYS:", "Work:", ...), which FileZilla
 * shows where Unix shows "/" (libfilezilla patch 0006).
 * Copyright (c) 2026 AmiCradle Team */
#include <string.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include "fzamiga.h"

/* Writes each name with its colon, NUL after each, into buf; answers how
 * many. Names that do not fit are left out. */
int fz_amiga_volumes(char *buf, int len)
{
    struct DosList *dl;
    int count = 0, used = 0;

    dl = LockDosList(LDF_VOLUMES | LDF_READ);
    while ((dl = NextDosEntry(dl, LDF_VOLUMES))) {
        const UBYTE *b = (const UBYTE *)BADDR(dl->dol_Name);
        int n;
        if (!b || !dl->dol_Task)        /* not mounted (disk not in the drive) */
            continue;
        n = b[0];
        if (n == 0 || used + n + 2 > len)
            continue;
        memcpy(buf + used, b + 1, (size_t)n);
        buf[used + n] = ':';
        buf[used + n + 1] = 0;
        used += n + 2;
        ++count;
    }
    UnLockDosList(LDF_VOLUMES | LDF_READ);
    return count;
}
