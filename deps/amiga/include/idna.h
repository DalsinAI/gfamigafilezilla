/* idna.h - our own small IDNA for FileZilla on AmigaOS: host names with
 * letters beyond ASCII to their "xn--" form (RFC 3492 Punycode, label by
 * label; ASCII letters lower-cased, no full Nameprep). The calls and names
 * are GNU libidn's, which FileZilla uses.
 * Copyright (c) 2026 AmiCradle Team */
#ifndef FZAMIGA_IDNA_H
#define FZAMIGA_IDNA_H
#ifdef __cplusplus
extern "C" {
#endif
enum { IDNA_SUCCESS = 0, IDNA_PUNYCODE_ERROR = 3, IDNA_MALLOC_ERROR = 201, IDNA_INVALID_LENGTH = 205 };
enum { IDNA_ALLOW_UNASSIGNED = 1, IDNA_USE_STD3_ASCII_RULES = 2 };
int idna_to_ascii_8z(const char *input, char **output, int flags);
int stringprep_check_version(void);
#ifdef __cplusplus
}
#endif
#endif
