/* iconv.h - our own small iconv for AmigaOS 3 (libnix has none).
 * Converts between UTF-8, UTF-16BE/LE, UTF-32BE/LE, WCHAR_T, ISO-8859-1 and ASCII.
 * Copyright (c) 2026 AmiCradle Team */
#ifndef FZAMIGA_ICONV_H
#define FZAMIGA_ICONV_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void *iconv_t;
iconv_t iconv_open(const char *tocode, const char *fromcode);
size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft);
int iconv_close(iconv_t cd);
#ifdef __cplusplus
}
#endif
#endif
