# P16-ARCH-001 — architecture adversarial review

An architecture cannot be adversarially reviewed by running tests, so this is done the only way
it can be: by attacking each decision with a concrete scenario and checking the answer **against
the tree** rather than against the ADR that made the claim.

Six findings. **Five were found while the architecture was being written and are already fixed in
the ADRs**; recording them is the point, because each would have become a defect in a later P16
milestone rather than a paragraph here. One is a residual risk with no owner yet and is recorded
as such.

```text
REVIEWED     ADR-030, ADR-031, ADR-032, ADR-033, the CheckLayering.cmake change
FINDINGS     6 -- 5 resolved in the ADRs, 1 recorded as residual
PRODUCTION
DEFECTS      0 new. The one production change is additive and was proven to fire
```

---

## Findings

### F1 — The attribution rule would have refused to mesh a box

**Attack.** ADR-032's first draft said "a facet that cannot be attributed to a CAD face is a
meshing failure". Mesh a plain `makeBox` part.

**Result.** It would have failed. `Faces.hpp` says "Other operations give bodies without names",
and `Primitives.hpp` confirms it at its sharpest: `makeBox`, `makeCylinder` and `makeSphere` take
**no namer**, so a primitive's faces carry no `FaceName` at all. The rule would have refused the
simplest solid in the system, and the reference models are full of them.

**Resolution.** ADR-032 now states that an unnamed face is normal and not a failure; the facet is
still attributed to its face, and only the reverse direction — a *user-supplied* `FaceName` that
resolves to no facet — is a failure. Fixed while writing.

### F2 — The face-to-name relation is many-to-many, and the obvious field shape is wrong twice

**Attack.** Store `optional<FaceName>` on each facet. Union two named blocks; chamfer an edge so a
named face splits.

**Result.** Wrong in both directions, and `Faces.hpp` already documents why: "a face split in two
carries its name on both parts, **faces merged into one carry all their names**, a face the
operation removed carries none". So one name may cover several faces and one face may carry
several names. Any design that assumed a name identifies one face would break on the first
boolean — that is, on essentially every real part.

**Resolution.** ADR-032 requires the relation to be stored as many-to-many, and a validation case
for each direction. Fixed while writing.

### F3 — The rule meant to contain a mesh backend did not exist, and the OCCT rule would not have caught one

**Attack.** Add `#include <nglib.h>` to `src/features/`. Does the architecture test fail?

**Result.** **No.** Rule 1 recognises an OCCT header by its `.hxx` extension; every candidate
backend ships `.h` headers (`nglib.h`, `tetgen.h`, `gmsh.h`, `CGAL/…`, `mmg/…`). Rule 2 matches
only Qt's shape, rule 3 only `bettercad/…` headers. A backend could have been included from a
public header, from `features`, or from the GUI, and the gate would have passed. P16's invariant
"backend-specific behaviour must remain behind a meshing interface" was **unenforceable**.

**Resolution.** Rule 5 added, and *proven to fire* rather than assumed — see the Tests section of [README.md](README.md). Two
mutations fail with the right message, the same include inside `src/meshing/netgen/` passes, and
the restored tree is back to 398 files and 0 violations. Fixed in this milestone.

### F4 — `bettercad::Id` would have given mesh nodes a promise they cannot keep

**Attack.** `using NodeId = Id<NodeIdTag>`, the uniform move, matching sixteen other identifiers.

**Result.** `Id.hpp`'s own contract is "identities, never container indices: they stay stable when
other objects are added or removed, and they are persisted with the document", and `IdAllocator`
exists so "a stale reference can never silently resolve to a newer item". A mesh node is unstable
across remesh, never persisted, effectively a dense index, and reused on every generation. The
never-reuse guarantee — the mechanism that makes a stale CAD reference *safe* in BetterCAD — would
have been silently absent, so two meshes would hand out node 1 for different points and nothing
would notice.

**Resolution.** ADR-031 rejects it, keeps mesh handles out of `core/Id.hpp` entirely, and requires
the mesh to carry a generation counter so a stale handle is refused rather than reinterpreted.

### F5 — Two separate paths to a solver-facing mesh, one of which skipped validation

**Attack.** ADR-030's first draft said the validating path "returns a diagnostic otherwise" and
inspection "is documented as not solver-facing". Take the inspected mesh of a *failed* validation
and pass it to P17.

**Result.** Nothing stopped it. Both paths returned the same `Mesh`, so P16's invariant "every
solver-facing mesh must pass validation before use" rested on a comment — exactly the kind of
invariant that holds until someone is in a hurry.

**Resolution.** ADR-030 now separates them by type: a `ValidatedMesh` constructible only by the
validating path, and solver-facing APIs take nothing else. Enforced by the compiler. Fixed while
writing.

### F6 — RESIDUAL: a control whose body is deleted

**Attack.** Create a `MeshControl` for a body, mesh it, then delete the feature that produced the
body.

