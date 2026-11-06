/* fzdecl.h - calls libnix has, or libfzamiga.a gives, that the stove's
 * headers leave undeclared. Small enough to -include ahead of gnulib's
 * own header wrappers (GnuTLS); fzamiga.h takes it in too.
 * Copyright (c) 2026 AmiCradle Team */
#ifndef FZDECL_H
#define FZDECL_H
#include <stdarg.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
int vasprintf(char **strp, const char *fmt, va_list ap);
/* libnix has it (libpthread's time support); time.h declares it only
 * under the POSIX timers switch, as the toolkit's amigacompat.h says */
__stdargs int nanosleep(const struct timespec *rqtp, struct timespec *rmtp);
#ifdef __cplusplus
}
#endif
#endif
