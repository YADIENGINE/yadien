"""Draws the launcher's icon and installer splash from code (engine design D1: visuals come from code).

Usage, from the launcher repository:
    python Scripts/brand.py OUTPUT_FOLDER --name NAME

Writes Icon.ico (16-256 px, PNG entries), Icon.png (256 px preview) and Splash.png (the installer's splash). The
product name comes from the engine manifest (the build passes it), so nothing here names the product: the icon is the
name's first letter in a 5x7 arcade pixel font, the splash spells the whole name. Colours are the tool UI's palette
(engine ToolUi/ToolTheme.cpp, "Arcade Cabinet"). Only the standard library: shapes are signed distance functions with
one-pixel antialiasing, PNG and ICO are written by hand. The build (Apps/CMakeLists.txt) and Scripts/package.py run it;
its output is never committed.
"""

import argparse
import math
import struct
import sys
import zlib
from pathlib import Path

ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)
SPLASH_SIZE = (600, 320)

# ToolPalette (engine ToolUi/ToolTheme.cpp)
PANEL = (0x2C, 0x26, 0x33)
CONTENT = (0x16, 0x13, 0x1B)
BORDER = (0x5A, 0x4C, 0x65)
ACCENT_LIGHT = (0xFF, 0xC1, 0x83)
ACCENT_DARK = (0xDE, 0x98, 0x50)
SPLASH_PROGRESS = "#F4AF65"  # ToolPalette::Accent, for vpk --splashProgressColor

