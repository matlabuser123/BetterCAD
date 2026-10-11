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

## What is NOT yet evidenced

The conversion itself is P16's and is covered by P16-VIZ-001's qualification.
**This milestone has not yet produced an OCCT presentation for the deformed
buffer**, so there is no P17-VIZ measurement of a millimetre coordinate
reaching the viewer. The brief's required table of

```text
NodeId | x_m | u_m | scale | expected x_view_mm | actual x_view_mm
```

therefore has its first four columns measured here and its last two still
owed. Recorded as outstanding rather than implied — see
[REMAINING_SCOPE.md](REMAINING_SCOPE.md).
