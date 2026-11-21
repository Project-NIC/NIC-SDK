"""IAGA-2002's filters against references: the low-pass and the Gaussian against SciPy's own, and,
where `BDV_SEC` and `BDV_MIN` name a day of Budkov's (BDV) second and minute values as INTERMAGNET
serves them, the observatory's own minutes made again from its own seconds, to its rounding.
Needs SciPy. The README says how the day is fetched."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import iaga, nrcmeasure
from scipy import signal as sps

for r in (10, 20, 32, 64):
    h = iaga.lowpass(r)
    ref = sps.firwin(len(h), 0.25, window="blackman", fs=float(r))
    assert max(abs(a - b) for a, b in zip(h, ref)) < 1e-15, r
g = sps.get_window(("gaussian", iaga.GAUSS_SIGMA), iaga.GAUSS_TAPS, fftbins=False)
assert max(abs(a - b) for a, b in zip(iaga.gaussian(), g / g.sum())) < 1e-15
print("the low-pass at 10, 20, 32 and 64 Hz and the Gaussian as SciPy's")

if os.environ.get("BDV_SEC") and os.environ.get("BDV_MIN"):
    sec = {n.split(":")[1]: xs for n, xs, _ in nrcmeasure.read_iaga2002(os.environ["BDV_SEC"])}
    mins = {n.split(":")[1]: xs for n, xs, _ in nrcmeasure.read_iaga2002(os.environ["BDV_MIN"])}
    gw = iaga.gaussian()
    for c in sec:
        vals = [x / 100 for x in sec[c]]
        worst = max(abs(round(iaga._filt(gw, vals, 0, 60 * i), 2) - mins[c][i] / 100) for i in range(1, 1439))
        assert worst <= 0.0100001, (c, worst)
    print("Budkov's minutes from its seconds: within 0,01 nT, its rounding")
else:
    print("BDV_SEC and BDV_MIN not set: Budkov's day skipped")