# 5x7 pixel font: rows top to bottom, '#' = lit.
FONT = {
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "B": ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
    "C": [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
    "D": ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "G": [".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."],
    "H": ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "I": [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "J": ["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."],
    "K": ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "M": ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
    "N": ["#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    "Q": [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "V": ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
    "W": ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."],
    "X": ["#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"],
    "Y": ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
    "Z": ["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
    "0": [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."],
    "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    "3": ["####.", "....#", "....#", ".###.", "....#", "....#", "####."],
    "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
    "5": ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
    "6": [".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."],
    "7": ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
    "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
    "9": [".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."],
    "-": [".....", ".....", ".....", "#####", ".....", ".....", "....."],
    " ": [".....", ".....", ".....", ".....", ".....", ".....", "....."],
}
GLYPH_COLUMNS, GLYPH_ROWS = 5, 7


def glyph(character: str) -> list[str]:
    return FONT.get(character.upper(), FONT["-"])


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(len(a)))


def coverage(distance: float) -> float:
    """Antialiased coverage of a shape whose signed distance (in pixels) is `distance`."""
    return min(1.0, max(0.0, 0.5 - distance))


def rounded_box(x, y, cx, cy, half_w, half_h, radius) -> float:
    qx = abs(x - cx) - (half_w - radius)
    qy = abs(y - cy) - (half_h - radius)
    outside = math.hypot(max(qx, 0.0), max(qy, 0.0))
    return outside + min(max(qx, qy), 0.0) - radius


class Canvas:
    """Premultiplied-free RGBA float canvas; `over` composites a colour with a coverage."""

    def __init__(self, width: int, height: int):
        self.width, self.height = width, height
        self.pixels = [[(0.0, 0.0, 0.0, 0.0)] * width for _ in range(height)]

    def over(self, x: int, y: int, colour, alpha: float) -> None:
        if alpha <= 0.0:
            return
        r, g, b, a = self.pixels[y][x]
        out_a = alpha + a * (1.0 - alpha)
        blend = lambda dst, src: (src * alpha + dst * a * (1.0 - alpha)) / out_a  # noqa: E731
        self.pixels[y][x] = (blend(r, colour[0]), blend(g, colour[1]), blend(b, colour[2]), out_a)

    def png(self) -> bytes:
        rows = bytearray()
        for row in self.pixels:
            rows.append(0)
            for r, g, b, a in row:
                rows += bytes((round(r), round(g), round(b), round(a * 255)))

        def chunk(kind: bytes, data: bytes) -> bytes:
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

        header = struct.pack(">IIBBBBB", self.width, self.height, 8, 6, 0, 0, 0)
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) +
                chunk(b"IEND", b""))


def draw_tile(canvas: Canvas, left: float, top: float, size: float) -> None:
    """The rounded dark tile with a thin border: the icon's background."""
    cx, cy, half = left + size / 2, top + size / 2, size * 0.47
    radius, border = size * 0.21, max(1.0, size / 48)
    for y in range(max(0, int(top)), min(canvas.height, math.ceil(top + size))):
        shade = mix(PANEL, CONTENT, min(1.0, max(0.0, (y + 0.5 - top) / size)))
        for x in range(max(0, int(left)), min(canvas.width, math.ceil(left + size))):
            d = rounded_box(x + 0.5, y + 0.5, cx, cy, half, half, radius)
            canvas.over(x, y, shade, coverage(d))
            canvas.over(x, y, BORDER, coverage(d) - coverage(d + border))


def draw_text(canvas: Canvas, text: str, left: float, top: float, cell: float, gap: float, round_blocks: bool) -> None:
    """Pixel-font text; each lit font pixel is a block of `cell` px, letters `gap` cells apart."""
    advance = (GLYPH_COLUMNS + gap) * cell
    height = GLYPH_ROWS * cell
    width = len(text) * advance - gap * cell
    half = cell * (0.44 if round_blocks else 0.5)
    radius = cell * 0.2 if round_blocks else 0.0
    for y in range(max(0, int(top)), min(canvas.height, math.ceil(top + height))):
        colour = mix(ACCENT_LIGHT, ACCENT_DARK, min(1.0, max(0.0, (y + 0.5 - top) / height)))
        row = int((y + 0.5 - top) // cell)
        for x in range(max(0, int(left)), min(canvas.width, math.ceil(left + width))):
            index = int((x + 0.5 - left) // advance)
            column = int((x + 0.5 - left - index * advance) // cell)
            best = 1e9
            for dr in (-1, 0, 1):  # neighbouring font pixels can reach this pixel through antialiasing
                for dc in (-1, 0, 1):
                    r, c = row + dr, column + dc
                    if 0 <= r < GLYPH_ROWS and 0 <= c < GLYPH_COLUMNS and index < len(text):
                        if glyph(text[index])[r][c] == "#":
                            bx = left + index * advance + (c + 0.5) * cell
                            by = top + (r + 0.5) * cell
                            best = min(best, rounded_box(x + 0.5, y + 0.5, bx, by, half, half, radius))
            canvas.over(x, y, colour, coverage(best))


def icon(size: int, letter: str) -> Canvas:
    canvas = Canvas(size, size)
    draw_tile(canvas, 0, 0, size)
    small = size <= 32  # Taskbar / title bar sizes: whole-pixel, gapless blocks stay crisp
    cell = size * 0.105
    left = (size - GLYPH_COLUMNS * cell) / 2
    top = (size - GLYPH_ROWS * cell) / 2
    if small:
        cell = max(2, math.floor(size * 0.7 / GLYPH_ROWS))
        left, top = (size - GLYPH_COLUMNS * cell) // 2, (size - GLYPH_ROWS * cell) // 2
    draw_text(canvas, letter, left, top, cell, 0, round_blocks=not small)
    return canvas


def splash(name: str) -> Canvas:
    width, height = SPLASH_SIZE
    canvas = Canvas(width, height)
    for y in range(height):
        shade = mix(PANEL, CONTENT, y / height)
        scanline = 0.92 if y % 3 == 0 else 1.0  # Arcade screen lines
        for x in range(width):
            canvas.pixels[y][x] = (shade[0] * scanline, shade[1] * scanline, shade[2] * scanline, 1.0)
            if x == 0 or y == 0 or x == width - 1 or y == height - 1:
                canvas.pixels[y][x] = (*BORDER, 1.0)
    tile = 104
    draw_tile(canvas, (width - tile) / 2, 44, tile)
    letter = icon(tile, name[:1])
    for y in range(tile):
        for x in range(tile):
            r, g, b, a = letter.pixels[y][x]
            canvas.over(int((width - tile) / 2) + x, 44 + y, (r, g, b), a)
    cell = min(9.0, (width - 80) / (len(name) * 6))
    text_width = len(name) * 6 * cell - cell
    draw_text(canvas, name, (width - text_width) / 2, 44 + tile + 36, cell, 1, round_blocks=True)
    return canvas


def ico(images: list[tuple[int, bytes]]) -> bytes:
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = len(header) + 16 * len(images)
    entries, data = b"", b""
    for size, png in images:
        dimension = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", dimension, dimension, 0, 0, 1, 32, len(png), offset + len(data))
        data += png
    return header + entries + data


def write_brand(output: Path, name: str) -> None:
    if not name:
        raise ValueError("the product name is empty")
    output.mkdir(parents=True, exist_ok=True)
    pngs = [(size, icon(size, name[0]).png()) for size in ICON_SIZES]
    (output / "Icon.ico").write_bytes(ico(pngs))
    (output / "Icon.png").write_bytes(pngs[-1][1])
    (output / "Splash.png").write_bytes(splash(name).png())


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Draw the launcher's icon and installer splash")
    parser.add_argument("output", type=Path)
    parser.add_argument("--name", required=True, help="product name (engine manifest)")
    args = parser.parse_args(argv)
    write_brand(args.output, args.name)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