**Result.** The control is a document object and a graph node, so deleting its target must leave it
explicitly unresolved — the shape P15 established for a dangling material assignment. But nothing
in this milestone *implements* that, and the failure mode if it is missed is bad in a specific
way: a control whose body is gone could mesh nothing and report success, which is the "nominally
valid current mesh" P16's invariants forbid.

**Status.** Recorded, not resolved. This is an architecture milestone and there is no code to fix.
It is owed by `P16-DATA-001` and `P16-CMD-001`, is named in ADR-030's validation list, and is
carried into `KNOWN_LIMITATIONS` so it cannot be lost between milestones.

---

## Attacks that found nothing, and what was checked

Recorded because "nothing found" is only worth anything if the attack was real.

**Could a mesh be believed valid after an undo returns the document to an earlier state?** No, and
not by luck. Every write to `revision_` in `Document.cpp` is `++revision_`, the only assignment is
a copy preserving it, and undo applies its inverse through the same `restore*` entry points, so it
increments too. The revision is monotonic, so a stamp can never match a different state carrying
an earlier number. There is no ABA hazard. **Checked in the source, not inferred from the header
comment.**

**Could a mesh go stale on disk?** It cannot be on disk. Nothing derived is persisted, so a stale
mesh can only exist inside one process lifetime against one `Document` instance — which bounds the
problem to what the stamp can see.

**Could a copied document inherit another's meshes?** No. The mesher binds to one document, which
is the `Regenerator`'s documented behaviour — "the regenerator binds to the first document it is
used with" — and the same precedent covers the copy case.

**Could geometry change without the document changing, so the stamp matches stale geometry?** Yes,
by exactly one route: the carried configuration-override defect. That is the one case that is
**refused** rather than stamped, reusing P15-MASS-001's guard unchanged. No second route was found.

**Could the licence rule be sidestepped by running a GPL mesher as a subprocess?** It could under
the rule as first written, which said "linking". Tightened to "using it by any mechanism", with the
engineering objections recorded too, so the rule does not rest on a licence argument alone.

**Could sharing layer 4 between `drawing` and `meshing` create a cycle?** No. The layering rule is
strictly-lower, so same-layer modules cannot include each other at all — the test enforces it, and
`renderer`/`scripting` already share 6. The cost is real and accepted: a drawing can never depict a
mesh. Nothing in P16 or P17 wants that.

**Does placing `meshing` at 4 force a renumbering later?** No, and that is why 4 rather than 3:
`assembly` (3) stays reachable, so meshing an assembly occurrence later needs no renumbering, which
both ADR-006 and ADR-015 had to do. Verified by mutation: a file in `src/meshing/netgen/` including
`bettercad/features/FaceReferences.hpp` passes, which also proves the new layer entry is registered
— without it the test reports "unknown module 'meshing'".

**Is `Poly_MergeNodesTool` the way to weld the duplicated surface-mesh nodes?** No, and it is a
trap worth naming: its own header says it merges nodes "for visualization purposes … but split the
ones on sharp corners at specified angle". Splitting at sharp corners is the opposite of
watertight, so it must not be used to build an engineering surface mesh.

**Is `FaceId` the right key for mesh-to-geometry mapping?** No. `grep -rn "\bFaceId\b" include/
src/` returns one hit — the alias's own definition. It is unused, and its comment says persistent
naming across regenerations is future work. Using it would give the mapping an identity with no
defined behaviour across the regeneration the mapping exists to survive. ADR-032 uses `FaceName`
and leaves `core/Id.hpp` untouched.

**Is CGAL admissible on licence?** cgal.org says "the Kernel and Support libraries are under the
LGPL", which read alone would have admitted it. The per-package licence for **3D Mesh Generation**
is `GPL`. Reading the front page instead of the package would have been wrong, and the engineering
objection is independent anyway: `Mesh_3` re-discretises the boundary, so a facet need not lie on
the CAD face, which makes ADR-032's mapping unsound.

**Is MMG a candidate?** No, and its licence file says why: it is "for the tetrahedral mesh
**modification**". It adapts an existing mesh; it does not generate one from a BRep. Admissible by
licence, wrong by capability.

**Did the scope widen?** The one production change is eleven lines of comment and four of rule in
the architecture gate, plus a layer-table entry — both of which ADR-033 requires and neither of
which implements any meshing. No mesh type, no `src/meshing/`, no backend, no dependency. The
milestone's own forbidden list (`P16-DATA-001` and later) is untouched.

**Was a gate relaxed or a tolerance moved?** No tolerance exists in this milestone. The
architecture gate was made *stricter* by one rule, and the check passes on the unchanged tree.

---

## What this review could not do

An architecture review cannot show that the design is implementable at the performance or
robustness a real mesher needs. It can show that the design is internally consistent, that it
matches the repository as it actually is, and that its claims about the repository are true. Those
are the claims checked here.

The single largest unproven assumption is stated plainly in `KNOWN_LIMITATIONS`: that a backend
meeting ADR-033's admission rule exists, is obtainable, and behaves deterministically in
BetterCAD's build. One candidate meets the rule on paper. Nothing has been linked, built or
measured, and `P16-VOL-001` is gated on a decision that is not this milestone's to take.
