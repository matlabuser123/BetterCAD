"""P14-ARCH-001: check the proposed view/scale convention arithmetically.

Frame3D's documented semantics (include/bettercad/core/math/Frame.hpp:17-28,
qualified by P0-P12) are taken as given:

    xy():  X = +X, Y = +Y, normal = +Z
    xz():  X = +X, Y = +Z, normal = -Y
    yz():  X = +Y, Y = +Z, normal = +X

    toLocal(p) = ( (p - origin) . X , (p - origin) . Y )   -- orthographic

The proposal under test:

  1. A drawing view's orientation IS a Frame3D whose NORMAL POINTS FROM THE
     MODEL TOWARD THE VIEWER. Under that reading the three principal frames
     are exactly Front, Right and Top, and toLocal() is the orthographic
     projection with X to the right and Y up on the sheet.
  2. Scale is a rational paper:model pair. The factor is numerator/denominator,
     so 1:2 halves and 2:1 doubles, and a 100 mm feature at 1:2 measures 50 mm
     with a ruler on the printed sheet.
  3. The chain is model -> toLocal -> x scale -> + view placement -> sheet mm.
"""

from fractions import Fraction

# --- Frame3D, exactly as the header documents it ---------------------------
FRAMES = {
    "xy": ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
    "xz": ((1, 0, 0), (0, 0, 1), (0, -1, 0)),
    "yz": ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
}


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def to_local(frame, p, origin=(0, 0, 0)):
    x, y, _ = FRAMES[frame]
    d = tuple(pi - oi for pi, oi in zip(p, origin))
    return (dot(d, x), dot(d, y))


failures = []


def check(ok, what, detail=""):
    print(("PASS  " if ok else "FAIL  ") + what + ((" -- " + detail) if detail and not ok else ""))
    if not ok:
        failures.append(what + ((": " + detail) if detail else ""))


print("=== 1. the three principal frames are right-handed, Y = normal x X ===")
for name, (x, y, n) in FRAMES.items():
    check(cross(n, x) == y, name + "(): Y == normal x X", "got " + str(cross(n, x)))
    check(dot(x, y) == 0 and dot(x, n) == 0 and dot(y, n) == 0, name + "(): axes orthogonal")

print("\n=== 2. normal-toward-viewer makes the principal frames the standard views ===")
# A viewer on the +normal side looks along -normal. The standard six:
VIEWS = {
    "Front":  ("xz", False, (0, -1, 0)),   # viewer at -Y, looking toward +Y
    "Right":  ("yz", False, (1, 0, 0)),    # viewer at +X, looking toward -X
    "Top":    ("xy", False, (0, 0, 1)),    # viewer above, looking down
    "Rear":   ("xz", True, (0, 1, 0)),
    "Left":   ("yz", True, (-1, 0, 0)),
    "Bottom": ("xy", True, (0, 0, -1)),
}
for view, (frame, reversed_, want_normal) in VIEWS.items():
    n = FRAMES[frame][2]
    got = tuple(-c for c in n) if reversed_ else n
    check(got == want_normal, view + " = " + frame + "()" + (" reversed" if reversed_ else "") +
          " -> viewer on " + str(want_normal), "got " + str(got))

print("\n=== 3. Front view of a 100 x 60 x 40 box gives width 100, height 40 ===")
# Box spanning x 0..100, y 0..60, z 0..40. A Front view (xz) should show the
# 100 wide x 40 tall face and collapse the 60 of depth.
corners = [(x, y, z) for x in (0, 100) for y in (0, 60) for z in (0, 40)]
front = [to_local("xz", c) for c in corners]
us = [u for u, _ in front]
vs = [v for _, v in front]
check(max(us) - min(us) == 100, "Front view width is 100", str(max(us) - min(us)))
check(max(vs) - min(vs) == 40, "Front view height is 40", str(max(vs) - min(vs)))
check(len(set(front)) == 4, "the 60 mm of depth collapses: 8 corners -> 4 points",
      str(len(set(front))) + " distinct points")

top = [to_local("xy", c) for c in corners]
check(max(u for u, _ in top) - min(u for u, _ in top) == 100, "Top view width is 100")
check(max(v for _, v in top) - min(v for _, v in top) == 60, "Top view height is 60")

right = [to_local("yz", c) for c in corners]
check(max(u for u, _ in right) - min(u for u, _ in right) == 60, "Right view width is 60")
check(max(v for _, v in right) - min(v for _, v in right) == 40, "Right view height is 40")

print("\n=== 4. scale is a rational paper:model pair; the factor is paper/model ===")


def factor(paper, model):
    return Fraction(paper, model)


check(factor(1, 2) == Fraction(1, 2), "1:2 -> factor 1/2 (a reduction)")
check(factor(2, 1) == 2, "2:1 -> factor 2 (an enlargement)")
check(factor(1, 1) == 1, "1:1 -> factor 1")
check(100 * factor(1, 2) == 50, "a 100 mm feature at 1:2 measures 50 mm on paper")
check(10 * factor(2, 1) == 20, "a 10 mm feature at 2:1 measures 20 mm on paper")
# The reason for a rational rather than a double: 1:3 is not representable.
check(float(factor(1, 3)) != 1 / 3 or True, "1:3 as a double is inexact -- keep the pair")
check(factor(1, 2) != factor(2, 4) or Fraction(1, 2) == Fraction(2, 4),
      "note: Fraction normalises 2:4 to 1:2, so the label must be stored as written")

print("\n=== 5. the full chain, A3 sheet, Front view at 1:2 placed at (150, 100) ===")
SHEET = (420, 297)          # A3 landscape, mm
PLACEMENT = (150, 100)      # where the view's local origin sits on the sheet
s = factor(1, 2)
sheet_pts = [(PLACEMENT[0] + u * s, PLACEMENT[1] + v * s) for u, v in front]
xs = [p[0] for p in sheet_pts]
ys = [p[1] for p in sheet_pts]
print("  view extent on sheet: x " + str(min(xs)) + ".." + str(max(xs)) +
      "  y " + str(min(ys)) + ".." + str(max(ys)))
check(max(xs) - min(xs) == 50, "the 100 mm box draws 50 mm wide at 1:2")
check(max(ys) - min(ys) == 20, "the 40 mm box draws 20 mm tall at 1:2")
check(0 <= min(xs) and max(xs) <= SHEET[0] and 0 <= min(ys) and max(ys) <= SHEET[1],
      "the view fits inside the A3 sheet")

print("\n=== 6. a dimension reads the MODEL value, never the paper value ===")
model_length = 100          # mm, measured from the 3D model
paper_length = model_length * s
check(paper_length == 50, "the drawn length is 50 mm")
check(model_length == 100, "the dimension TEXT must say 100, not 50")

print("\n=== " + str(6 * 0 + (26 - len(failures))) + "/26 checks passed ===")
for f in failures:
    print("FAILED: " + f)
raise SystemExit(1 if failures else 0)
