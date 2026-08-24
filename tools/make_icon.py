"""Generate src/config/sam.ico without any third-party imaging library.

Draws a simple speaker-and-waves glyph at several sizes and packs them into a
multi-resolution .ico as 32-bit BGRA images.
"""
import os
import struct

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "src", "config", "sam.ico")

BG = (0, 0, 0, 0)              # transparent
BODY = (0xF0, 0xC0, 0x30)      # BGR: amber speaker
WAVE = (0xFF, 0xE8, 0x90)      # BGR: pale wave


def draw(size):
    """Return a size*size list of (b, g, r, a) pixels."""
    px = [BG] * (size * size)

    def put(x, y, colour):
        if 0 <= x < size and 0 <= y < size:
            px[y * size + x] = (colour[0], colour[1], colour[2], 255)

    s = size / 32.0

    # Speaker box: a rectangle plus a trapezoidal cone.
    box_l, box_r = int(5 * s), int(11 * s)
    box_t, box_b = int(12 * s), int(20 * s)
    for y in range(box_t, box_b):
        for x in range(box_l, box_r):
            put(x, y, BODY)

    cone_l, cone_r = int(11 * s), int(18 * s)
    for x in range(cone_l, cone_r):
        # Cone widens linearly from the box to the mouth.
        t = (x - cone_l) / max(1.0, float(cone_r - cone_l))
        half = (4 + 7 * t) * s
        mid = 16 * s
        for y in range(int(mid - half), int(mid + half)):
            put(x, y, BODY)

    # Three sound arcs to the right.
    cx, cy = 18 * s, 16 * s
    for i, radius in enumerate((5, 8, 11)):
        r = radius * s
        steps = max(24, int(r * 8))
        for k in range(steps + 1):
            # Sweep roughly -55..55 degrees.
            ang = (-0.95) + (1.90 * k / steps)
            import math
            x = cx + r * math.cos(ang)
            y = cy + r * math.sin(ang)
            thickness = max(1, int(round(1.4 * s)))
            for dx in range(thickness):
                for dy in range(thickness):
                    put(int(x) + dx, int(y) + dy, WAVE)
    return px


def bmp_for(size):
    """Build a BITMAPINFOHEADER DIB (bottom-up BGRA + AND mask)."""
    px = draw(size)

    header = struct.pack("<IiiHHIIiiII",
                         40,          # biSize
                         size,        # biWidth
                         size * 2,    # biHeight (colour + mask)
                         1,           # biPlanes
                         32,          # biBitCount
                         0,           # biCompression = BI_RGB
                         size * size * 4,
                         0, 0, 0, 0)

    rows = []
    for y in range(size - 1, -1, -1):       # bottom-up
        row = bytearray()
        for x in range(size):
            b, g, r, a = px[y * size + x]
            row += bytes((b, g, r, a))
        rows.append(bytes(row))
    colour = b"".join(rows)

    # AND mask: one bit per pixel, rows padded to 4 bytes. Fully transparent
    # pixels are masked out for the benefit of very old renderers.
    stride = ((size + 31) // 32) * 4
    mask = bytearray()
    for y in range(size - 1, -1, -1):
        bits = bytearray(stride)
        for x in range(size):
            if px[y * size + x][3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits

    return header + colour + bytes(mask)


def main():
    sizes = (16, 20, 24, 32, 40, 48, 64, 128, 256)
    images = [(s, bmp_for(s)) for s in sizes]

    out = bytearray()
    out += struct.pack("<HHH", 0, 1, len(images))       # reserved, type=icon, count
    offset = 6 + 16 * len(images)
    for size, data in images:
        out += struct.pack("<BBBBHHII",
                           size if size < 256 else 0,
                           size if size < 256 else 0,
                           0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in images:
        out += data

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(out)
    print("wrote %s (%d bytes, %d sizes)" % (OUT, len(out), len(images)))


if __name__ == "__main__":
    main()
