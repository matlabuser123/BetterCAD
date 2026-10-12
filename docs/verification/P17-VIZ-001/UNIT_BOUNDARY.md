# P17-VIZ-001 — the unit boundary

The first of the two carried traps. P16-VIZ-001 found it the hard way and
recorded both the defect and the fix; this milestone's obligation is not to
re-solve it but to go through the same single conversion.

## The three spaces, frozen

```text
P16 engineering mesh coordinates     SI METRES   meshing::Node::position is a Point3D
P17 displacement                     SI METRES   Translation3D of Length
OCCT model / viewer space            MILLIMETRES
```

`core/geometry/occt/OcctBody.hpp` states why the kernel is millimetres: "OCCT
model space uses millimetres: its default precision (1e-7) and tolerances are
designed for millimetre-scale models."

```text
viewer_mm = 1000 * engineering_m
```

## There is exactly one conversion, and it is not in this milestone

```cpp
// src/renderer/occt/OcctMeshPresentation.cpp
[[nodiscard]] double toModelLength(double metres) noexcept {
    return geometry::occt::toModel(Length::fromSi(metres));
}
```

P16-VIZ-001 placed it there deliberately, and its comment says why: "it belongs
HERE rather than in MeshView: the adapter is unit-honest SI and is tested
headlessly, and millimetres are a fact about the kernel." It is described in
that file as "the project's only one".

**So P17-VIZ adds no conversion at all.** The deformed buffer is produced in
metres and converted by the same adapter as the undeformed buffer, which makes
the two numerically consistent by construction rather than by two call sites
agreeing:

```text
x_view_mm = 1000 * (x_m + s * u_m)
```

is obtained as `toModelLength(x_m + s * u_m)`, with the multiplication by 1000
occurring once, inside `toModelLength`.

### Checkable, and checked

```text
grep for `1000`, COMMENTS STRIPPED, in
  src/renderer/StructuralResultView.cpp                           0 matches
  src/renderer/StructuralInspection.cpp                           0 matches
grep for `toModel` in either                                      0 matches
```

The only `1000` in either file is the comment asserting the rule, which is why
the count above is taken with comments stripped: an unqualified grep returns 1,
and the first draft of this document claimed 0 — a quoted figure nobody had
re-derived.

The conversion is reached only through the presentation adapter, which both
buffers pass through.

## How P16 found the defect, kept here because it is the reason for the rule

Recorded in `OcctMeshPresentation.cpp`:

> a mesh displayed ALONE looked perfect, because FitAll framed the only thing
> in the scene. Displayed beside its own CAD solid and then fitted, it became a
> sub-pixel speck — 0 covered pixels — and the test that caught it was the one
> asking whether hiding the CAD leaves the mesh visible. The clue was that the
> same box measured 32674 pixels as a mesh and 54120 with the solid added: one
> silhouette, two scales.

A mesh in metres beside a body in millimetres is **1000 times too small**, and
alone it is invisible as an error.

## Measured: everything leaving this layer is metres

`StructuralViz_EverythingLeavingThisLayerIsSiMetres`.

| check | result |
| --- | --- |
| every view vertex equals the canonical `Mesh::findNode(id)->position` component, exactly | PASS, all vertices |
| the widest x of a 40 × 30 × 20 mm body | **0.040**, within 1e-9 — metres, not 40 |
| the deformed buffer's widest x at scale 1 | between 0.039 and 0.041 — unchanged by three orders |

The second row is the one that matters: if this layer were secretly converting,
the extent would be 40 rather than 0.04, and the first row would still pass.

## Measured: the deformed buffer reaches model space at the right scale

The deformed display now exists (ADR-043), so this is no longer owed.

`StructuralViz_ADeformedMeshOverlaysItsOwnCadSolidAtTheSameScale`.

A boundary mesh of a solid has the **same silhouette** as the solid, so from a
fixed camera the solid alone, the mesh alone and both together cover the same
pixels. A scale error between them gives three different numbers, and a factor
of 1000 gives a speck. At a displacement of micrometres on a 40 mm bar the
deformation is far below one pixel, so the deformed buffer must satisfy the
same three-way equality:

Measured at 320 × 240, isometric, `fitAll`:

| displayed | covered pixels |
| --- | --- |
| the CAD solid alone | **42036** — the reference, asserted `> 0` first so the equalities are not four zeros agreeing |
| the **deformed** mesh alone | **42036** |
| the CAD solid **and** the deformed mesh | **42036** |
| the undeformed mesh alone | **42036** |

**All four exactly equal.** The last row is the same claim from the other side:
both buffers pass through the one conversion in the presentation constructor,
so they cannot disagree.

The contrast with the defect this test shape exists to catch is the point.
P16-VIZ-001 measured **32674** for the mesh and **54120** for the two together
— one silhouette, two scales. Here the four numbers are one number.

Two more measurements pin the ends of the range:

```text
scale 0      the deformed image is BYTE-IDENTICAL to the undeformed one
             (rgb == rgb), and the buffer is first asserted equal to
             view.positions() element by element -- so the case cannot pass
             for the wrong reason

scale 2e4    the image DIFFERS, and the buffer's largest coordinate change is
             measured as > 1e-3 m BEFORE anything is drawn, so a failure is
             about the display rather than the arithmetic
```

Without the second, the first would be satisfied by a deformed path that
ignored its buffer entirely.

## The brief's table, and the column no API can fill

The brief asked for:

```text
NodeId | x_m | u_m | scale | expected x_view_mm | actual x_view_mm
```

The first four columns are measured above and in
[MEASUREMENTS.md](MEASUREMENTS.md). **The last column cannot be filled, and
the reason is a design property rather than an omission: nothing in the public
API returns a model-space coordinate.**

```text
MeshPick        carries a PresentationId and a render triangle index -- an
                index, deliberately, because "an AIS object has no business
                producing engineering identity"
RenderedImage   carries pixels: at(x, y) and coverage(background)
MeshPresentation's millimetre corners are private to the OCCT adapter, which
                is what architecture rule 1 enforces
```

Adding an accessor that returned those corners would be **behaviour that
exists only for tests**, which this project forbids outright. So the column
stays empty and the measurement above stands in its place — and it is not a
weaker one, because it is the measurement that actually caught the defect:
P16-VIZ-001 found the metres-versus-millimetres error precisely by displaying
the mesh beside its own solid, after the mesh *alone* had looked perfect.

A per-node millimetre readout would have agreed with a wrong answer just as
readily, as long as the test computed its expectation the same wrong way. The
overlay comparison cannot, because the solid's scale is supplied by the kernel
and not by the test.
