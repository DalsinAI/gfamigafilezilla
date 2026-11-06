/* sqlite_pre.h - taken in ahead of sqlite3.c. libnix's pthread.h brings in
 * exec/types.h, whose GLOBAL ("extern") and LOCAL ("static") would replace
 * SQLite's own GLOBAL() macro; take those headers first and drop them.
 * Copyright (c) 2026 AmiCradle Team */
#include <exec/types.h>
#include <pthread.h>
#undef GLOBAL
#undef LOCAL
