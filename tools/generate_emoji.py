#!/usr/bin/env python3
"""Generate emoji JPEG images and convert to C header for ESP32 firmware.

Usage:
    python3 tools/generate_emoji.py [--font /path/to/NotoColorEmoji.ttf]

Output:
    src/emoji_bitmaps.h  — namespace mova { const uint8_t EMOJI_xxx[] PROGMEM = {...}; }
"""

import argparse
import io
import os
import sys

from PIL import Image, ImageDraw, ImageFont, ImageChops

WIDTH = 320
HEIGHT = 240
EMOJI_SIZE = 200
BG_COLOR = (0, 0, 0)
JPEG_QUALITY = 85

EMOJIS = [
    ("NEUTRAL",   "\U0001F610"),  # 😐
    ("GRINNING",  "\U0001F600"),  # 😀
    ("COOL",      "\U0001F60E"),  # 😎
    ("FIRE",      "\U0001F525"),  # 🔥
    ("CRYING",    "\U0001F622"),  # 😢
    ("ANGRY",     "\U0001F621"),  # 😡
    ("HEART",     "\u2764\ufe0f"),  # ❤️ (VS16 forces color emoji presentation)
    ("LIGHTNING", "\u26A1"),      # ⚡
]


def is_colorful(img, threshold=10):
    """Check if image has meaningful color (not just grayscale)."""
    rgb = img.convert("RGB")
    r, g, b = rgb.split()
    diff_rg = ImageChops.difference(r, g)
    diff_rb = ImageChops.difference(r, b)
    return diff_rg.getextrema()[1] > threshold or diff_rb.getextrema()[1] > threshold


def centered_position(draw, text, font):
    """Compute (x, y) to draw text centered on the canvas."""
    bbox = draw.textbbox((0, 0), text, font=font)
    x = (WIDTH - (bbox[2] - bbox[0])) // 2 - bbox[0]
    y = (HEIGHT - (bbox[3] - bbox[1])) // 2 - bbox[1]
    return x, y


def render_with_font(char, font_path):
    """Try to render emoji using the specified font. Returns Image or None."""
    try:
        font = ImageFont.truetype(font_path, EMOJI_SIZE)
    except (OSError, IOError) as e:
        print(f"  WARNING: Cannot load font {font_path}: {e}", file=sys.stderr)
        return None

    img = Image.new("RGB", (WIDTH, HEIGHT), BG_COLOR)
    draw = ImageDraw.Draw(img)
    x, y = centered_position(draw, char, font)
    draw.text((x, y), char, font=font, fill=(255, 255, 255))

    if not is_colorful(img):
        return None

    return img


def render_fallback(name):
    """Render expressive face/symbol art on black background."""
    img = Image.new("RGB", (WIDTH, HEIGHT), BG_COLOR)
    d = ImageDraw.Draw(img)
    cx, cy = WIDTH // 2, HEIGHT // 2

    WHITE = (255, 255, 255)
    RED = (255, 50, 50)
    YELLOW = (255, 220, 50)
    ORANGE = (255, 140, 30)
    CYAN = (100, 200, 255)

    if name == "NEUTRAL":
        d.ellipse([cx-40-10, cy-30-10, cx-40+10, cy-30+10], fill=WHITE)
        d.ellipse([cx+40-10, cy-30-10, cx+40+10, cy-30+10], fill=WHITE)
        d.line([cx-30, cy+30, cx+30, cy+30], fill=WHITE, width=4)

    elif name == "GRINNING":
        d.ellipse([cx-40-10, cy-30-10, cx-40+10, cy-30+10], fill=WHITE)
        d.ellipse([cx+40-10, cy-30-10, cx+40+10, cy-30+10], fill=WHITE)
        d.arc([cx-40, cy, cx+40, cy+50], 0, 180, fill=WHITE, width=4)

    elif name == "COOL":
        d.rounded_rectangle([cx-65, cy-40, cx-15, cy-15], radius=5, fill=YELLOW)
        d.rounded_rectangle([cx+15, cy-40, cx+65, cy-15], radius=5, fill=YELLOW)
        d.line([cx-15, cy-28, cx+15, cy-28], fill=YELLOW, width=3)
        d.arc([cx-30, cy+5, cx+30, cy+45], 0, 180, fill=WHITE, width=4)

    elif name == "FIRE":
        outer = [
            (cx, cy-90), (cx+45, cy-40), (cx+55, cy+10),
            (cx+40, cy+60), (cx+15, cy+80), (cx-15, cy+80),
            (cx-40, cy+60), (cx-55, cy+10), (cx-45, cy-40),
        ]
        d.polygon(outer, fill=ORANGE)
        inner = [
            (cx, cy-40), (cx+25, cy), (cx+25, cy+35),
            (cx+10, cy+60), (cx-10, cy+60), (cx-25, cy+35), (cx-25, cy),
        ]
        d.polygon(inner, fill=YELLOW)

    elif name == "CRYING":
        d.ellipse([cx-40-10, cy-30-10, cx-40+10, cy-30+10], fill=WHITE)
        d.ellipse([cx+40-10, cy-30-10, cx+40+10, cy-30+10], fill=WHITE)
        d.ellipse([cx-40-6, cy, cx-40+6, cy+25], fill=CYAN)
        d.arc([cx-30, cy+20, cx+30, cy+60], 180, 360, fill=WHITE, width=4)

    elif name == "ANGRY":
        d.line([cx-55, cy-50, cx-25, cy-35], fill=RED, width=5)
        d.line([cx+55, cy-50, cx+25, cy-35], fill=RED, width=5)
        d.ellipse([cx-40-10, cy-20-10, cx-40+10, cy-20+10], fill=RED)
        d.ellipse([cx+40-10, cy-20-10, cx+40+10, cy-20+10], fill=RED)
        d.arc([cx-35, cy+20, cx+35, cy+60], 180, 360, fill=RED, width=5)

    elif name == "HEART":
        r = 38
        d.ellipse([cx-r*2, cy-r-20, cx, cy+r-20], fill=RED)
        d.ellipse([cx, cy-r-20, cx+r*2, cy+r-20], fill=RED)
        d.polygon([(cx-r*2, cy-5), (cx+r*2, cy-5), (cx, cy+75)], fill=RED)

    elif name == "LIGHTNING":
        bolt = [
            (cx+10, cy-90), (cx-25, cy-5), (cx+5, cy-5),
            (cx-20, cy+90), (cx+35, cy+5), (cx+5, cy+5),
        ]
        d.polygon(bolt, fill=YELLOW)

    return img


