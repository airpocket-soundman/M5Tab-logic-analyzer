#!/usr/bin/env python3
"""Convert the BMPs emitted by shotgen into PNGs for the documentation site.

Usage:  python sim/tools/to_png.py <bmp-dir> <png-dir>
"""

import pathlib
import sys

from PIL import Image


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    src = pathlib.Path(argv[1])
    dst = pathlib.Path(argv[2])
    dst.mkdir(parents=True, exist_ok=True)

    count = 0
    for bmp in sorted(src.glob("*.bmp")):
        img = Image.open(bmp).convert("RGB")
        out = dst / (bmp.stem + ".png")
        img.save(out, "PNG", optimize=True)
        print(f"{out}  {img.width}x{img.height}  {out.stat().st_size // 1024} kB")
        count += 1
    if count == 0:
        print("no BMPs found", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
