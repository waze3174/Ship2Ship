#!/usr/bin/env python3
# Static walk of MM's Sequence_0 (MM seqplayer semantics) to find which opcodes each context uses.
import sys, struct, collections

seq = open(sys.argv[1], 'rb').read()
N = len(seq)

# MM arg table for 0xA0..0xFF (channel/flow): (count, [is_s16...])
def A(*t): return t
ARGS = {}
tab = [
 ('A0',[1]),('A1',[]),('A2',[1]),('A3',[]),('A4',[0]),('A5',[]),('A6',[0,1]),('A7',[0]),('A8',[1,1]),
 ('A9',[]),('AA',[]),('AB',[]),('AC',[]),('AD',[]),('AE',[]),('AF',[]),
 ('B0',[1]),('B1',[]),('B2',[1]),('B3',[0]),('B4',[]),('B5',[]),('B6',[]),('B7',[1]),('B8',[0]),('B9',[0]),('BA',[0]),
 ('BB',[0,1]),('BC',[1]),('BD',[1]),('BE',[0]),('BF',[]),('C0',[]),('C1',[0]),('C2',[1]),('C3',[]),('C4',[]),('C5',[]),
 ('C6',[0]),('C7',[0,1]),('C8',[0]),('C9',[0]),('CA',[0]),('CB',[1]),('CC',[0]),('CD',[0]),('CE',[1]),('CF',[1]),
 ('D0',[0]),('D1',[0]),('D2',[0]),('D3',[0]),('D4',[0]),('D5',[0]),('D6',[0]),('D7',[0]),('D8',[0]),('D9',[0]),
 ('DA',[1]),('DB',[0]),('DC',[0]),('DD',[0]),('DE',[1]),('DF',[0]),('E0',[0]),('E1',[0,0,0]),('E2',[0,0,0]),('E3',[0]),
 ('E4',[]),('E5',[0]),('E6',[0]),('E7',[1]),('E8',[0,0,0]),('E9',[0]),('EA',[]),('EB',[0,0]),('EC',[]),('ED',[0]),
 ('EE',[0]),('EF',[1,0]),('F0',[]),('F1',[0]),('F2',[0]),('F3',[0]),('F4',[0]),('F5',[1]),('F6',[]),('F7',[]),('F8',[0]),
 ('F9',[1]),('FA',[1]),('FB',[1]),('FC',[1]),('FD',[]),('FE',[]),('FF',[]),
]
for name, a in tab:
    ARGS[int(name,16)] = a

used = {'seq': collections.Counter(), 'chan': collections.Counter(), 'layer': collections.Counter()}
visited = set()
work = [('seq', 0)]
dyntables = set()

def u8(p): return seq[p]
def s16(p): return struct.unpack_from('>h', seq, p)[0]
def u16(p): return struct.unpack_from('>H', seq, p)[0]
def cu16(p):
    v = seq[p]
    if v & 0x80:
        return ((v << 8) & 0x7F00) | seq[p+1], 2
    return v, 1

def flow(ctx, p, op):
    """returns (newp, targets, stop)"""
    a = ARGS[op]
    targets = []
    if op == 0xFD:
        _, l = cu16(p); return p + l, [], False
    if op in (0xFE, 0xF6, 0xF7):
        return p, [], False
    if op == 0xFF:
        return p, [], True
    if len(a) == 1:
        v = s16(p) if a[0] else u8(p)
        np = p + (2 if a[0] else 1)
        if op in (0xF2, 0xF3, 0xF4):
            t = np + ((v ^ 0x80) - 0x80)
            targets.append(t)
            return np, targets, op == 0xF4
        if op == 0xF8:
            return np, [], False
        t = v & 0xFFFF
        targets.append(t)
        return np, targets, op == 0xFB
    return p, [], False

