FileZilla 0.1.0-rc1 for AmigaOS 3
=================================

FileZilla 3.71.0, the FTP, FTPS and SFTP client, for AmigaOS 3.2 on a
68040 or 68060 (it uses the FPU when there is one). Built on the GFOpenWX
Toolkit.

This is a release candidate: the program runs and browses your volumes;
transfers have not been tested yet.

Installing: double-click Install-FileZilla and choose where the FileZilla
drawer goes. FileZilla needs z.library and gfopenpng.datatype, which
GFOpenUp installs. Its settings are kept in Settings/ in its drawer.

ARexx: while it runs, FileZilla has the port FILEZILLA.1 (arexx.class,
AmigaOS 3.2). Commands, with their templates:
  CONNECT URL/A,PORT/K/N,USER/K,PASSWORD/K,TIMEOUT/K/N   waits until logged in
  DISCONNECT
  CD PATH/A,TIMEOUT/K/N              RESULT: the new directory
  LIST LONG/S,TIMEOUT/K/N            RESULT: one entry a line, directories end in /
  GET FILE/A,TO/K,OVERWRITE/S,NOWAIT/S,TIMEOUT/K/N   into TO, else the
                                     local directory
  PUT FILE/A,OVERWRITE/S,NOWAIT/S,TIMEOUT/K/N   into the remote directory
  STATUS                             RESULT: CONNECTED IDLE url directory
  QUIT FORCE/S, ACTIVATE, DEACTIVATE, VERSION, HELP COMMAND, FAULT CODE/N/A
Commands that talk to the server wait 60 seconds at most (TIMEOUT changes
it); transfers wait until they end. GET and PUT refuse a target that is
there already unless OVERWRITE is given. A server FileZilla does not know
yet asks in its window whether to trust its key, and CONNECT waits for
that answer. RC is 0 when it worked, 5 a warning,
10 an error with its number in RC2 (FAULT gives the number's text), 20
when FileZilla is closing. RESULT only when the script sets OPTIONS
RESULTS. Rexx/FZ-Get.rexx is an example.

FileZilla and libfilezilla are GPL version 2 or later. This software
incorporates components from fzssh, copyright (c) 2025-2026 Tim Kosse and
Business Follows Srl, which is AGPL version 3, so the program as a whole is
under the GNU Affero General Public License version 3. Source:
https://github.com/DalsinAI/gfamigafilezilla

Designed, tested and delivered by The AmiCradle Team.
Copyright (c) 2026 AmiCradle Team
