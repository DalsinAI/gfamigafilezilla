/* fzarexx.h - a program's ARexx port on arexx.class (OS 3.2), with no
 * AmigaOS types, for C++ code that keeps the system headers out:
 * the port lives in a task of its own, which waits on the port's signal
 * and calls the handler there for each command (deps/amiga/arexxhost.c).
 * Copyright (c) 2026 AmiCradle Team */
#ifndef FZAREXX_H
#define FZAREXX_H
#ifdef __cplusplus
extern "C" {
#endif

/* one command: its name and its ReadArgs template ("" for none) */
struct fz_rexx_cmd {
    const char *name;
    const char *tmpl;
};

/* Called in the port's task for command number id (its place in the
 * table), with ReadArgs' answers in args (strings as char *, /N as
 * long *, /S as 0 or not). Gives the return code (0, 5, 10, 20), sets
 * *rc2 for an error and *result to a string the port copies when the
 * script asked for a result. */
typedef long (*fz_rexx_handler)(void *user, int id, long *args, long *rc2, char **result);

/* Opens arexx.class and makes the port HOST.1, HOST.2 ... in a new task.
 * Gives 0 when it is up (its name in name, len bytes), else the error:
 * -1 no arexx.class, -2 no task, or arexx.class's AREXX_ErrorCode. */
long fz_rexx_start(const char *host, const struct fz_rexx_cmd *cmds, int count,
                   fz_rexx_handler handler, void *user, char *name, int len);
/* Ends the task: commands waiting are answered RC 20, the port goes. */
void fz_rexx_stop(void);
/* dos.library's text for an error number, into buf */
void fz_rexx_fault(long code, char *buf, int len);

#ifdef __cplusplus
}
#endif
#endif
