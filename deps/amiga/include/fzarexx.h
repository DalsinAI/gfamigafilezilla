/* fzarexx.h - what FileZilla's ARexx port needs from AmigaOS beyond the
 * toolkit's wxARexxPort, with no AmigaOS types (deps/amiga/arexxhost.c).
 * Copyright (c) 2026 AmiCradle Team */
#ifndef FZAREXX_H
#define FZAREXX_H
#ifdef __cplusplus
extern "C" {
#endif
/* dos.library's text for an error number, into buf */
void fz_rexx_fault(long code, char *buf, int len);
#ifdef __cplusplus
}
#endif
#endif
