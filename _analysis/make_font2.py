"""Merriweather Bold -> TTF -> src/Fonts.h (embedded, single-file plugin)."""
import io
import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "fonts")
HDR = os.path.join(HERE, "..", "src", "Fonts.h")
os.makedirs(OUT, exist_ok=True)

WOFF2 = os.path.join(OUT, "merriweather-latin-700.woff2")
TTF = os.path.join(OUT, "Merriweather-Bold.ttf")

if not os.path.exists(WOFF2):
    url = "https://cdn.jsdelivr.net/npm/@fontsource/merriweather@5.3.0/files/merriweather-latin-700-normal.woff2"
    print("download", url)
    urllib.request.urlretrieve(url, WOFF2)
print("woff2:", os.path.getsize(WOFF2), "bytes")

from fontTools.ttLib import TTFont  # noqa: E402

font = TTFont(WOFF2)
font.flavor = None            # decompress to a plain TTF
font.save(TTF)
size = os.path.getsize(TTF)
print("ttf:", size, "bytes")

data = open(TTF, "rb").read()
with io.open(HDR, "w", encoding="utf-8", newline="\n") as f:
    f.write("// Merriweather Bold, latin subset (SIL Open Font License 1.1).\n")
    f.write("// Embedded so the plugin stays a single file; see _analysis/fonts/OFL.txt.\n")
    f.write("#pragma once\n\n")
    f.write("namespace mel {\n\n")
    f.write("static const unsigned int kDisplayFontBytes = %d;\n" % len(data))
    f.write("static const unsigned char kDisplayFont[%d] = {\n" % len(data))
    for i in range(0, len(data), 24):
        f.write("    " + ",".join(str(b) for b in data[i:i + 24]) + ",\n")
    f.write("};\n\n} // namespace mel\n")
print("wrote", HDR, os.path.getsize(HDR), "bytes")

# keep the licence next to the build for reference
OFL = os.path.join(OUT, "OFL.txt")
if not os.path.exists(OFL):
    urllib.request.urlretrieve(
        "https://raw.githubusercontent.com/google/fonts/main/ofl/merriweather/OFL.txt", OFL)
print("licence:", os.path.getsize(OFL), "bytes")
