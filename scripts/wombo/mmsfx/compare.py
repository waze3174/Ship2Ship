#!/usr/bin/env python3
"""Compare MM-native (mm engine) vs native-port (oot engine) renders of the same MM sfx.
usage: compare.py <dir> [ids...]  (finds mmsfx_<ID>_mm.wav / mmsfx_<ID>_oot.wav)"""
import sys, os, glob, wave
import numpy as np

SR = 32000

def load(path):
    w = wave.open(path, 'rb')
    data = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64)
    return data.reshape(-1, 2)

def env(x, win=320):  # 10 ms RMS envelope
    n = len(x) // win
    x = x[:n * win].reshape(n, win)
    return np.sqrt((x ** 2).mean(axis=1))

def onset_and_end(mono):
    e = env(mono)
    if e.max() <= 0:
        return 0, 0
    thr = e.max() * 0.01
    idx = np.nonzero(e > thr)[0]
    on = idx[0]
    off = idx[-1] + 1
    # refine onset to sample level within that frame
    seg = np.abs(mono[on * 320:(on + 1) * 320])
    s = on * 320 + int(np.argmax(seg > max(seg.max() * 0.05, 1)))
    return s, off * 320

def best_shift(a, b, maxlag=640):
    # shift of b (samples) that best matches a, by cross-correlation via FFT over the first 0.6 s
    n = min(len(a), len(b), int(0.6 * SR))
    a = a[:n] - a[:n].mean(); b = b[:n] - b[:n].mean()
    N = 1 << int(np.ceil(np.log2(2 * n)))
    c = np.fft.irfft(np.fft.rfft(a, N) * np.conj(np.fft.rfft(b, N)), N)
    lags = np.concatenate([np.arange(0, maxlag + 1), np.arange(-maxlag, 0)])
    vals = np.concatenate([c[:maxlag + 1], c[-maxlag:]])
    return int(lags[np.argmax(vals)])

def spectrum(x):
    n = 4096
    if len(x) < n:
        x = np.pad(x, (0, n - len(x)))
    frames = [x[i:i + n] * np.hanning(n) for i in range(0, len(x) - n + 1, n // 2)]
    return np.mean([np.abs(np.fft.rfft(f)) for f in frames], axis=0)

def corr(a, b):
    a = a - a.mean(); b = b - b.mean()
    d = np.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / d) if d > 0 else 0.0

def compare(mm_path, oot_path):
    mm = load(mm_path); oot = load(oot_path)
    mmm = mm.mean(axis=1); oom = oot.mean(axis=1)
    on_m, end_m = onset_and_end(mmm)
    on_o, end_o = onset_and_end(oom)
    res = {}
    res['peak_mm'] = int(np.abs(mm).max()); res['peak_oot'] = int(np.abs(oot).max())
    res['len_mm_ms'] = (end_m - on_m) * 1000 / SR
    res['len_oot_ms'] = (end_o - on_o) * 1000 / SR
    a = mmm[on_m:]; b = oom[on_o:]
    sh = best_shift(a, b)
    if sh > 0:
        b = np.concatenate([np.zeros(sh), b])
    elif sh < 0:
        b = b[-sh:]
    L = min(len(a), len(b), max(end_m - on_m, end_o - on_o) + 640)
    a = a[:L]; b = b[:L]
    res['align_shift_samples'] = sh
    rms_a = np.sqrt((a ** 2).mean()); rms_b = np.sqrt((b ** 2).mean())
    res['rms_mm'] = round(float(rms_a), 1); res['rms_oot'] = round(float(rms_b), 1)
    res['level_diff_db'] = round(20 * np.log10(rms_b / rms_a), 2) if rms_a > 0 and rms_b > 0 else None
    res['wave_corr'] = round(corr(a, b), 4)
    E = min(len(a), int(0.15 * SR))
    res['early150_corr'] = round(corr(a[:E], b[:E]), 4)
    g = (a * b).sum() / max((b * b).sum(), 1e-9)
    res['residual_rel'] = round(float(np.sqrt(((a - g * b) ** 2).mean()) / max(rms_a, 1e-9)), 4)
    ea = env(a); eb = env(b)
    n = min(len(ea), len(eb))
    res['env_corr'] = round(corr(ea[:n], eb[:n]), 4)
    sa = spectrum(a); sb = spectrum(b)
    res['spec_corr'] = round(corr(np.log10(sa + 1), np.log10(sb + 1)), 4)
    f = np.fft.rfftfreq(4096, 1 / SR)
    res['centroid_mm_hz'] = round(float((f * sa).sum() / sa.sum()), 1)
    res['centroid_oot_hz'] = round(float((f * sb).sum() / sb.sum()), 1)
    res['peakfreq_mm_hz'] = round(float(f[np.argmax(sa)]), 1)
    res['peakfreq_oot_hz'] = round(float(f[np.argmax(sb)]), 1)
    # stereo balance
    def bal(x):
        l = np.sqrt((x[:, 0] ** 2).mean()); r = np.sqrt((x[:, 1] ** 2).mean())
        return round(float(20 * np.log10((l + 1e-9) / (r + 1e-9))), 2)
    res['lr_db_mm'] = bal(mm[on_m:on_m + L]); res['lr_db_oot'] = bal(oot[on_o:on_o + L])
    return res

def main():
    d = sys.argv[1]
    files = glob.glob(os.path.join(d, 'mmsfx_*_oot*.wav'))
    ids = sorted({os.path.basename(p).split('_')[1] for p in files})
    if len(sys.argv) > 2:
        ids = sys.argv[2:]
    for i in ids:
        oots = sorted(glob.glob(os.path.join(d, 'mmsfx_%s_oot*.wav' % i)))
        mms = sorted(glob.glob(os.path.join(d, 'mmsfx_%s_mm*.wav' % i)))
        if not oots or not mms:
            print(i, 'missing'); continue
        if len(oots) == 1 and len(mms) == 1:
            r = compare(mms[0], oots[0])
            print('0x%s' % i, ' '.join('%s=%s' % kv for kv in r.items()))
            continue
        print('0x%s: %d OOT-native renders vs %d MM-engine renders; best MM match per OOT render:' % (i, len(oots), len(mms)))
        for o in oots:
            best = max((compare(m, o) | {'mm': os.path.basename(m)} for m in mms), key=lambda r: r['wave_corr'])
            keys = ['mm', 'wave_corr', 'early150_corr', 'env_corr', 'spec_corr', 'level_diff_db', 'len_mm_ms', 'len_oot_ms', 'residual_rel']
            print('  ', os.path.basename(o), ' '.join('%s=%s' % (k, best[k]) for k in keys))

main()
