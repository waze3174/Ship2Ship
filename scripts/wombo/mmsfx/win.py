import sys, wave
import numpy as np

R = '/tmp/scratch/mmsfx/test/data/render/'


def load(p):
    w = wave.open(p)
    return np.frombuffer(w.readframes(w.getnframes()), '<i2').astype(float).reshape(-1, 2)


# usage: win.py <mm file suffix> <oot file suffix> e.g. 1857_mm_1 1857_oot
a = load(R + 'mmsfx_%s.wav' % sys.argv[1]).mean(1)
b = load(R + 'mmsfx_%s.wav' % sys.argv[2]).mean(1)
oa = int(np.argmax(np.abs(a) > 200))
ob = int(np.argmax(np.abs(b) > 200))
print('onsets', oa, ob)
W = 1600
for k in range(int(sys.argv[3]) if len(sys.argv) > 3 else 12):
    s0 = k * W
    x = a[oa + s0:oa + s0 + W]
    best = (-1, 0)
    for lag in range(-120, 121):
        if ob + s0 + lag < 0:
            continue
        y = b[ob + s0 + lag:ob + s0 + lag + W]
        if len(y) < W or x.std() == 0 or y.std() == 0:
            continue
        cc = np.corrcoef(x, y)[0, 1]
        if cc > best[0]:
            best = (cc, lag)
    print('  %4dms corr %.3f lag %4d rmsMM %6.0f rmsOOT %6.0f' % (s0 / 32, best[0], best[1], np.sqrt((x ** 2).mean()),
                                                              np.sqrt((b[ob + s0:ob + s0 + W] ** 2).mean())))
