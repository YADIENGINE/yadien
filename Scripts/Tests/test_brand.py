"""brand: the generated icon and splash are valid files of the expected sizes; the pixel font is well formed."""

import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from Scripts.brand import FONT, GLYPH_COLUMNS, GLYPH_ROWS, ICON_SIZES, SPLASH_SIZE, glyph, icon, write_brand


def png_size(data: bytes) -> tuple[int, int]:
    assert data.startswith(b"\x89PNG\r\n\x1a\n")
    width, height = struct.unpack(">II", data[16:24])
    return width, height


class BrandTests(unittest.TestCase):
    def test_every_glyph_is_five_by_seven(self):
        for character, rows in FONT.items():
            self.assertEqual(len(rows), GLYPH_ROWS, character)
            self.assertTrue(all(len(row) == GLYPH_COLUMNS and set(row) <= {".", "#"} for row in rows), character)

    def test_unknown_characters_fall_back_to_a_dash(self):
        self.assertEqual(glyph("é"), FONT["-"])
        self.assertEqual(glyph("y"), FONT["Y"])

    def test_icon_has_every_size_and_lit_pixels(self):
        with tempfile.TemporaryDirectory(prefix="Brand-") as temp:
            output = Path(temp)
            write_brand(output, "MyEngine")
            data = (output / "Icon.ico").read_bytes()
            _, kind, count = struct.unpack("<HHH", data[:6])
            self.assertEqual((kind, count), (1, len(ICON_SIZES)))
            for index, size in enumerate(ICON_SIZES):
                _, _, _, _, _, _, length, offset = struct.unpack("<BBBBHHII", data[6 + 16 * index:22 + 16 * index])
                self.assertEqual(png_size(data[offset:offset + length]), (size, size))
            self.assertEqual(png_size((output / "Splash.png").read_bytes()), SPLASH_SIZE)

    def test_the_letter_is_drawn_inside_the_tile(self):
        canvas = icon(64, "M")
        accent = sum(1 for row in canvas.pixels for r, g, b, a in row if a > 0.9 and r > 200 and g > 130)
        self.assertGreater(accent, 64)
        self.assertEqual(canvas.pixels[0][0][3], 0.0)  # Corners stay transparent (rounded tile)

    def test_png_stream_decompresses(self):
        data = icon(16, "A").png()
        start = data.index(b"IDAT") + 4
        length = struct.unpack(">I", data[start - 8:start - 4])[0]
        self.assertEqual(len(zlib.decompress(data[start:start + length])), 16 * (16 * 4 + 1))


if __name__ == "__main__":
    unittest.main()
