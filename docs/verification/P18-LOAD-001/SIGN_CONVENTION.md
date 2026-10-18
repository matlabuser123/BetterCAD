# P18-LOAD-001 — the sign convention

```text
SUBJECT:  what a positive and a negative load mean, where the convention comes
          from, and the tests that would fail if the sign were flipped, dropped
          or made to depend on a triangle's winding
SOURCE:   P18-DATA-001 froze it; this milestone obeys it exactly
```

## The convention, written once

```text
positive q''     heat flows INTO the body through the face     (the face is heated)
negative q''     heat flows OUT of the body through the face   (extraction)
positive q'''    heat is GENERATED in the body
negative q'''    heat is REMOVED in the body                   (a volumetric sink)
zero             a valid load: a zero contribution; the target must still resolve
```

This is not a choice made here. `include/bettercad/thermal/ThermalData.hpp`
(P18-DATA-001) documents `SurfaceHeatFluxLoad` as "POSITIVE IS INTO THE BODY ...
a negative one is heat leaving through it, which is ordinary and is accepted
rather than refused. The same convention `HeatBalance` uses, so a load's sign
and its appearance in the balance agree", and `VolumetricHeatSourceLoad` as
"Positive generates heat, negative is a volumetric sink". The brief's
alternative (positive = outward Fourier flux) was **not** adopted, because
P18-ARCH already froze this one and the brief says to obey it exactly. There
is one convention in the tree: load, balance, CLI and visualisation will read
the same sign.

## Why no normal is needed

The sign belongs to the **scalar**. The heat applied through a facet is
`q'' A` with `A > 0`, whichever way the triangle is wound, so:

```text
reversing a triangle's vertex order      changes nothing   (tested)
a pressure, by contrast, is p n_out      and WOULD flip     (P17; not carried over)
```

Nowhere in `ThermalLoadVector.cpp` is a normal, a cross-product direction or an
orientation computed; the area is `meshing::triangleArea`, an unsigned
magnitude. The convention therefore cannot disagree with the mesh's boundary
orientation, and the "boundary-normal independence" the brief asks for holds by
construction rather than by a check.

## Distinct from the Fourier flux

The applied scalar `q''` is not the physical vector `q = -k grad(T)` that
P18-POST-001 will recover. A positive applied flux is heat entering; the
recovered `q` is a vector whose sign follows the temperature gradient. Keeping
the two apart is the reason the load is a signed scalar on a face and not a
vector.

## Signed values are accepted exactly

No `abs`, no clamp, no warning for a negative value, no restriction to
`>= 0` (there is no architectural reason for one: the thermal problem is
linear under constant properties). Only non-finite values are refused.

## The tests that hold it

```text
SignConventionIsPositiveIntoTheBody   q'' = +12 -> +36 W total, [12 12 12] W;
                                      q'' = -12 -> -36 W, [-12 -12 -12] W;
                                      q'' = 0 -> 0; likewise q''' = +24 / -24 / 0
ZeroAndNegativeValuesArePreservedExactly
                                      negative vector == minus the positive one,
                                      entry by entry, on the real block; a zero
                                      load keeps its resolved target; a zero load
                                      on an unresolved target is still refused
SignedCancellation                    +L and -L cancel (surface: exactly 0;
                                      volume: 0.83 eps)
FacetWindingDoesNotChangeTheHeatInput the same triangle wound both ways: equal
Mutations L06 (flux sign flipped), L07 (abs of the flux), L08 (abs of the
generation)                           all killed
```
