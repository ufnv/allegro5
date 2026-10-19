"""Write test PNGs (RGBA8): plain and Adam7-interlaced, same pixels.
pixel(x, y) = (x*16, y*32, 200, alpha=(x*8 + y*16) & 255)"""
import struct, zlib, sys, os

W, H = 16, 8

def pix(x, y):
    return bytes([(x * 16) & 255, (y * 32) & 255, 200, (x * 8 + y * 16) & 255])

def chunk(t, d):
    c = struct.pack('>I', len(d)) + t + d
    return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)

def png(interlace):
    raw = b''
    if not interlace:
        for y in range(H):
            raw += b'\0' + b''.join(pix(x, y) for x in range(W))
    else:
        passes = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
                  (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
        for x0, y0, dx, dy in passes:
            xs = list(range(x0, W, dx))
            ys = list(range(y0, H, dy))
            if not xs or not ys:
                continue
            for y in ys:
                raw += b'\0' + b''.join(pix(x, y) for x in xs)
    ihdr = struct.pack('>IIBBBBB', W, H, 8, 6, 0, 0, 1 if interlace else 0)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) +
            chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))

out = sys.argv[1]
open(os.path.join(out, 'plain.png'), 'wb').write(png(False))
open(os.path.join(out, 'interlaced.png'), 'wb').write(png(True))
print('written')
