#!/usr/bin/env python3
"""One-off: lift the article bodies out of the hand-written docs/index.html
into content_ja.py, so the Japanese manual becomes the generator's input
instead of being maintained twice.

Usage:  python docs_src/extract_ja.py
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "docs" / "index.html"
DST = ROOT / "docs_src" / "content_ja.py"

ART = re.compile(
    r'<article data-page="(?P<page>[^"]+)" data-section="(?P<sec>[^"]+)" '
    r'data-title="(?P<title>[^"]+)">\s*(?P<body>.*?)\s*</article>',
    re.S,
)


def main():
    html = SRC.read_text(encoding="utf-8")
    out = [
        '# Generated from the original hand-written docs/index.html by',
        '# docs_src/extract_ja.py, then maintained here.',
        '',
        'SECTIONS = [',
        '    ("start", "はじめに"),',
        '    ("use", "使う"),',
        '    ("api", "API"),',
        '    ("how", "仕組み"),',
        '    ("dev", "開発"),',
        ']',
        '',
        'STRINGS = {',
        '    "lang": "ja",',
        '    "html_lang": "ja",',
        '    "title": "M5Tab5 Logic Analyzer — ドキュメント",',
        '    "brand": "M5Tab5 Logic Analyzer",',
        '    "tagline": "8ch / 最大 80 MSa/s / ESP32-P4",',
        '    "toc": "目次",',
        '    "menu_label": "目次",',
        '}',
        '',
        'PAGES = [',
    ]

    count = 0
    for m in ART.finditer(html):
        body = m.group("body")
        out.append("    {")
        out.append(f'        "id": "{m.group("page")}",')
        out.append(f'        "section": "{m.group("sec")}",')
        out.append(f'        "title": "{m.group("title")}",')
        out.append('        "body": """' + body.replace('\\', '\\\\').replace('"""', '\\"\\"\\"') + '""",')
        out.append("    },")
        count += 1

    out.append("]")
    out.append("")
    DST.write_text("\n".join(out), encoding="utf-8")
    print(f"wrote {DST} with {count} pages")
    return 0


if __name__ == "__main__":
    sys.exit(main())
