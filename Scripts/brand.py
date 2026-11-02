"""Draws the launcher's icon and installer splash from code (engine design D1: visuals come from code).

Usage, from the launcher repository:
    python Scripts/brand.py OUTPUT_FOLDER --name NAME --version VERSION

Writes Icon.ico (16-256 px, PNG entries), Icon.png (256 px preview) and Splash.png (the installer's splash). The product
name comes from the engine manifest and the version from the launcher manifest (the build passes both), so nothing here
names the product. The look follows engine design D23: flat colours and square cells of the tool UI's 5x7 font
(engine ToolUi/ToolPixel.cpp), on whole pixels. The icon is the name's first letter in the primary button's colours; the
splash repeats the launcher's first-run screen, so setup and the launcher read as one thing. Only the standard library:
PNG and ICO are written by hand. The build (Apps/CMakeLists.txt) and Scripts/package.py run it; its output is never
committed.
"""

import argparse
import struct
import sys
import zlib
from pathlib import Path

ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)
SPLASH_SIZE = (600, 320)
TAGLINE = "C++ 2D GAME ENGINE"

# ToolPalette (engine ToolUi/ToolTheme.cpp)
WINDOW = (0x0F, 0x0E, 0x0D)
PANEL = (0x1C, 0x1A, 0x18)
ACCENT = (0xF4, 0xAF, 0x65)
ACCENT_TEXT = (0x2A, 0x1A, 0x0A)
SECONDARY = (0xAC, 0xA6, 0x9D)
SPLASH_PROGRESS = "#F4AF65"  # ToolPalette::Accent, for vpk --splashProgressColor

# Splash layout, in pixels. The bottom stays clear for Velopack's progress bar.
SPLASH_MARGIN = 32
SPLASH_GHOST_CELL = 36
SPLASH_GHOST_TOP = 34
SPLASH_NAME_CELL = 8
SPLASH_NAME_TOP = 208
SPLASH_TEXT_CELL = 2
SPLASH_TEXT_TOP = 276

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
    "+": [".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."],
    ".": [".....", ".....", ".....", ".....", ".....", "..#..", "..#.."],
    " ": [".....", ".....", ".....", ".....", ".....", ".....", "....."],
}
GLYPH_COLUMNS, GLYPH_ROWS = 5, 7


def glyph(character: str) -> list[str]:
    return FONT.get(character.upper(), FONT["-"])


class Canvas:
    """An RGBA canvas of whole-pixel rectangles; alpha is either 0 or 255."""

    def __init__(self, width: int, height: int):
        self.width, self.height = width, height
        self.pixels = [[(0, 0, 0, 0)] * width for _ in range(height)]

    def rect(self, left: int, top: int, width: int, height: int, colour) -> None:
        """Fills a rectangle, clipped to the canvas; `colour` None makes it transparent."""
        pixel = (*colour, 255) if colour else (0, 0, 0, 0)
        for y in range(max(0, top), min(self.height, top + height)):
            for x in range(max(0, left), min(self.width, left + width)):
                self.pixels[y][x] = pixel

    def png(self) -> bytes:
        rows = bytearray()
        for row in self.pixels:
            rows.append(0)
            for pixel in row:
                rows += bytes(pixel)

        def chunk(kind: bytes, data: bytes) -> bytes:
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

        header = struct.pack(">IIBBBBB", self.width, self.height, 8, 6, 0, 0, 0)
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) +
                chunk(b"IEND", b""))


def draw_text(canvas: Canvas, text: str, left: int, top: int, cell: int, colour) -> None:
    """Pixel-font text; each lit font pixel is a square of `cell` px, letters one cell apart."""
    for index, character in enumerate(text):
        for row, bits in enumerate(glyph(character)):
            for column, bit in enumerate(bits):
                if bit == "#":
                    x = left + index * (GLYPH_COLUMNS + 1) * cell + column * cell
                    canvas.rect(x, top + row * cell, cell, cell, colour)


def icon(size: int, letter: str) -> Canvas:
    canvas = Canvas(size, size)
    canvas.rect(0, 0, size, size, ACCENT)
    notch = max(1, round(size / 16))  # Corners are one stepped pixel, in the font's own language
    for x in (0, size - notch):
        for y in (0, size - notch):
            canvas.rect(x, y, notch, notch, None)
    cell = size // (GLYPH_ROWS + 1)
    left, top = (size - GLYPH_COLUMNS * cell) // 2, (size - GLYPH_ROWS * cell) // 2
    draw_text(canvas, letter, left, top, cell, ACCENT_TEXT)
    return canvas


def splash(name: str, version: str) -> Canvas:
    width, height = SPLASH_SIZE
    canvas = Canvas(width, height)
    canvas.rect(0, 0, width, height, WINDOW)
    ghost_left = width - SPLASH_MARGIN - GLYPH_COLUMNS * SPLASH_GHOST_CELL
    draw_text(canvas, name[:1], ghost_left, SPLASH_GHOST_TOP, SPLASH_GHOST_CELL, PANEL)
    # A long name gets smaller cells instead of leaving the splash.
    advance_cells = len(name) * (GLYPH_COLUMNS + 1) - 1
    cell = max(1, min(SPLASH_NAME_CELL, (width - 2 * SPLASH_MARGIN) // advance_cells))
    draw_text(canvas, name, SPLASH_MARGIN, SPLASH_NAME_TOP, cell, ACCENT)
    draw_text(canvas, f"{TAGLINE}    {version}", SPLASH_MARGIN, SPLASH_TEXT_TOP, SPLASH_TEXT_CELL, SECONDARY)
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


def write_brand(output: Path, name: str, version: str) -> None:
    if not name:
        raise ValueError("the product name is empty")
    output.mkdir(parents=True, exist_ok=True)
    pngs = [(size, icon(size, name[0]).png()) for size in ICON_SIZES]
    (output / "Icon.ico").write_bytes(ico(pngs))
    (output / "Icon.png").write_bytes(pngs[-1][1])
    (output / "Splash.png").write_bytes(splash(name, version).png())


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Draw the launcher's icon and installer splash")
    parser.add_argument("output", type=Path)
    parser.add_argument("--name", required=True, help="product name (engine manifest)")
    parser.add_argument("--version", required=True, help="launcher version (launcher manifest)")
    args = parser.parse_args(argv)
    write_brand(args.output, args.name, args.version)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
