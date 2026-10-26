# RM-THERM-03 — Composite wall: recorded as NOT representable in P18 v1

**There is no `ThermalComposite…` document, and that is the result.** A layered wall needs two conductivities in one body. P18 v1 cannot supply them, for two independent reasons that the suite *demonstrates* rather than asserts:

1. **One material per document.** `Document::materialAssignment()` is a single slot (ADR-049). A second `assignMaterial` *replaces* the first. `RM03_UnsupportedLayeredCapabilityExplicit` (section 1) assigns a second material to the slab and shows the first is no longer assigned to anything, and that the slab then has the **same gradient in every element** — the signature of a homogeneous wall and the opposite of a composite one.
2. **One solid per mesh.** P16 meshes one solid. Section 2 builds ONE body of two solids (two rectangles in one sketch, extruded) and shows `meshing::volumeMeshFor` refuses it by name, `VolumeMeshFailure::MultipleSolids`; so a body of two solids cannot stand in for two layers. (Two separate documents would each have one material and one solid, and are not a layered wall either — they are two unconnected problems.) Section 1's content is the slot check (a second assignment replaces the first) **and the second material's conductivity being the one used**: the same 100 K across 1 m now moves `2 × 0.25 × 100 / 1 = 50 W`, not the first material's 200 W. The "same gradient in every element" check on its own would hold for any k on a two-held-face slab; it is kept only as the signature of a homogeneous wall.

## The closed form is still verified (against hand arithmetic only)

Wall: A = 0.2 m²; layer 1 = 0.1 m of k = 2; layer 2 = 0.15 m of k = 5; 400 K → 300 K.

```
R1 = 0.1/(2×0.2) = 0.25 K/W      R2 = 0.15/(5×0.2) = 0.15 K/W      Rtotal = 0.40 K/W
Qdot = 100/0.40 = 250 W          T_interface = 400 − 250×0.25 = 337.5 K     q = 250/0.2 = 1250 W/m²
gradient layer 1 = −1250/2 = −625 K/m      layer 2 = −1250/5 = −250 K/m      (flux continuous, gradient jumps)
```

`RM03_ClosedFormAgainstHandArithmetic` checks `ThermalAnalytic.hpp: compositeWall` against these (`kClassA`) and flux continuity `−2·(−625) = −5·(−250)`. Ready for the day a multi-material document exists.

## Why no single-material stand-in is built

`RM03_AnAveragedConductivityIsNotACompositeWall`: the series conductivity `k_eff = L/Σ(Li/ki) = 0.25/0.08 = 3.125 W/(m K)` gives the **right heat rate** (250 W) and the **wrong** interface temperature (**360 K**, not 337.5 K), a uniform gradient of −400 K/m instead of −625 / −250, and no flux jump. A document using it would pass a heat-rate check and be called a composite wall by anyone reading its name. The suite builds neither.

## Status in the TODO

"Composite-wall conduction" is ticked with the annotation that it **records what it cannot**: a capability absent from P18 v1 is documented, tested for its refusal, and given a verified oracle — not faked. Owner finding **F-R3**: a multi-material document needs per-region material assignment (a data-model change) *and* a multi-solid mesher (a P16 change); neither is in scope here.
