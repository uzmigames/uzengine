"""Generate a 512x512 checker texture PNG (dark gray / light gray).
   Mimics Unreal Engine's default M_EditorGrid checker pattern."""
import struct, zlib, math

WIDTH  = 512
HEIGHT = 512
CELLS  = 8   # checker squares per axis

def make_png(width, height, pixels):
    """pixels: list of (r,g,b) tuples, row-major."""
    def chunk(name, data):
        c = name + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)

    # IHDR
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)

    # Image data
    raw = b''
    for y in range(height):
        raw += b'\x00'  # filter byte
        for x in range(width):
            r, g, b = pixels[y * width + x]
            raw += bytes([r, g, b])

    compressed = zlib.compress(raw, 9)

    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', ihdr)
            + chunk(b'IDAT', compressed)
            + chunk(b'IEND', b''))

pixels = []
cell_size = WIDTH // CELLS
# Unreal-style: light gray (#CBCBCB) and dark gray (#808080) checker
LIGHT = (203, 203, 203)
DARK  = (110, 110, 110)
for y in range(HEIGHT):
    for x in range(WIDTH):
        cx = x // cell_size
        cy = y // cell_size
        if (cx + cy) % 2 == 0:
            pixels.append(LIGHT)
        else:
            pixels.append(DARK)

out = "e:/UzEngine/assets/textures/checker.png"
with open(out, 'wb') as f:
    f.write(make_png(WIDTH, HEIGHT, pixels))
print(f"Written {out}: {WIDTH}x{HEIGHT} checker")
