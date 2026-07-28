#!/usr/bin/env python3
"""Draw the M5-Bus header pinout with the probe channels highlighted.

The pin assignments are M5Stack hardware facts; this is our own rendering of
them so the manual can mark which pins the firmware samples and colour them to
match the on-screen traces.

Usage:  python sim/tools/make_pinout.py > docs/img/m5bus-pinout.svg
"""

import sys

# row: (left function, left pin, left number, right number, right pin, right function)
ROWS = [
    ("",        "GND",  1,  2,  "G16", "GPIO"),
    ("",        "GND",  3,  4,  "G17", "PB_IN"),
    ("",        "GND",  5,  6,  "RST", "EN"),
    ("MOSI",    "G18",  7,  8,  "G45", "GPIO"),
    ("MISO",    "G19",  9, 10,  "G52", "PB_OUT"),
    ("SCK",     "G5",  11, 12,  "3V3", ""),
    ("RXD0",    "G38", 13, 14,  "G37", "TXD0"),
    ("PC_RX",   "G7",  15, 16,  "G6",  "PC_TX"),
    ("Int SDA", "G31", 17, 18,  "G32", "Int SCL"),
    ("GPIO",    "G3",  19, 20,  "G4",  "GPIO"),
    ("GPIO",    "G2",  21, 22,  "G48", "GPIO"),
    ("GPIO",    "G47", 23, 24,  "G35", "GPIO"),
    ("",        "HVIN",25, 26,  "G51", "GPIO"),
    ("",        "HVIN",27, 28,  "5V",  ""),
    ("",        "HVIN",29, 30,  "BAT", ""),
]

# Probe channel -> pin name, coloured to match the firmware's trace palette.
CHANNELS = {
    "G2":  ("CH0", "#00ffff"),
    "G3":  ("CH1", "#00ff00"),
    "G4":  ("CH2", "#ffff00"),
    "G5":  ("CH3", "#ffa400"),
    "G16": ("CH4", "#ff00ff"),
    "G17": ("CH5", "#999dff"),
    "G18": ("CH6", "#ffffff"),
    "G19": ("CH7", "#ff7d63"),
}

POWER = {"3V3": "#c0392b", "5V": "#c0392b", "BAT": "#8e44ad",
         "HVIN": "#e08e0b", "GND": "#111111", "RST": "#7f8c8d"}

ROW_H = 30
TOP = 96
BADGE_W = 66
FUNC_W = 104
PIN_W = 74
NUM_W = 58
GAP = 6
LEFT_X = 20
WIDTH = LEFT_X + BADGE_W + GAP + FUNC_W + PIN_W + NUM_W * 2 + PIN_W + FUNC_W + GAP + BADGE_W + LEFT_X
HEIGHT = TOP + len(ROWS) * ROW_H + 108


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def cell(x, y, w, h, fill, text, color="#e6edf3", weight="500", size=13, rx=4):
    out = [f'<rect x="{x}" y="{y}" width="{w}" height="{h - 4}" rx="{rx}" fill="{fill}"/>']
    if text:
        out.append(
            f'<text x="{x + w / 2:.1f}" y="{y + (h - 4) / 2 + 4.5:.1f}" text-anchor="middle" '
            f'font-size="{size}" font-weight="{weight}" fill="{color}">{esc(text)}</text>')
    return "".join(out)


def main():
    o = []
    o.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {HEIGHT}" '
             f'width="{WIDTH}" height="{HEIGHT}" font-family="Segoe UI, Helvetica Neue, Hiragino Sans, sans-serif">')
    o.append(f'<rect width="{WIDTH}" height="{HEIGHT}" fill="#0f1216"/>')

    o.append(f'<text x="{LEFT_X}" y="34" font-size="19" font-weight="700" fill="#e6edf3">'
             'M5Stack Tab5 &#8212; M5-Bus (2&#215;15)</text>')
    o.append(f'<text x="{LEFT_X}" y="56" font-size="13" fill="#8d9aa8">'
             'Logic analyzer probe channels highlighted. Input level is 3.3 V.</text>')

    # column headings
    x = LEFT_X + BADGE_W + GAP
    heads = [("FUNC", FUNC_W), ("PIN", PIN_W), ("LEFT", NUM_W),
             ("RIGHT", NUM_W), ("PIN", PIN_W), ("FUNC", FUNC_W)]
    for label, w in heads:
        o.append(f'<text x="{x + w / 2:.1f}" y="{TOP - 12}" text-anchor="middle" '
                 f'font-size="11" letter-spacing="1.2" fill="#8d9aa8">{label}</text>')
        x += w

    for i, (lf, lp, ln, rn, rp, rf) in enumerate(ROWS):
        y = TOP + i * ROW_H

        # zebra backing so the eye can track a row across the header
        if i % 2 == 0:
            o.append(f'<rect x="{LEFT_X}" y="{y - 2}" width="{WIDTH - 2 * LEFT_X}" '
                     f'height="{ROW_H}" fill="#151a20"/>')

        x = LEFT_X
        # left channel badge
        if lp in CHANNELS:
            name, col = CHANNELS[lp]
            o.append(cell(x, y, BADGE_W, ROW_H, col, name, "#0b0f13", "700", 13))
        x += BADGE_W + GAP

        o.append(cell(x, y, FUNC_W, ROW_H, "#1d242c", lf, "#b9c4cf")); x += FUNC_W
        o.append(cell(x, y, PIN_W, ROW_H, POWER.get(lp, "#2b343e"), lp,
                      "#ffffff" if lp in POWER else "#e6edf3", "600")); x += PIN_W
        o.append(cell(x, y, NUM_W, ROW_H, "#3a444f", str(ln), "#e6edf3", "600")); x += NUM_W
        o.append(cell(x, y, NUM_W, ROW_H, "#3a444f", str(rn), "#e6edf3", "600")); x += NUM_W
        o.append(cell(x, y, PIN_W, ROW_H, POWER.get(rp, "#2b343e"), rp,
                      "#ffffff" if rp in POWER else "#e6edf3", "600")); x += PIN_W
        o.append(cell(x, y, FUNC_W, ROW_H, "#1d242c", rf, "#b9c4cf")); x += FUNC_W

        x += GAP
        if rp in CHANNELS:
            name, col = CHANNELS[rp]
            o.append(cell(x, y, BADGE_W, ROW_H, col, name, "#0b0f13", "700", 13))

    y = TOP + len(ROWS) * ROW_H + 22
    o.append(f'<text x="{LEFT_X}" y="{y}" font-size="13" fill="#ff8f3f" font-weight="600">'
             '&#9888; Pins 25 / 27 / 29 are HVIN and 28 / 30 are 5 V / battery.</text>')
    o.append(f'<text x="{LEFT_X}" y="{y + 20}" font-size="13" fill="#8d9aa8">'
             'Do not let a probe lead touch them, and never feed more than 3.3 V into a channel.</text>')
    o.append(f'<text x="{LEFT_X}" y="{y + 46}" font-size="13" fill="#8d9aa8">'
             'Use pins 1 / 3 / 5 (GND) as the common ground with the circuit under test.</text>')
    o.append(f'<text x="{LEFT_X}" y="{y + 66}" font-size="13" fill="#8d9aa8">'
             'CH3 / CH6 / CH7 share the M5-Bus SPI lines and CH5 shares PB_IN &#8212; '
             'avoid stacking a module that drives them.</text>')

    o.append('</svg>')
    print("\n".join(o))
    return 0


if __name__ == "__main__":
    sys.exit(main())
