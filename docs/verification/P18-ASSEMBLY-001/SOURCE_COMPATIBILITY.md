# P18-ASSEMBLY-001 — source compatibility

```text
SUBJECT:  how an assembly refuses to mix generations, and how a system knows it is
          stale
```

## The rule

Dimensions agreeing proves nothing. A remesh of the same body with the same controls
gives the **same node count, the same element count and the same numeric ids** — and
different geometry. Every input is therefore compared by *value* of its source, never by
size, and the whole check runs **before the first element is visited**, so a stale input
is reported as one and never as whatever numerical symptom it would have caused.

## The checks, in order (`AssemblyProblem`)

| # | Check | Refusal |
| --- | --- | --- |
| 1 | the numbering describes the model's current mesh (stamp and count) | `dof_map_mismatch` |
| 2 | the prepared loads' source equals the current load source for these loads | `load_source_mismatch` |
| 3 | the prepared boundary's source equals the current boundary source for these conditions | `boundary_source_mismatch` |
| 4 | the material resolves for this mode (P18-MAT-001) | `material_unavailable` |
| 5 | its `k` equals the `k` the model was validated against | `material_source_mismatch` |
| seam | the mesh has tetrahedra | `mesh_has_no_elements` |
| seam | the heat vector is `Ndof` long | `load_vector_mismatch` |
| seam | every convection unknown is in the numbering | `boundary_dof_out_of_range` |
| numeric | an element is degenerate, inverted, non-finite, or has an unusable material | `element_rejected` (names the element) |
| numeric | a sum overflowed | `non_finite_system` |
| numeric | the assembled arrays failed their own validation | `inconsistent_system` |

Eleven problems, ten reachable from valid public inputs; `inconsistent_system` is a
self-check of the assembler's own scatter (a mutation makes it fire, see MUTATIONS.md).
A test pins the **order**: a foreign numbering together with stale loads is reported as
`dof_map_mismatch`, ten times out of ten.

Why `ThermalModel` is taken: possession proves the geometry and the mesh were current
when it was built, and it borrows the mesh, so the stale-geometry and stale-mesh
questions need no separate check. The document is taken to resolve the material afresh
on every call, because a material is derived and never cached: a conductivity edit after
the model was built is refused, a density or specific-heat edit is simply read.

## What a system records (`ThermalSystemSource`)

A value, not a hash: no pointer, no address, no timestamp.

```text
body, mesh stamp, node count, geometry revision, mode,
material id, material revision, the canonical loads, the canonical CONVECTION conditions
```

`operator==` is `= default`, so a field cannot be added without being compared, and
`staleReasons` names which moved (`body, mesh, geometry, mode, material, loads,
convections`).

## What deliberately does not stale a system

| Edit | Stales it? | Reason |
| --- | --- | --- |
| a prescribed temperature (value, face, add, remove) | **no** | never enters K, C or Q; the constraint map has its own source |
| solver settings | no | tolerance and pivot floor do not enter K, C or Q |
| time step, end time, initial condition | no | `C / dt` is not formed here |
| the analysis revision | not used | it moves on any edit and would stale the system for a tolerance edit |
| a heat load | yes (`loads`) | |
| a convection coefficient, ambient or face, or adding a convection | yes (`convections`) | |
| the mode | yes (`mode`) | steady has no C |
| a material edit | yes (`material`) | whole-record revision, conservative and never wrong |
| a remesh | yes (`mesh`) | even with the same counts and ids |

Each row has a test; "material edit" is whole-record, as P17's `AssemblySource` carries
it, so a conductivity edit that would not change a result still stales conservatively.

## The remesh case, measured

The block remeshed with identical controls: 9 nodes, 12 tetrahedra and 12 boundary
facets in **both** generations, every `NodeId` and `ElementId` identical, the
`MeshStamp` different. The old 9-unknown system's dimension matches the new mesh, its
element order even reads the same, and it is stale by stamp alone (`mesh`). The old
numbering, prepared loads and prepared boundary are refused against the new mesh as
`dof_map_mismatch`; prepared afresh, the system assembles and is current.