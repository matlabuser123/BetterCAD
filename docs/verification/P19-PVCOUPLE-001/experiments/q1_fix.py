"""Q1, re-measured. The first form drew w from rng.random(), which returns
k * 2^-53; `1 - w` is then exact BY CONSTRUCTION OF THE SAMPLE and the question
was never asked. A real interpolation weight is dist_n / (dist_o + dist_n).
"""
import numpy as np

SEED = 20261108
rng = np.random.default_rng(SEED)
N = 2_000_000

do = rng.lognormal(0.0, 1.0, N)          # owner-to-face distance
dn = rng.lognormal(0.0, 1.0, N)          # face-to-neighbour distance
w = dn / (do + dn)                       # the weight toward the owner
dp = rng.lognormal(0.0, 2.0, N)
dv = rng.lognormal(0.0, 2.0, N)

print("Q1 RE-MEASURED, w as a real interpolation weight dist_n/(dist_o+dist_n)")
print(f"      samples                        {N}")
exact = (1.0 - (1.0 - w)) == w
print(f"      `1 - w` round-trips exactly    {exact.sum()} / {N}"
      f"   = {100.0 * exact.mean():.4f}%")

# The owner's visit and the neighbour's visit of the SAME face.
owner_side = w * dp + (1.0 - w) * dv
wprime = do / (do + dn)                  # the neighbour recomputes its own weight
neighbour_side = wprime * dv + (1.0 - wprime) * dp

same = owner_side == neighbour_side
scale = np.maximum(np.abs(owner_side), np.abs(neighbour_side))
rel = np.where(scale > 0.0, np.abs(owner_side - neighbour_side) / scale, 0.0)
print(f"      two visits bit-identical       {same.sum()} / {N}"
      f"   = {100.0 * same.mean():.4f}%")
print(f"      worst relative difference      {rel.max():.6e}")

# Is w + w' exactly 1?
compl = (w + wprime) == 1.0
print(f"      w + w' == 1 exactly            {compl.sum()} / {N}"
      f"   = {100.0 * compl.mean():.4f}%")

# CONTROL: addition commutes, so ONE computation written to both rows is
# trivially symmetric. This is the control that says the asymmetry above comes
# from recomputing the weight and not from the sum.
once = w * dp + (1.0 - w) * dv
print(f"      CONTROL one value, both rows   "
      f"{int((once == once).sum())} / {N}   = 100.0000%")
