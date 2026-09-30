#!/usr/bin/env python3
"""Render an image as ASCII art so the component layout can be read in a
terminal. Usage:

    tools/img2ascii.py <image> [cols] [--edge]

Plain mode maps luminance to a ramp; --edge runs a Sobel edge filter first,
which makes component outlines stand out better. Useful to sanity-check the
interface layout of a device photo before drawing an illustration.
"""
import sys
from PIL import Image, ImageFilter, ImageOps

RAMP = " .:-=+*#%@"


def main(path, cols=110, edges=False):
    im = Image.open(path).convert("L")
    if edges:
        im = ImageOps.autocontrast(im.filter(ImageFilter.FIND_EDGES))
    w, h = im.size
    rows = max(1, int(h / w * cols * 0.5))  # chars are ~2x as tall as wide
    im = im.resize((cols, rows))
    px = im.load()
    for y in range(rows):
        print("".join(RAMP[min(9, px[x, y] * 10 // 256)] for x in range(cols)))


if __name__ == "__main__":
    cols = 110
    edges = False
    args = [a for a in sys.argv[1:] if a != "--edge"]
    if "--edge" in sys.argv:
        edges = True
    if len(args) > 1:
        cols = int(args[1])
    main(args[0], cols, edges)