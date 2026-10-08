# P17-BC-001 — remesh, invalidation and determinism

## The semantic invariant

Restraints have no force or moment to conserve, so the invariant that survives
a remesh is not a quantity and is not a count. It is this, and it is brief
section 132's own wording:

```text
every current mesh node lying on the same canonical restrained CAD region
receives the same requested zero-displacement component constraints
```

That is what the tests assert, every time, through one helper:

```text
for every node of the mesh
    if the node is in the target set   it holds EXACTLY the requested mask
    otherwise                          it holds NOTHING
and  constraints().size() == target nodes x components
```

**Both directions, always.** A test that only asked "is every target node
constrained?" would pass a path that constrained the whole mesh; one that only
asked "is every constrained node on the target?" would pass a path that
constrained a single corner. The expected target set is assembled **in the
test** from P16's facet mapping, facet by facet, with its own `std::set` — not
from `meshing::boundaryNodesOf`, which is what production calls.

**No count is compared across two meshes.** A finer mesh has more nodes on the
same face and *should* have more constrained degrees of freedom; demanding
equal counts would be the wrong invariant, and brief section 133 says so.

## Remesh evidence

```text
RestraintId:            restraint:1
Canonical target:       FaceName{ RM-MESH-01 solid, FaceRole::EndCap }
Components:             fixed (ux+uy+uz)

M1 source:              MeshStamp of the first generation
  mapped facets:        2
  unique nodes:         4
  constrained DOFs:     12

M2 source:              MeshStamp of the second generation, same unchanged model
  mapped facets:        2
  unique nodes:         4
  constrained DOFs:     12

Canonical target unchanged:      YES -- the record compares byte-equal to a
                                 freshly constructed one
Old prepared constraints reused: NO -- and it is refused, not merely avoided:
                                   before.describes(M2) == false
                                   after.describes(M2)  == true
                                 so a consumer cannot mistake one for the other
Every current target node correctly constrained: YES, by the semantic check
```

On this model the two meshes happen to have the same node count, because the
geometry did not change. The claim does not rest on that: the **mesh identity**
moved, the old prepared set is unreadable against the new mesh, and the
semantic check was re-run against the new node handles.

### The same claim where the counts DO move

RM-MESH-02's lateral wall, one canonical `uz` restraint, three surface
deflections:

```text
deflection   facets   unique nodes   constrained DOFs
0.400 mm         72             72                 72
0.100 mm        100            100                100
0.025 mm        200            200                200
```

The levels are asserted to **differ** (strictly more facets and more nodes at
each step) before anything else is compared — the lesson of P17-LOAD-001's
convergence test, whose first two drafts both passed while measuring the same
mesh three times. The semantic check is re-run at each level, so the claim is
not "the count follows" alone but "every mapped node is constrained, at every
discretisation, and the count is therefore N".

RM-MESH-07, one canonical fixed support on the locally refined face, three
local targets:

```text
local target   body nodes   face facets   face nodes   constrained DOFs
12 mm                  14             2            4               12
 6 mm                  61             2            4               12
 3 mm                  64             2            4               12
```

The body grew and the restrained face did not, because a planar face is exactly
representable. That is recorded as the P16 property it is, with the premise
asserted (the body's node count strictly increases, so the control *was*
honoured) rather than the conclusion asserted away.

## Invalidation matrix

Measured on the block fixture with a material assigned, through P17-DATA-001's
own `resultCurrency` and P16's own `Mesher::currency`:

```text
Change                          Geometry current?  Mesh current?  Old result current?  Reason
-----------------------------------------------------------------------------------------------
component edit (ux -> ux+uy)    YES                YES            NO                   Analysis
target edit (end cap -> start)  YES                YES            NO                   Analysis
add a restraint                 YES                YES            NO                   Analysis
remove a restraint              YES                YES            NO                   Analysis
re-set the identical restraints YES                YES            YES                  --
display-only future metadata    YES                YES            YES                  (none exists)
```