def walk(ctx, p):
    while 0 <= p < N:
        if (ctx, p) in visited:
            return
        visited.add((ctx, p))
        op = seq[p]; p += 1
        if op >= 0xF2:
            used[ctx][op] += 1
            p, t, stop = flow(ctx, p, op)
            for x in t: work.append((ctx, x))
            if stop: return
            continue
        if ctx == 'seq':
            if op >= 0xC0:
                used['seq'][op] += 1
                if op in (0xF1, 0xDF, 0xDE, 0xDD, 0xDC, 0xD9, 0xD5, 0xD3, 0xD0, 0xCE, 0xCC, 0xC9, 0xC8): p += 1
                elif op == 0xDA: p += 3
                elif op == 0xDB: p += 1
                elif op in (0xD7, 0xD6, 0xD1, 0xD2, 0xC5): p += 2
                elif op == 0xCD:
                    p += 2  # dyncall; can't resolve
                elif op == 0xC7: p += 3
                elif op in (0xC2, 0xC3): p += 2
                elif op == 0xC6: return
                elif op == 0xEF: p += 3
                elif op == 0xC4: p += 2
                continue
            hi = op & 0xF0
            used['seq'][hi] += 1
            if hi == 0x90:
                work.append(('chan', u16(p))); p += 2
            elif hi == 0xA0:
                work.append(('chan', p + 2 + s16(p))); p += 2
            elif hi == 0xB0: p += 3
            elif hi == 0x60: p += 2
            continue
        if ctx == 'chan':
            if op >= 0xA0:
                used['chan'][op] += 1
                a = ARGS[op]
                args = []
                for isS in a:
                    args.append(s16(p) if isS else u8(p)); p += 2 if isS else 1
                if op == 0xC2:
                    dyntables.add(args[0] & 0xFFFF)
                if op == 0xEA: return
                if op == 0xE8: p += 5
                continue
            if op >= 0x70:
                hi = op & 0xF8
                used['chan'][hi] += 1
                if hi == 0x88:
                    work.append(('layer', u16(p))); p += 2
                elif hi == 0x78:
                    work.append(('layer', p + 2 + s16(p))); p += 2
                continue
            hi = op & 0xF0
            used['chan'][hi] += 1
            if hi == 0x20:
                work.append(('chan', u16(p))); p += 2
            elif hi in (0x30, 0x40): p += 1
            continue
        if ctx == 'layer':
            if op <= 0xC0:
                used['layer']['note' if op < 0xC0 else 0xC0] += 1
                # assume small notes unless we can't know: try to account large note encoding? skip args heuristically
                if op == 0xC0:
                    _, l = cu16(p); p += l; continue
                # we don't know largeNotes; sfx seq uses large notes (C4) mostly
                kind = op & 0xC0
                if LARGE:
                    if kind == 0x00: _, l = cu16(p); p += l + 2
                    elif kind == 0x40: _, l = cu16(p); p += l + 1
                    else: p += 2
                else:
                    if kind == 0x00: _, l = cu16(p); p += l
                continue
            used['layer'][op] += 1
            if op in (0xC1, 0xCA, 0xC9, 0xC2, 0xC6, 0xCF, 0xCD, 0xCE, 0xF1): p += 1
            elif op == 0xC3: _, l = cu16(p); p += l
            elif op == 0xC7:
                mode = u8(p); p += 2
                if mode & 0x80: p += 1
                else: _, l = cu16(p); p += l
            elif op == 0xCB: p += 3
            elif op == 0xF0: p += 2
            continue

LARGE = True
while work:
    ctx, p = work.pop()
    walk(ctx, p)

# dyntables: enumerate entries that look like pointers (channel scripts)
for t in sorted(dyntables):
    cnt = 0
    for i in range(0, 0x200):
        q = t + i * 2
        if q + 1 >= N: break
        v = u16(q)
        if v == 0 or v >= N: break
        work.append(('chan', v)); cnt += 1
    print('dyntable %04X entries %d' % (t, cnt))
while work:
    ctx, p = work.pop()
    walk(ctx, p)
# second round for dyntables found later
for ctx in used:
    print(ctx, ' '.join('%s:%d' % (('%02X' % k) if isinstance(k, int) else k, v) for k, v in sorted(used[ctx].items(), key=lambda kv: str(kv[0]))))

for (ctx, p) in sorted(visited):
    if ctx == 'layer' and seq[p] in (0xF0, 0xF1):
        print('layer %02X at %04X args %s' % (seq[p], p, seq[p+1:p+3].hex()))
    if ctx == 'chan' and seq[p] in (0xA4, 0xBB, 0xBD, 0xED):
        print('chan %02X at %04X args %s' % (seq[p], p, seq[p+1:p+4].hex()))
