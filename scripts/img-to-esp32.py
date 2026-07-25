#!/usr/bin/env python3
"""
Convert any image to 64x64 XBM bitmap and push to ESP32 via ws_cmd.json.

Usage:
  python3 img-to-esp32.py <image_url_or_path> [output_x] [output_y]

Example:
  python3 img-to-esp32.py https://example.com/icon.png 32 0
  python3 img-to-esp32.py /tmp/photo.jpg 48 16
"""
import json, sys, os, tempfile
from PIL import Image

# Default output position (centered 64x64 on 128x64 OLED)
OUT_X = 32
OUT_Y = 0

def convert_to_xbm(img_url, out_x=OUT_X, out_y=OUT_Y):
    """Download/load image → 64x64 monochrome XBM → push to ESP32."""
    # Load image
    if img_url.startswith(('http://', 'https://')):
        import urllib.request
        with tempfile.NamedTemporaryFile(suffix='.png', delete=False) as f:
            urllib.request.urlretrieve(img_url, f.name)
            path = f.name
    else:
        path = img_url

    img = Image.open(path)

    # Convert to RGBA, composite on black (remove transparency)
    if img.mode == 'RGBA':
        bg = Image.new('RGBA', img.size, (0, 0, 0, 255))
        img = Image.alpha_composite(bg, img)
    else:
        img = img.convert('RGBA')

    # Grayscale → binary threshold → resize to 64x64
    gray = img.convert('L')
    bw = gray.point(lambda x: 255 if x > 30 else 0, '1')
    sm = bw.resize((64, 64), Image.NEAREST)

    # Convert to XBM (LSB-first, verified working with ESP32 drawXBM)
    px = list(sm.getdata())
    out = bytearray()
    for y in range(64):
        for xb in range(0, 64, 8):
            b = 0
            for bit in range(8):
                if xb + bit < 64 and px[y * 64 + xb + bit]:
                    b |= 1 << bit
            out.append(b)

    # Push to ESP32 queue
    cmd = json.dumps({
        "type": "bitmap",
        "x": out_x,
        "y": out_y,
        "w": 64,
        "h": 64,
        "data": list(out)
    })
    with open('/tmp/ws_cmd.json', 'w') as f:
        f.write(cmd)

    lit = sum(1 for p in px if p)
    print(f'Done: {len(cmd)} bytes, {lit}/4096 pixels lit @ ({out_x},{out_y})')

    # Clean up temp file
    if img_url.startswith(('http://', 'https://')):
        os.unlink(path)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    url = sys.argv[1]
    x = int(sys.argv[2]) if len(sys.argv) > 2 else OUT_X
    y = int(sys.argv[3]) if len(sys.argv) > 3 else OUT_Y
    convert_to_xbm(url, x, y)
