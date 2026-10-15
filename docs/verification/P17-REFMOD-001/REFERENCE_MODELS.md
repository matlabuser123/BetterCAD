# P17-REFMOD-001 — the reference suite contract

Ten mandatory model IDs in twelve committed documents. RM-FEA-06 is a pair --
a body and the same body rigidly transformed -- and RM-FEA-07 is a pair -- a
root refinement and its mirror -- because comparing the halves IS the case,
exactly as RM-MESH-06 and RM-MAT-04 are pairs and a triple.

Every model is built only through the public document, parameter, sketch,
feature, material, meshing and structural APIs by
`examples/reference_models/StructuralModels.cpp`, carries its whole intent --
a synthetic steel assigned at document level, one `MeshControl` named "Mesh",
one `StructuralAnalysis` named "Study" -- and declares in
`StructuralReferenceModels.hpp` what it is for, whether it is expected to
solve, and which main dimension the model-change gate edits.

```text
MATERIAL    E 210 GPa, nu 0.3, rho 7850 kg/m^3 -- TEST / SYNTHETIC ENGINEERING
            DATA, declared in the header and compared against what the
            document RESOLVES through P15, exactly
```

## The models

| ID | Document | Geometry (mm) | Mesh | Loads | Restraints | Expected | Main parameter |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RM-FEA-01 | StructuralPatch | 40 x 30 x 20 block | 20 mm global: 9 nodes, 12 tets, ONE interior node | 2.5 MPa pressure on the top | fixed bottom | solve | patch_a 40 -> 48 |
| RM-FEA-02 | StructuralAxialBar | 24 x 18 x 90 bar along Z | 20 mm: 10 nodes, 16 tets | +50 MPa traction on the tip, along +Z | Uz on the base, Ux on x = 0, Uy on y = 0 (rollers) | solve | bar_l 90 -> 120 |
| RM-FEA-03 | StructuralCantilever | cylinder r 10, L 100 along Z | 10 mm, 0.25 mm deflection: 138 nodes, 489 tets (the coarse level) | -200 N total as a uniform traction on the tip, along -Y | fixed root | solve | cant_l 100 -> 80 |
| RM-FEA-04 | StructuralPressureBlock | 60 x 45 x 30 block | 20 mm: 8 nodes, 6 tets | 2 MPa pressure on the top | fixed bottom | solve | press_a 60 -> 72 |
| RM-FEA-05 | StructuralGravityBlock | 50 x 35 x 80 column | 20 mm: 10 nodes, 16 tets | gravity (0, 0, -9.80665) m/s^2, mode WITH GRAVITY | fixed bottom | solve | grav_h 80 -> 100 |
| RM-FEA-06 | StructuralTransformedBase | 90 x 55 x 24 block at the origin | 16 mm: 9 nodes, 12 tets | 20 MPa pressure on the face opposite the datum | fixed datum face | solve | sbase_a 90 -> 110 |
| RM-FEA-06 | StructuralTransformedPlaced | the same block on RM-MESH-06's rotated, translated frame | 16 mm: 9 nodes, 12 tets | the same pressure | the same support | solve | splaced_a 90 -> 110 |
| RM-FEA-07 | StructuralLocalRefinementRoot | 40 x 60 x 100 beam along Z | 20 mm global, 5 mm on the ROOT cap | -300 N total traction on the tip, along -Y | fixed root | solve | loc_l 100 -> 120 |
| RM-FEA-07 | StructuralLocalRefinementTip | the same beam | 20 mm global, 5 mm on the TIP cap (the mirror) | the same | the same | solve | loct_l 100 -> 120 |
| RM-FEA-08 | StructuralUnderConstrained | 60 x 40 x 25 block | 20 mm: 9 nodes, 12 tets | +0.1 MPa traction on the x = 0 face, along +X | Uz on the bottom AND Uz on the top: 8 DOFs, all in z | REFUSAL by the pivot gate | uc_a 60 -> 72 |
| RM-FEA-09 | StructuralStaleMesh | 50 x 40 x 30 block | 20 mm: 9 nodes, 12 tets | 1.5 MPa pressure on the top | fixed bottom | solve; the suite then makes the mesh stale | stale_c 30 -> 36 |
| RM-FEA-10 | StructuralUnresolved | 50 x 40 x 30 block | 20 mm: 9 nodes, 12 tets | 1 MPa pressure on the top, AND a traction on a face no entity sweeps (Side of entity 9991) | fixed bottom | REFUSAL at the load stage | unres_a 50 -> 60 |

