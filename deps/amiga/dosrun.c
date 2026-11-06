/* dosrun.c - start a command and let it run on its own (FileZilla's
 * "open with" and "edit"): AmigaDOS SystemTags(), asynchronous, with NIL:
 * for its input and output.
 * Copyright (c) 2026 AmiCradle Team */
#include <dos/dostags.h>
#include <proto/dos.h>
#include "fzamiga.h"

int fz_amiga_run_detached(const char *cmdline)
{
    BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE), out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out) {
        if (in) Close(in);
        if (out) Close(out);
        return -1;
    }
    if (SystemTags((STRPTR)cmdline, SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE,
                   NP_StackSize, 65536, TAG_DONE) == -1) {
        /* not started: the handles are still ours */
        Close(in);
        Close(out);
        return -1;
    }
    return 0;
}
