# P19-MESH-001 — Adversarial review

Three passes:

1. the author's own questions while building;
2. an independent, read-only reviewer agent, run twice (a review, then a
   re-verification of the fixes);
3. a mutation campaign against the fixed code.

## 1. Found by the author before review

| # | What | Disposition |
|---|---|---|
| A1 | The first draft took the vertex opposite face k as `3 - k`. The table's opposite vertices are `{3, 2, 0, 1}`. Every P16 reference mesh failed with `InwardBoundaryFace` | Fixed: `kTetFaceOpposite` is a table. The names test checks each face omits exactly its opposite vertex, and that each face is outward on the unit tetrahedron. MP03 and MP04 pin it |
| A2 | The checklist's "non-manifold volume topology" was covered only by a face claimed three times. Cells touching at a vertex, along an edge, or not at all passed | Added `DisconnectedCells` (union-find over shared faces; root = lowest cell) with vertex, edge, no-contact and three-piece fixtures |
| A3 | The reference loop skipped RM-MESH-06's base model | It now iterates `kMeshReferenceModels`, requires 8 meshed bodies, and skips only RM-MESH-08 (no body) |
| A4 | ADR-058 left the tetrahedral quality question to be "measured" | Measured on every reference mesh. Finding F1 (poor FV quality) is recorded for P19-VALID-001 and the owner |
| A5 | No reference body has an unnamed face, so `unnamedPatches` was only compared with 0 | A drilled body (P16's own unnamed-wall case) gives 7 patches, 1 unnamed |
| A6 | The non-finite-volume branch had no test, because `MeshBuilder` refuses a non-finite coordinate | Coordinates of 1e120 m overflow the volume to +inf |

## 2. Independent review

### First pass: 0 blockers, 1 major, 4 minor, 8 nits, 10 surviving-mutant candidates

| # | Sev | Finding | Disposition |
|---|---|---|---|
| F1 | **major** | No check that a shared face is traversed in opposite directions. Two positive cells folded onto one side of their face (B = (1,2,3,4), C = (1,2,3,5)) passed every check, so the ADR's "S . d > 0 for any interior face of two positive cells" was false | **Fixed**: `InconsistentFaceOrientation`, an exact combinatorial test (the neighbour's winding must be a rotation of the owner's reversed). Test: the reviewer's fold, rotated corner orders, and a rotated genuine neighbour accepted. The ADR's bound is restated as a four-step argument that depends on the check |
| F2 | minor | The interior-face and duplicate-triangle tests could not detect a missing `continue`: the spoiled triangles were outward | Fixed: the spoiled triangles (interior face, three-way face, duplicate) are wound into their cell, so a fall-through adds a second finding. Counts asserted |
| F3 | minor | `requireCfdMesh`'s failure path and `cfdMeshProblem` were never run | Fixed: exported `cfdMeshRefusal` (what the gate returns) and `CfdMeshInspection::firstProblem()`. The exact message and code are asserted |
| F4 | minor | The gate pairs map and mesh only by facet count | Recorded, not changed: the pairing belongs to the `CfdModel` (one held entry, ADR-060). The header and ADR say so, the reference test asserts `mesh.owns(map.meshStamp())`, and the wording became "a different number of facets" |
| F5 | minor | `P16_AUDIT.md` cited but missing | Written |
| F6 | minor | Two ADR table rows cited the wrong P16 mechanism (positivity is `validate()` → `InvalidMesh`; winding is `tetrahedralBoundary`, and `BoundaryNotConforming` ignores it) | Fixed |
| F7 | nit | Header claimed node-for-node agreement is checked | Reworded: orientation is checked, geometrically |
| F8 | nit | Skewness formula trivially 0 on boundary faces | Skewness defined for interior faces only |
| F9 | nit | A zero/NaN dot product was reported as "wound into" | Own message, "is degenerate against cell"; the flat-cell test asserts it |
| F10 | nit | `count` not "how many times" for two problems | Defined: DisconnectedCells = pieces beyond the first; Unattributed = defects counted by the report (min 1). Both tested |
| F11 | nit | ADR-032 erratum's condition unsatisfiable | Reworded: such a mesh would be refused, which is the right answer until an ADR joins regions |
| F12 | nit | Hand-built cases compile only with the reference-models target | Kept: matches practice; all three presets build it |
| F13 | nit | The accepted-pinch decision is untested | Edge-only adjacency in a face-connected fan is now shown accepted. A true boundary vertex pinch is recorded as not shown |

### Re-verification: every code fix correct; no new major or minor; 5 nits

| # | Nit | Disposition |
|---|---|---|
| N1 | "both lie on the same side" is false when one of the pair is inverted (the volume refusal also fires) | Reworded to the combinatorial fact: "traverse their shared face ... in the same direction" |
| N2 | The quality test's comment stated the bound without the orientation condition | Fixed |
| N3 | ADR said every spoiled triangle is wound inward; a triangle on no face cannot be | Reworded |
| N4 | The F13 justification overstated the size of a fixture | The fan fixture was added (above); the wording now covers only the boundary pinch |
| N5 | An overlong header line | Rewrapped |

## 3. Mutation campaign

**31 probes, 31 killed, 0 survived, 0 equivalent.**

The campaign ran against the fixed code. Each probe replaces one exact text (which
must occur exactly once), rebuilds `bettercad_tests` (debug-ext), runs the 86
`CfdMesh_`/`CfdInput_`/`CfdData_`/`CfdFluid_` cases, and restores the file byte for
byte. After the campaign both sources hash-equal the clean copies taken before it
(`CfdMesh.cpp` 17099c4, `CfdMesh.hpp` d24e3bd).

Two probes were **VOID on the first run**, because the mutant did not compile.
`MP05` (`if (false)`) left `reversed()` unused, and `MP22` (`1`) left `defects`
unused, and -Werror refuses both. They were rewritten to compilable mutants of the
same intent (`claims == 2 && false && ...`, `std::min(defects, 1)`) and re-run
alone. Both were then killed. `progress.txt` keeps both runs.

| Probe | Mutation | Killed by |
|---|---|---|
| MP01 | a zero-volume cell accepted | RefusesAZeroOrNegativeCellVolume |
| MP02 | a non-finite volume accepted | RefusesAZeroOrNegativeCellVolume (overflow section) |
| MP03 | opposite vertex = 3 - k (the first draft's bug) | 13 cases, every P16 mesh |
| MP04 | opposite-vertex table's last two swapped | 14 cases |
| MP05 | shared-face orientation not checked | RefusesTwoCellsFoldedOntoOneSideOfTheirFace |
| MP06 | `reversed()` compares with the owner's own winding | 14 cases |
| MP07 | `reversed()` tries only one rotation | 5 cases, including the P16 meshes |
| MP08 | a shared face does not join its cells | 13 cases |
| MP09 | a union keeps the higher root | the stray cell named in the message |
| MP10 | the stray is any non-root cell | the stray cell named in the message |
| MP11 | two pieces accepted | the disconnected cases |
| MP12 | disconnected count saturates at 1 | the three-piece section |
| MP13 | a three-way face counted as interior | the non-manifold cases |
| MP14 | only a two-way face refuses a triangle on it | the three-way-face section |
| MP15 | the shared-face ownership refusal falls through | the inward-wound spoiled triangle |
| MP16 | a duplicate triangle accepted | the reversed duplicate |
| MP17 | outward test reversed | 13 cases |
| MP18 | a degenerate triangle passes | the flat-cell section |
| MP19 | an uncovered face accepted | 3 cases |
| MP20 | an incomplete attribution accepted | 2 cases |
| MP21 | a report counting other facets accepted | the facet-count section |
| MP22 | attribution count saturates at 1 | 4 cases (the count of 3, and every empty report) |
| MP23 | two findings reported out of order | ReportsEveryProblemInTheDocumentedOrder |
| MP24 | the refusal joins with a comma | TheRefusalNamesTheControlAndEveryFinding |
| MP25 | the refusal's error code | TheRefusalNamesTheControlAndEveryFinding |
| MP26 | `firstProblem` reports the last finding | 2 cases |
| MP27 | the contract records no geometry revision | EveryP16ReferenceMeshSatisfiesTheContract |
| MP28 | the contract counts no unnamed patch | AnUnnamedPatchIsCountedNotRefused |
| MP29 | the contract counts only named patches | AnUnnamedPatchIsCountedNotRefused |
| MP30 | a tally keeps only its last count | the inward count of 4 |
| MP31 | an empty mesh not refused as empty | 2 cases |

The script is `qualification/mutation/mutate-mesh.ps1`. The per-probe test logs
and `progress.txt` are beside it.
