#!/usr/bin/env python3
# Writes FileZilla's Workbench icons (classic DiskObjects, 4 colours):
#   python3 make-icon.py tool OUT/FileZilla.info
#       the program: a double-click runs it with the stack it needs
#   python3 make-icon.py install OUT/Install-FileZilla.info
#       the installer script: a double-click runs it with Installer
# Copyright (c) 2026 AmiCradle Team
import struct
import sys

W, H, DEPTH = 48, 26, 2
STACK = 1048576
WBTOOL, WBPROJECT = 3, 4

# 0 grey, 1 black, 2 white, 3 blue (the Workbench pens)
def picture():
    px = [[0] * W for _ in range(H)]
    def box(x0, y0, x1, y1):
        for x in range(x0, x1 + 1):
            px[y0][x] = 2
            px[y1][x] = 1
        for y in range(y0, y1 + 1):
            px[y][x0] = 2
            px[y][x1] = 1
    def fill(x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                px[y][x] = c
    # two folders, local and remote
    fill(4, 6, 18, 20, 3); box(4, 6, 18, 20); fill(4, 4, 10, 5, 3)
    fill(29, 6, 43, 20, 3); box(29, 6, 43, 20); fill(29, 4, 35, 5, 3)
    # arrows between them: right on top, left below
    for x in range(20, 28):
        px[10][x] = 1
        px[16][x] = 1
    for d in range(1, 3):
        px[10 - d][27 - d] = 1; px[10 + d][27 - d] = 1
        px[16 - d][20 + d] = 1; px[16 + d][20 + d] = 1
    return px

def planes(px):
    row_words = (W + 15) // 16
    out = b""
    for plane in range(DEPTH):
        for y in range(H):
            bits = 0
            for x in range(row_words * 16):
                bit = (px[y][x] >> plane) & 1 if x < W else 0
                bits = (bits << 1) | bit
            out += bits.to_bytes(row_words * 2, "big")
    return out

def main():
    kind, path = sys.argv[1], sys.argv[2]
    data = planes(picture())
    tool = b"SYS:Utilities/Installer\0" if kind == "install" else b""
    gadget = struct.pack(">IhhHHHHHIIIIIHI",
                         0, 0, 0, W, H + 1,
                         0x0004,          # GFLG_GADGIMAGE, complement when selected
                         0x0003,          # RELVERIFY | GADGIMMEDIATE
                         0x0001,          # BOOLGADGET
                         1, 0, 0, 0, 0, 0,
                         1)               # UserData 1: a 2.x icon
    disk = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(">BBIIiiIII",
                         WBPROJECT if tool else WBTOOL, 0,
                         1 if tool else 0, 0,     # default tool, no tool types
                         -0x80000000, -0x80000000,   # NO_ICON_POSITION
                         0, 0, STACK)
    assert len(disk) == 78, len(disk)
    image = struct.pack(">hhHHHIBBI", 0, 0, W, H, DEPTH, 1, 3, 0, 0)
    extra = struct.pack(">I", len(tool)) + tool if tool else b""
    with open(path, "wb") as f:
        f.write(disk + image + data + extra)

if __name__ == "__main__":
    main()
