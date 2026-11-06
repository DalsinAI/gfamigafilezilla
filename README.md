# GF Amiga FileZilla

FileZilla 3.71.0 for AmigaOS 3 (68040; the FPU when there is one), on the GFOpenWX Toolkit.

Work in progress: it builds and links; it has not yet been run on an Amiga.

## What is in it

- FileZilla 3.71.0, libfilezilla 0.57.0 and fzssh 1.4.0 (SFTP), with
  all static, on our own shared libraries: GFOpenTLS (gfopentls.library 1.5)
  for FTPS, GFOpenCrypto (gfopencrypto.library 2.4) for SFTP and the rest of
  the cryptography, and GFOpenSQLite (SQLite 3.53.4).
- The GFOpenWX Toolkit (wxWidgets on GFOpenLayout) at the commit
  `filezilla.lock` names.
- Our own code for what libnix lacks (`deps/amiga`): getentropy, socket
  helpers, iconv, IDNA (Punycode), directory descriptors, wordexp,
  detached AmigaDOS commands and a few more.
- Patches in `patches/`, one change each, applied at build time.

None of the upstream sources are kept here: `build-amiga.sh` fetches them
at the versions and checksums in `filezilla.lock`.

## Building

With a Cradle stove (os32-gcc16-open6):

    STOVE=.../stoves/os32-gcc16-open6 OUT=out ./build-amiga.sh

The build tree lives outside the checkout (`~/.cache/gfamigafilezilla`),
and a run that runs out of time carries on where it stopped the next time.

## Licences

FileZilla and libfilezilla are GPL version 2 or later. This software
incorporates components from fzssh, copyright (c) 2025-2026 Tim Kosse and
Business Follows Srl, which is AGPL version 3, so the program as a whole is
under the GNU Affero General Public License version 3. Our own code here is
GPL version 3 (see LICENSE).

Designed, tested and delivered by The AmiCradle Team.
Copyright (c) 2026 AmiCradle Team
