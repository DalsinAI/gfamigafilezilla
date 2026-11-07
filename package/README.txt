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
  CONNECT URL/A,PORT/K/N,USER/K,PASSWORD/K   waits until logged in
  DISCONNECT
  CD PATH/A                                  RESULT: the new directory
  LIST LONG/S                                RESULT: one entry a line,
                                             directories end in /
  GET FILE/A,TO/K,NOWAIT/S                   into TO, else the local directory
  PUT FILE/A,NOWAIT/S                        into the remote directory
  STATUS                                     RESULT: CONNECTED IDLE url dir
  QUIT FORCE/S, ACTIVATE, DEACTIVATE, VERSION, HELP COMMAND, FAULT CODE/A/N
RC is 0 when it worked, 5 a warning, 10 an error with its number in RC2;
FAULT gives the number's text. RESULT only when the script sets OPTIONS
RESULTS. Rexx/FZ-Get.rexx is an example.

FileZilla and libfilezilla are GPL version 2 or later. This software
incorporates components from fzssh, copyright (c) 2025-2026 Tim Kosse and
Business Follows Srl, which is AGPL version 3, so the program as a whole is
under the GNU Affero General Public License version 3. Source:
https://github.com/DalsinAI/gfamigafilezilla

Designed, tested and delivered by The AmiCradle Team.
Copyright (c) 2026 AmiCradle Team
