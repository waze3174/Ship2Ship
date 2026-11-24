"""selfcmp.py <dir> <id> <engine a> <engine b>: best wave_corr of each <a> render against the other <b> renders
(a == b: MM-vs-MM self-consistency, the baseline for sfx with continuous randomness)."""
import sys, glob, os
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'compare.py')).read().replace('\nmain()\n', '\n')
exec(compile(src, 'compare.py', 'exec'))
d, i, ea, eb = sys.argv[1:5]
A = sorted(glob.glob(os.path.join(d, 'mmsfx_%s_%s*.wav' % (i, ea))))
B = sorted(glob.glob(os.path.join(d, 'mmsfx_%s_%s*.wav' % (i, eb))))
vals = []
for a in A:
    best = max((compare(b, a)['wave_corr'], os.path.basename(b)) for b in B if b != a)
    vals.append(best[0])
    print(os.path.basename(a), best)
print('%s vs %s: mean best wave_corr %.3f' % (ea, eb, sum(vals) / len(vals)))
