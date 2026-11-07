/* arexxhost.c - dos.library's text for an error number, for C++ code that
 * keeps the system headers out (FileZilla's ARexx FAULT command).
 * Copyright (c) 2026 AmiCradle Team */
#include <exec/types.h>
#include <clib/dos_protos.h>
#include "fzarexx.h"

void fz_rexx_fault(long code, char *buf, int len)
{
    if (len <= 0)
        return;
    buf[0] = 0;
    Fault(code, NULL, (STRPTR)buf, len);
}
