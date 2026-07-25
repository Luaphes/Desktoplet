#!/usr/bin/env python3
"""
Compose 128x64 OLED display: combine bitmaps + any Chinese/English text.

Usage:
  # Hermes logo at left, text at right (like our '初号机' layout)
  python3 img-to-esp32.py compose '[
    {"type":"bitmap","url":"https://cdn.jsdelivr.net/gh/selfhst/icons/png/hermes-agent.png","x":0,"y":4,"w":64,"h":64},
    {"type":"text","text":"初号机","x":76,"y":4,"size":14}
  ]'

  # Just a bitmap
  python3 img-to-esp32.py bitmap <url> [x=32] [y=0]

  # Just Chinese text (centered)
  python3 img-to-esp32.py text "你好世界"
"""

import json, sys, os, tempfile, urllib.request
from PIL import Image, ImageDraw, ImageFont

WQY_FONT = "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc"
CMD_FILE = "/tmp/ws_cmd.json"

def load_image(url_or_path):
    """Load image, composite on black bg, return 1-bit image."""
    if url_or_path.startswith(('http://', 'https://')):
        with tempfile.NamedTemporaryFile(suffix='.png', delete=False) as f:
            urllib.request.urlretrieve(url_or_path, f.name)
            path = f.name
    else:
        path = url_or_path
    img = Image.open(path).convert('RGBA')
    bg = Image.new('RGBA', img.size, (0, 0, 0, 255))
    comp = Image.alpha_composite(bg, img).convert('L')
    bw = comp.point(lambda x: 255 if x > 30 else 0, '1')
    if url_or_path.startswith(('http://', 'https://')):
        os.unlink(path)
    return bw

def xbm_encode(img):
    """Convert PIL image to XBM bytearray (LSB-first)."""
    px = list(img.getdata())
    w, h = img.size
    out = bytearray()
    for y in range(h):
        for xb in range(0, w, 8):
            b = 0
            for bit in range(8):
                if xb + bit < w and px[y * w + xb + bit]:
                    b |= 1 << bit
            out.append(b)
    return out

def compose(elements, target=None):
    """Render elements onto 128x64 canvas, push to ESP32."""
    canvas = Image.new('1', (128, 64), 0)
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.truetype(WQY_FONT, 14) if os.path.exists(WQY_FONT) else None

    for el in elements:
        t = el.get("type", "")
        if t == "bitmap":
            img = load_image(el["url"])
            img = img.resize((el.get("w", 64), el.get("h", 64)), Image.NEAREST)
            canvas.paste(img, (el.get("x", 0), el.get("y", 0)))
        elif t == "text":
            if font:
                text = el["text"]
                vx = el.get("x", 0)
                vy = el.get("y", 0)
                if el.get("vertical"):
                    for i, ch in enumerate(text):
                        draw.text((vx, vy + i * (el.get("size", 14) + 6)), ch, fill=1, font=font)
                else:
                    draw.text((vx, vy), text, fill=1, font=font)

    xbm = xbm_encode(canvas)
    cmd = json.dumps({"type": "bitmap", "x": 0, "y": 0, "w": 128, "h": 64,
                       "data": list(xbm)})
    if target:
        cmd = json.dumps({"target": target, "type": "bitmap", "x": 0, "y": 0,
                           "w": 128, "h": 64, "data": list(xbm)})
    with open(CMD_FILE, 'w') as f:
        f.write(cmd)
    print(f"Pushed: {len(elements)} elements, {len(cmd)} bytes")

def main():
    if len(sys.argv) < 2:
        print(__doc__); return

    mode = sys.argv[1]
    if mode == "compose" and len(sys.argv) >= 3:
        elements = json.loads(sys.argv[2])
        target = sys.argv[3] if len(sys.argv) > 3 else None
        compose(elements, target)
    elif mode == "bitmap" and len(sys.argv) >= 3:
        img = load_image(sys.argv[2])
        img = img.resize((64, 64), Image.NEAREST)
        x = int(sys.argv[3]) if len(sys.argv) > 3 else 32
        y = int(sys.argv[4]) if len(sys.argv) > 4 else 0
        xbm = xbm_encode(img)
        cmd = json.dumps({"type": "bitmap", "x": x, "y": y, "w": 64, "h": 64,
                           "data": list(xbm)})
        with open(CMD_FILE, 'w') as f: f.write(cmd)
        print(f"Bitmap @ ({x},{y}): {len(cmd)} bytes")
    elif mode == "text" and len(sys.argv) >= 3:
        compose([{"type": "text", "text": sys.argv[2], "x": 0, "y": 20}])
    else:
        print("Usage:\n  compose '<json>' [target]\n  bitmap <url> [x] [y]\n  text <string>")

if __name__ == "__main__":
    main()
