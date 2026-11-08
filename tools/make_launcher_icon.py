"""Draws the exe's fallback icon (launcher/launcher.ico): the same round gunmetal badge the launcher cuts from the imported key art,
with the key art's teal light slit on black instead of the art. Original drawing; the exe never ships game art (at run time the
window icon is replaced by the badge made from the player's own install, MainForm.ApplyArtIcon).
Usage: python tools/make_launcher_icon.py"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

N = 1024
RIM, FACE = 52, 68


def disc(inset):
    m = Image.new('L', (N, N), 0)
    ImageDraw.Draw(m).ellipse((inset, inset, N - 1 - inset, N - 1 - inset), fill=255)
    return m


def draw():
    out = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    ring = Image.new('RGB', (N, N))
    d = ImageDraw.Draw(ring)
    for y in range(N):
        c = int(200 - 154 * y / N)
        d.line((0, y, N, y), fill=(c, c + 4, c + 8))
    out.paste(ring, (0, 0), disc(8))
    out.paste((20, 22, 24), (0, 0), disc(RIM))
    # black field with a faint cold haze around the slit
    field = Image.new('RGB', (N, N), (6, 8, 10))
    haze = Image.new('L', (N, N), 0)
    ImageDraw.Draw(haze).rectangle((N // 2 - 90, 0, N // 2 + 90, N), fill=70)
    field = Image.composite(Image.new('RGB', (N, N), (30, 70, 80)), field, haze.filter(ImageFilter.GaussianBlur(80)))
    # the light slit: soft glow plus a hot core, tapering at both ends
    glow = Image.new('L', (N, N), 0)
    ImageDraw.Draw(glow).polygon([(N // 2, 110), (N // 2 + 14, N // 2), (N // 2, N - 110), (N // 2 - 14, N // 2)], fill=255)
    field = Image.composite(Image.new('RGB', (N, N), (90, 200, 215)), field, glow.filter(ImageFilter.GaussianBlur(22)))
    core = Image.new('L', (N, N), 0)
    ImageDraw.Draw(core).polygon([(N // 2, 150), (N // 2 + 5, N // 2), (N // 2, N - 150), (N // 2 - 5, N // 2)], fill=255)
    field = Image.composite(Image.new('RGB', (N, N), (235, 255, 255)), field, core.filter(ImageFilter.GaussianBlur(2)))
    out.paste(field, (0, 0), disc(FACE))
    ImageDraw.Draw(out).ellipse((FACE - 8, FACE - 8, N - 1 - FACE + 8, N - 1 - FACE + 8), outline=(110, 200, 210), width=8)
    return out


if __name__ == '__main__':
    out = Path(__file__).resolve().parent.parent / 'launcher' / 'launcher.ico'
    draw().save(out, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print(out)
