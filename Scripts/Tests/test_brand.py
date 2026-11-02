"""brand: the generated icon and splash are valid files of the expected sizes; the pixel font is well formed."""

import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from Scripts.brand import (ACCENT, FONT, GLYPH_COLUMNS, GLYPH_ROWS, ICON_SIZES, SPLASH_SIZE, glyph, icon, splash,
                           write_brand)


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
            write_brand(output, "MyEngine", "1.2.3")
            data = (output / "Icon.ico").read_bytes()
            _, kind, count = struct.unpack("<HHH", data[:6])
            self.assertEqual((kind, count), (1, len(ICON_SIZES)))
            for index, size in enumerate(ICON_SIZES):
                _, _, _, _, _, _, length, offset = struct.unpack("<BBBBHHII", data[6 + 16 * index:22 + 16 * index])
                self.assertEqual(png_size(data[offset:offset + length]), (size, size))
            self.assertEqual(png_size((output / "Splash.png").read_bytes()), SPLASH_SIZE)

    def test_the_letter_stays_inside_the_plate(self):
        for size in ICON_SIZES:
            canvas = icon(size, "M")
            self.assertEqual(canvas.pixels[0][0][3], 0, size)  # Stepped corner
            plate = canvas.pixels[0][size // 2]
            ink = {pixel for row in canvas.pixels for pixel in row} - {plate, (0, 0, 0, 0)}
            self.assertEqual(len(ink), 1, size)
            edges = [*canvas.pixels[0], *canvas.pixels[-1], *(row[0] for row in canvas.pixels),
                     *(row[-1] for row in canvas.pixels)]
            self.assertFalse(ink & set(edges), size)

    def test_every_icon_size_is_flat(self):
        # Design D23: no gradients or antialiasing, so small sizes stay crisp.
        for size in ICON_SIZES:
            colours = {pixel for row in icon(size, "A").pixels for pixel in row}
            self.assertEqual(len(colours), 3, size)

    def test_a_long_name_stays_on_the_splash(self):
        canvas = splash("A" * 40, "1.2.3")
        self.assertEqual((canvas.width, canvas.height), SPLASH_SIZE)
        accent_columns = {x for row in canvas.pixels for x, pixel in enumerate(row) if pixel[:3] == ACCENT}
        self.assertLess(max(accent_columns), SPLASH_SIZE[0] - 1)

    def test_the_splash_shows_the_version(self):
        self.assertNotEqual(splash("MyEngine", "1.2.3").png(), splash("MyEngine", "1.2.4").png())

    def test_png_stream_decompresses(self):
        data = icon(16, "A").png()
        start = data.index(b"IDAT") + 4
        length = struct.unpack(">I", data[start - 8:start - 4])[0]
        self.assertEqual(len(zlib.decompress(data[start:start + length])), 16 * (16 * 4 + 1))


if __name__ == "__main__":
    unittest.main()
