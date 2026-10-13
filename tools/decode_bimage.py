"""Decode a raw Dishonored 2 bimage7 mip (20-byte header + BCn blocks) to PNG via a DDS wrapper.

Usage: python tools/decode_bimage.py <mip file> <out.png> [bc7|bc3|bc1] [width]
Width defaults to sqrt of the block payload (square textures).
"""
import io, struct, sys
from PIL import Image

DXGI = {'bc1': (71, 8), 'bc3': (77, 16), 'bc7': (98, 16)}


def decode(path, fmt='bc7', width=None):
    data = open(path, 'rb').read()[20:]
    dxgi, block = DXGI[fmt]
    if width is None:
        width = int(round((len(data) // block) ** .5)) * 4
    height = len(data) // block // (width // 4) * 4
    dds = b'DDS ' + struct.pack('<IIIIIII', 124, 0x1007, height, width, len(data), 0, 1) + b'\0' * 44 + \
        struct.pack('<II4sIIIII', 32, 4, b'DX10', 0, 0, 0, 0, 0) + struct.pack('<IIIII', 0x1000, 0, 0, 0, 0) + \
        struct.pack('<IIIII', dxgi, 3, 0, 1, 0) + data
    return Image.open(io.BytesIO(dds)).convert('RGBA')


if __name__ == '__main__':
    img = decode(sys.argv[1], *(sys.argv[3:4] or ['bc7']), *(int(a) for a in sys.argv[4:5]))
    img.save(sys.argv[2]); print(img.size)