Every face a load or restraint names is also a `NamedBoundarySet` of the
control, so `mesh-boundaries` shows the same regions the analysis uses.

## The oracles, per model

Every expected value is computed by `tests/reference/Analytic.hpp` from the
model's own parameters and the document's own material. The file includes no
BetterCAD header and cannot call a production formula.

**RM-FEA-01, the patch test.** An affine field `u = A x + b` with nine distinct
gradient entries is imposed on every node. Oracle: `eps = sym(A)` (engineering
shear `A_ij + A_ji`), `sigma` by Hooke's law from `lambda, mu`, von Mises
through the deviatoric contraction. A single skew tetrahedron is checked
first through the public kernels, including `f = Ke u` against the consistent
nodal forces of `sigma` on its four faces, and a rigid rotation that must give
no strain; then the meshed block, where `K u` must equal the boundary
tractions `sigma S / 3` per corner and be ZERO at the interior node.

**RM-FEA-02, the axial bar.** Rollers leave the Poisson contraction free, so
the exact solution `u = (-nu eps x, -nu eps y, eps z)` is affine and a linear
element carries it: `sigma = F/A`, `eps = sigma/E`, `delta = FL/AE`, `R = -F`,
`U = F delta / 2`, every node, every element, to 1e-9.

**RM-FEA-03, the cantilever.** Euler-Bernoulli `F L^3 / 3EI` plus the
Timoshenko shear term `F L / kappa G A` with `kappa = 6(1+nu)/(7+6nu)`, as the
TARGET of a three-level convergence study; the root reaction `-F` and the root
moment `F L` exactly at every level.

**RM-FEA-04, the pressure block.** `F = p A` through the nodal loads and `-F`
through the reactions, no moment about the face centroid, and the
volume-averaged axial stress EXACTLY `-p` by the virtual work of `u = z e_z`.

**RM-FEA-05, self-weight.** `W = rho V g`, the weight's moment about the
origin `(-b/2 W, +a/2 W, 0)` from the centre of volume, and the mean axial
stress EXACTLY `-rho g H / 2`.

**RM-FEA-06, the rigid transform.** Invariant: strain energy, every element's
von Mises and hydrostatic stress, the largest displacement magnitude, the
magnitude of the total reaction. Rotated by R: every displacement vector,
every nodal reaction, the total reaction. Boundary nodes follow `R x + t` to
kernel rounding; the interior node is judged separately.

**RM-FEA-07, local refinement.** No stress value at the clamped corner, which
is a singularity. The root band is finer than the tip band in the root
document and the reverse in the mirror; both caps resolve to their two facets
on every mesh; both refined meshes are more compliant than the unrefined one
and resolve a higher von Mises peak. Where that peak lands is recorded, not
asserted.

**RM-FEA-08, under-constrained.** Validation ACCEPTS (eight constrained
degrees of freedom), the factorisation REFUSES (`singular_system`, pivot
ratio ~1e-17), and no outcome exists. One restraint fewer is refused earlier
by the count; a fixed base makes the same model solve.

**RM-FEA-09, the stale mesh.** After a depth edit the mesh is
`stale: the geometry changed`, validation stops at the INPUT stage with
`mesh_stale`, nothing past it is measured, the solve is refused; remeshing
restores a solve to a different answer. A sizing edit is refused the same way.

**RM-FEA-10, the unresolved reference.** Validation stops at the LOAD stage
with `loads_unresolvable` after the restraint stage passed; the missing face
resolves to no facet (`MappingState::Unresolved`) while the real faces resolve
to their two; an unresolvable restraint is refused at the restraint stage.

## What each document must NOT contain

No node, no element, no displacement, no stress, no reaction: the save/load
case checks the bytes for every one of those words and requires the reloaded
document's mesher to hold nothing until something asks it to generate.
