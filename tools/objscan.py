# find objects of a class in the running game by their vtable pointer (read-only)
# objscan.py <vtable hex> [dump bytes hex] [max hits]   e.g. objscan.py ba8644 40
# objscan.py refs <addr hex> [max hits]                  find dwords pointing at addr
import ctypes, struct, subprocess, sys
k32 = ctypes.WinDLL('kernel32')
class MBI(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_void_p), ('AllocationBase', ctypes.c_void_p), ('AllocationProtect', ctypes.c_ulong), ('PartitionId', ctypes.c_ushort), ('RegionSize', ctypes.c_size_t), ('State', ctypes.c_ulong), ('Protect', ctypes.c_ulong), ('Type', ctypes.c_ulong)]
pid = [int(l.split('","')[1]) for l in subprocess.check_output(['tasklist', '/fo', 'csv', '/nh']).decode('mbcs').splitlines() if l.lower().startswith('"splitsecond.exe"')][0]
h = k32.OpenProcess(0x0410, False, pid)

def rd(a, n):
    b = ctypes.create_string_buffer(n); g = ctypes.c_size_t()
    k32.ReadProcessMemory(h, ctypes.c_void_p(a), b, n, ctypes.byref(g)); return b.raw[:g.value]

def regions():
    a, m = 0x10000, MBI()
    while a < 0xFFFF0000 and k32.VirtualQueryEx(h, ctypes.c_void_p(a), ctypes.byref(m), ctypes.sizeof(m)):
        size = m.RegionSize
        # committed, readable/writable private or mapped memory (heap, .data), no guard pages
        if m.State == 0x1000 and m.Protect & 0xCC and not m.Protect & 0x100:
            yield a, size
        a += size

def find(value, limit):
    pat = struct.pack('<I', value); hits = []
    for a, size in regions():
        for off in range(0, size, 0x100000):
            chunk = rd(a + off, min(0x100000, size - off))
            i = chunk.find(pat)
            while i >= 0:
                if i % 4 == 0:
                    hits.append(a + off + i)
                    if len(hits) >= limit: return hits
                i = chunk.find(pat, i + 1)
    return hits

if sys.argv[1] == 'refs':
    for x in find(int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 50): print('%08X' % x)
else:
    vt = int(sys.argv[1], 16); n = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x40
    for x in find(vt, int(sys.argv[3]) if len(sys.argv) > 3 else 64):
        raw = rd(x, n)
        words = struct.unpack('<%dI' % (len(raw) // 4), raw[:len(raw) // 4 * 4])
        print('%08X ' % x + ' '.join('%08X' % w for w in words))
