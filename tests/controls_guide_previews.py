"""Convert native check raw 1920x1200 RGBA previews to lossless PNGs (stdlib only)."""
import pathlib
import struct
import sys
import zlib


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


folder = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "artifacts/guide-pages")
for source in sorted(folder.glob("*.rgba")):
    rgba = source.read_bytes()
    width, height = 1920, 1200
    assert len(rgba) == width * height * 4, source
    scanlines = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b"")
    target = source.with_suffix(".png")
    target.write_bytes(png)
    print(target.resolve())
