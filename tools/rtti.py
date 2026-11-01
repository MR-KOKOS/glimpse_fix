# class name for a vtable address in the dumped image: rtti.py <vtable hex>...
import struct, sys
d = open(__import__('os').path.join(__import__('os').path.dirname(__file__), '..', 're', 'full_image.bin'), 'rb').read(); b = 0x400000
rd = lambda a: struct.unpack_from('<I', d, a - b)[0]
for v in sys.argv[1:]:
    vt = int(v, 16)
    try:
        col = rd(vt - 4); td = rd(col + 12)
        print('%08X %s' % (vt, d[td + 8 - b:d.index(b'\0', td + 8 - b)].decode()))
    except Exception as e: print('%08X ?' % vt)
