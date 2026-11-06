/* hook.c - a struct Hook entry for C and C++ code: AmigaOS calls h_Entry
 * with the hook in A0, the object in A2 and the message in A1; this passes
 * them on to h_SubEntry as an ordinary C call. libfilezilla's TLS layer
 * hands GFOpenTLS its I/O hook this way.
 * Copyright (c) 2026 AmiCradle Team */
#include <exec/types.h>
#include <utility/hooks.h>

typedef ULONG (*fz_hook_func)(struct Hook *, APTR, APTR);

ULONG fz_hook_entry(register struct Hook *hook __asm("a0"),
                    register APTR object __asm("a2"),
                    register APTR message __asm("a1"))
{
    return ((fz_hook_func)hook->h_SubEntry)(hook, object, message);
}