Every row asserts all three columns: that the mesh is still
`MeshCurrency::Current`, that the source's `geometry` and `mesh` fields are
unchanged, and that the result is `Stale` for exactly
`std::vector{StaleReason::Analysis}`.

### And no new mechanism was added

The restraints are a field of `StructuralAnalysisDefinition`, so an edit moves
the owning object's revision, which moves `analysisRevision`, which
`StructuralResultSource` already carried and `staleReasons` already compared.
P17-DATA-001 answered the staleness question for every field this struct will
ever grow, and P17-LOAD-001 already demonstrated it for loads.

There is **no parallel stale counter**, and the evidence is structural rather
than a grep: `setDefinition` short-circuits when the new definition compares
equal, so re-setting the same restraints moves nothing, and the mutation that
removes the restraints from the definition's `operator==` makes a restraint
edit invisible — which the currency test kills.

### A restraint edit stales the RESULT and not the MESH

```text
restraint edit  ->  structural result stale
restraint edit  ->  P16 mesh NOT stale
restraint edit  ->  geometry NOT regenerated
```

A restraint names a `FaceName`, not a mesh control, so it is not in the mesh's
dependency chain at all. This is the same asymmetry a material edit has, which
P17-DATA-001 recorded: a mesh is a function of geometry and meshing intent, and
a boundary condition is in neither. An architecture that invalidated the mesh on
a restraint edit would remesh a hundred thousand elements to answer a question
about three bits.

## Prepared state is disposable

```text
PreparedRestraints   bound to   MeshStamp + node count (describes())
                     bound to   the MeshDofMap it was numbered against
                     persisted  NEVER
```

There is no serialization of a prepared set, no cache and no shared state. A
numbering is cheap to rebuild and a cached one is a stale one waiting for a
remesh — P17-DOF-001's words, inherited here.

The binding is checked by `MeshStamp` **and node count**, not by stamp alone,
for the reason `MeshDofMap::describes` records: `MeshBuilder` sets its stamp in
its constructor and `build()` is a snapshot, so two snapshots of one builder
share a stamp and may hold different numbers of nodes. A stamp identifies the
builder, not the snapshot. Production cannot reach that ambiguity today, which
is why the mutation reducing `describes` to a stamp comparison survives — see
[MUTATION_PROTECTION.md](MUTATION_PROTECTION.md).

## Deterministic evidence

```text
fixture                     runs  node-set fingerprint   constraint-set fingerprint  identical?
------------------------------------------------------------------------------------------------
block, 2 restraints, given
  in order (1, 2)              1   ordered NodeId list    ordered DofIndex list       --
block, same 2 restraints
  in order (2, 1)              1   same list              same list                   YES
block, order (1, 2) again      1   same list              same list                   YES
every reference model          1   --                     --                          see below
ctest --repeat until-pass:3    3   --                     --                          YES
```

The fingerprints are the **ordered index lists themselves**, compared element
for element with no tolerance — `std::ranges::equal` over
`constraints().constrained()` and over `nodes()`, plus `ConstraintSet`'s own
`operator==`. There is nothing to tolerance: these are integers.

Order independence is a property of the representation and not of a sorting
step a caller might skip:

```text
facets        P16 returns them ascending and without repeats
nodes         boundaryNodesOf sorts and uniques
node union    sorted and uniqued again across restraints
prescribed    sorted and uniqued before handing off
ConstraintSet sorts its indices, and its own header says two callers listing
              the same restraints in different orders build sets that compare
              equal
```

**Nothing in the module is built from an unordered container.** There is no
`std::unordered_map`, no `std::unordered_set` and no iteration over a hash in
`StructuralConstraints.cpp`, `StructuralTarget.cpp` or either header. The
per-restraint `resolutions()` list keeps the user's order, which is the one
thing that *should* depend on it, and the test asserts both halves: the derived
set is identical and the records are not.

Cross-preset equivalence is established by the three-preset run: the outputs
compared are mapping state, facet count, ordered node list, ordered constrained
index list, constraint count and diagnostics — all integer or string identity,
so no floating tolerance is involved anywhere in this milestone.