def image_to_jpeg_bytes(img):
    """Convert PIL Image to JPEG bytes."""
    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=JPEG_QUALITY)
    return buf.getvalue()


def bytes_to_c_array(data, var_name):
    """Convert bytes to C array string."""
    lines = []
    lines.append(f"static const uint8_t {var_name}[] PROGMEM = {{")
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_vals},")
    lines.append("};")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Generate emoji JPEG bitmaps for MOVA firmware")
    parser.add_argument("--font", type=str, default=None,
                        help="Path to Noto Color Emoji or similar font file")
    parser.add_argument("--output", type=str, default=None,
                        help="Output header file path (default: src/emoji_bitmaps.h)")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    output_path = args.output or os.path.join(project_root, "src", "emoji_bitmaps.h")

    font_label = args.font if args.font else "none (using fallback rendering)"
    print(f"Generating emoji bitmaps -> {output_path}")
    print(f"Image size: {WIDTH}x{HEIGHT}, JPEG quality: {JPEG_QUALITY}")
    print(f"Font: {font_label}")
    print()

    entries = []
    total_size = 0

    for name, char in EMOJIS:
        img = None

        if args.font:
            img = render_with_font(char, args.font)
            if img is None:
                print(f"  WARNING: {name} ({char}) - font render was grayscale, using fallback")

        if img is None:
            img = render_fallback(name)
            print(f"  {name}: fallback (yellow circle)")
        else:
            print(f"  {name}: font render OK")

        jpeg_data = image_to_jpeg_bytes(img)
        size = len(jpeg_data)
        total_size += size
        print(f"    JPEG size: {size:,} bytes ({size/1024:.1f} KB)")

        entries.append((name, jpeg_data))

    print()
    print(f"Total: {total_size:,} bytes ({total_size/1024:.1f} KB)")
    print()

    # Generate header file
    preamble = (
        "// Auto-generated by tools/generate_emoji.py — DO NOT EDIT\n"
        "// This header should only be included from src/emoji_data.cpp.\n"
        "#ifndef MOVA_EMOJI_BITMAPS_H\n"
        "#define MOVA_EMOJI_BITMAPS_H\n"
        "\n"
        "#include <cstdint>\n"
        "#include <pgmspace.h>\n"
        "\n"
        "namespace mova {\n"
    )

    array_sections = []
    for name, jpeg_data in entries:
        array_sections.append(bytes_to_c_array(jpeg_data, f"EMOJI_{name}"))

    postamble = (
        "}  // namespace mova\n"
        "\n"
        "#endif  // MOVA_EMOJI_BITMAPS_H\n"
    )

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w") as f:
        f.write(preamble + "\n")
        f.write("\n\n".join(array_sections))
        f.write("\n\n" + postamble)

    print(f"Written: {output_path}")


if __name__ == "__main__":
    main()
