import sys, struct, importlib.util, io, contextlib
path = '/tmp/scratch/mmsfx/disasm.py'
seqp = '/tmp/scratch/mmsfx/seq0.bin'
sys.argv = ['x', seqp]
spec = importlib.util.spec_from_file_location('d', path)
d = importlib.util.module_from_spec(spec)
with contextlib.redirect_stdout(io.StringIO()):
    spec.loader.exec_module(d)
seq = d.seq


def reach(start):
    d.visited.clear()
    d.work.clear()
    d.work.append(('chan', start))
    while d.work:
        c, p = d.work.pop()
        d.walk(c, p)
    hits = [p for (c, p) in d.visited if c == 'layer' and seq[p] == 0xF0]
    ops = sorted({seq[p] for (c, p) in d.visited if c == 'chan' and seq[p] in (0xA4, 0xBB, 0xBD, 0xED, 0xD3, 0xEE, 0xD8, 0xE2)})
    return hits, ops


for name, table, idx in [('player 0x990', 0x23E8, 0x190), ('player 0x8E6', 0x23E8, 0xE6), ('player 0x8E8', 0x23E8, 0xE8),
                         ('item 0x1857', 0x1990, 0x57), ('item 0x185A', 0x1990, 0x5A), ('voice 0x68C0', 0xB30E, 0xC0),
                         ('voice 0x68A0', 0xB30E, 0xA0), ('voice 0x6880', 0xB30E, 0x80)]:
    e = struct.unpack_from('>H', seq, table + idx * 2)[0]
    hits, ops = reach(e)
    print(name, 'script %04X' % e, 'F0 hits', ['%04X' % h for h in hits], 'chan ops', ['%02X' % o for o in ops])
