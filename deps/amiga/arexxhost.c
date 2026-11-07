/* arexxhost.c - a program's ARexx port on arexx.class (OS 3.2 API):
 * AREXX_HostName gives HOST.1 (HOST.2 ... when taken), AREXX_Commands the
 * table with its ReadArgs templates, ac_RC/ac_RC2/ac_Result the reply.
 * The port lives in a task of its own that waits on AREXX_SigMask and
 * hands each command to the program's handler (fzarexx.h), so a program
 * whose main loop takes no extra signals still answers at once, and
 * nothing polls.
 * Copyright (c) 2026 AmiCradle Team */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/tasks.h>
#include <intuition/classusr.h>
#include <utility/tagitem.h>
#include <dos/dos.h>
#include <clib/exec_protos.h>
#include <clib/intuition_protos.h>
#include <clib/dos_protos.h>
#include <clib/alib_protos.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "fzarexx.h"

/* classes/arexx.h (NDK 3.2) */
#define FZ_AREXX_Dummy     (TAG_USER + 0x5000000 + 0x30000)
#define FZ_AREXX_HostName  (FZ_AREXX_Dummy + 1)
#define FZ_AREXX_Commands  (FZ_AREXX_Dummy + 3)
#define FZ_AREXX_ErrorCode (FZ_AREXX_Dummy + 4)
#define FZ_AREXX_SigMask   (FZ_AREXX_Dummy + 5)
#define FZ_AM_HANDLEEVENT  0x590001UL

struct fz_ARexxCmd {
    STRPTR ac_Name;
    UWORD ac_ID;
    void (*ac_Func)(void);
    STRPTR ac_ArgTemplate;
    ULONG ac_Flags;
    ULONG *ac_ArgList;
    LONG ac_RC;
    LONG ac_RC2;
    STRPTR ac_Result;
};
typedef char fz_check_cmd[sizeof(struct fz_ARexxCmd) == 34 ? 1 : -1];

static struct {
    struct fz_ARexxCmd *table;
    fz_rexx_handler handler;
    void *user;
    const char *host;
    char *result;             /* the last command's, kept until the next */
    struct Library *base;
    Object *obj;
    struct Task *task;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t up;
    int state;                /* 0 starting, 1 up, -1 failed */
    long error;
    char name[40];
} rx;

/* arexx.class calls ac_Func with the command in A0 and the message in A1 */
static void fz_rexx_func(register struct fz_ARexxCmd *cmd __asm("a0"),
                         register APTR msg __asm("a1"))
{
    long rc2 = 0;
    char *res = NULL;
    (void)msg;
    free(rx.result);
    rx.result = NULL;
    cmd->ac_RC = rx.handler(rx.user, cmd->ac_ID, (long *)cmd->ac_ArgList, &rc2, &res);
    cmd->ac_RC2 = rc2;
    if (res) {
        rx.result = strdup(res);
        cmd->ac_Result = (STRPTR)rx.result;
    }
}

static void *fz_rexx_task(void *unused)
{
    ULONG mask = 0;
    (void)unused;
    rx.task = FindTask(NULL);
    rx.obj = NewObject(NULL, (STRPTR)"arexx.class",
                       FZ_AREXX_HostName, (ULONG)rx.host,
                       FZ_AREXX_Commands, (ULONG)rx.table,
                       FZ_AREXX_ErrorCode, (ULONG)&rx.error,
                       TAG_DONE);
    if (rx.obj) {
        STRPTR name = NULL;
        GetAttr(FZ_AREXX_HostName, rx.obj, (ULONG *)&name);
        GetAttr(FZ_AREXX_SigMask, rx.obj, &mask);
        if (name)
            strncpy(rx.name, (const char *)name, sizeof rx.name - 1);
    }
    pthread_mutex_lock(&rx.lock);
    rx.state = rx.obj ? 1 : -1;
    pthread_cond_signal(&rx.up);
    pthread_mutex_unlock(&rx.lock);
    if (!rx.obj)
        return NULL;
    for (;;) {
        ULONG got = Wait(mask | SIGBREAKF_CTRL_F);
        if (got & mask)
            DoMethod(rx.obj, FZ_AM_HANDLEEVENT);
        if (got & SIGBREAKF_CTRL_F)
            break;
    }
    /* answers what is still waiting (RC 20) and takes the port away */
    DisposeObject(rx.obj);
    rx.obj = NULL;
    free(rx.result);
    rx.result = NULL;
    return NULL;
}

long fz_rexx_start(const char *host, const struct fz_rexx_cmd *cmds, int count,
                   fz_rexx_handler handler, void *user, char *name, int len)
{
    int i;
    if (rx.task)
        return -2;
    rx.base = OpenLibrary((STRPTR)"arexx.class", 44);
    if (!rx.base)
        rx.base = OpenLibrary((STRPTR)"SYS:Classes/arexx.class", 44);
    if (!rx.base)
        return -1;
    rx.table = calloc(count + 1, sizeof *rx.table);
    if (!rx.table) {
        CloseLibrary(rx.base);
        rx.base = NULL;
        return 4;
    }
    for (i = 0; i < count; i++) {
        rx.table[i].ac_Name = (STRPTR)cmds[i].name;
        rx.table[i].ac_ID = (UWORD)i;
        rx.table[i].ac_Func = (void (*)(void))fz_rexx_func;
        rx.table[i].ac_ArgTemplate = (STRPTR)(cmds[i].tmpl && *cmds[i].tmpl ? cmds[i].tmpl : NULL);
    }
    rx.handler = handler;
    rx.user = user;
    rx.host = host;
    rx.state = 0;
    rx.error = 0;
    rx.name[0] = 0;
    pthread_mutex_init(&rx.lock, NULL);
    pthread_cond_init(&rx.up, NULL);
    if (pthread_create(&rx.thread, NULL, fz_rexx_task, NULL) != 0) {
        free(rx.table);
        rx.table = NULL;
        CloseLibrary(rx.base);
        rx.base = NULL;
        return -2;
    }
    pthread_mutex_lock(&rx.lock);
    while (rx.state == 0)
        pthread_cond_wait(&rx.up, &rx.lock);
    pthread_mutex_unlock(&rx.lock);
    if (rx.state < 0) {
        long err = rx.error ? rx.error : -2;
        pthread_join(rx.thread, NULL);
        rx.task = NULL;
        free(rx.table);
        rx.table = NULL;
        CloseLibrary(rx.base);
        rx.base = NULL;
        return err;
    }
    if (name && len > 0) {
        strncpy(name, rx.name, len - 1);
        name[len - 1] = 0;
    }
    return 0;
}

void fz_rexx_stop(void)
{
    if (!rx.task)
        return;
    Signal(rx.task, SIGBREAKF_CTRL_F);
    pthread_join(rx.thread, NULL);
    rx.task = NULL;
    free(rx.table);
    rx.table = NULL;
    CloseLibrary(rx.base);
    rx.base = NULL;
}

void fz_rexx_fault(long code, char *buf, int len)
{
    if (len <= 0)
        return;
    buf[0] = 0;
    Fault(code, NULL, (STRPTR)buf, len);
}
