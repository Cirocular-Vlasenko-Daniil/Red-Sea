#!/usr/bin/env python3
"""PBM 128×32 → PNG ×4 с полями, «как на OLED». Без внешних зависимостей."""
import struct, sys, zlib, pathlib

SCALE, PAD = 4, 8
ON, OFF = (236, 240, 242), (12, 13, 15)

def png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    pathlib.Path(path).write_bytes(data)

for src in sys.argv[1:]:
    lines = pathlib.Path(src).read_text().split("\n")[2:34]
    W, H = 128 * SCALE + 2 * PAD, 32 * SCALE + 2 * PAD
    rows = []
    for y in range(H):
        sy = (y - PAD) // SCALE
        row = []
        for x in range(W):
            sx = (x - PAD) // SCALE
            lit = 0 <= sx < 128 and 0 <= sy < 32 and lines[sy][sx] == "1"
            row += ON if lit else OFF
        rows.append(row)
    png(src[:-4] + ".png", W, H, rows)
