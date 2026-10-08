# P17-BC-001 — adversarial review

```text
RESULT: PASS
```

Twenty-five attacks, taken from brief section 140 and asked against the final
diff rather than the intent. **Four** found real defects in my own work; all
four were fixed and each has a test or a probe that would have caught it. The
rest are answered with the mechanism that closes them, not with a reassurance.

## The four defects this review found

### D1 — the remesh binding assertion was written backwards

```cpp
CHECK(before.describes(second.mesh().mesh()) == after->describes(second.mesh().mesh()));
```

That compares two booleans for equality. It is *true* when both are false —
which is to say it would have passed if a prepared set described **no** mesh at
all, and it failed on the correct implementation (false == true) only by luck.
The claim it was supposed to make is a conjunction:

```cpp
CHECK_FALSE(before.describes(second.mesh().mesh()));
CHECK(after->describes(second.mesh().mesh()));
```

A test defect, not a production one, and the kind that makes an authority claim
look checked when it is not. Fixed, and the two halves are now asserted
separately with the stamps compared as well.

### D2 — the "not only CAD corners" test was vacuous, and could not be made otherwise

The first draft asserted `expectedNodes(side 0).size() > 4` on the block
fixture and got **4**. A finer global target did not help, and neither did a
local control: a planar box face is exactly representable, so nothing in the
sizing retriangulates its two facets. On a box face, "constrain only the four
CAD corners" and "constrain all four mapped nodes" are the **same answer**, so
no test on that geometry can distinguish a corners-only implementation.

Root cause: I chose the fixture before checking what the instrument could see.
The same mistake as P17-ELEM-001's two mesh-density assertions and
P17-LOAD-001's two vacuous refinement drafts, which is why it is recorded here
in those terms rather than quietly corrected.

Fix, in two parts:

```text
the corners claim moved to RM-MESH-04's curved inner wall, which carries 72
    mapped nodes against a face bounded by two circular edges -- and the
    premise (> 8 nodes) is asserted before the property

the block test became what the block CAN show: the off-target half, with its
    own premise asserted (the target face carries fewer than half the mesh's
    nodes), and a comment on the fixture recording why the other half is
    impossible here
```

### D3 — the refinement comparison measured the same mesh three times

RM-MESH-07's local target was varied 12 -> 6 -> 3 mm and the restrained face
reported 2 facets and 4 nodes at every level. The test passed. It was measuring
nothing, exactly as P17-LOAD-001's second draft did.

Fix: the genuine refinement claim moved to RM-MESH-02's cylindrical wall, where
the same canonical target gives **72, 100 and 200** nodes and the constrained
count follows at every level — with the levels asserted to differ *before* any
comparison. The RM-MESH-07 case was kept and re-aimed: it now asserts that the
body's node count strictly increases (so the control was honoured) while the
planar face's node set does not move, which is the P16 property it actually
demonstrates.

### D4 -- the currentness test proved only that `modifyObject` bumps a revision

Found by a **mutation probe**, not by reading. M14 makes
`StructuralAnalysisDefinition::operator==` ignore the restraints, so
`setDefinition` short-circuits and a restraint edit moves nothing. It killed
only one of the two tests that should have failed, and that gap was the
evidence.

The fixture helper was:

```cpp
return study.setDefinition(std::move(definition)).has_value();
```

`Result<bool>` from a mutation means *did anything change*, and
`Document::modifyObject` bumps the object and document revisions only when it
is `true`. But `.has_value()` is true whenever the CALL succeeded -- including
when the definition compared equal and nothing moved. So the helper reported a
change on every call, the revision moved on every call, and
`StructuralBC_ARestraintEditStalesTheResultAndNotTheMesh` would have passed
**even if the restraints were not in the definition at all**. It was testing
`modifyObject`, not the milestone.

Fix, in three parts:

```text
the helper returns the change flag, and the mutation returns setDefinition own
    Result<bool> unchanged

a CONTROL section asserts that re-setting the IDENTICAL restraints returns
    false, leaves the source byte-equal and leaves the result Current -- the
    invalidation matrix had that row and nothing tested it

each of the four edits asserts that it DID report a change, so no section can
    go quiet
```

M14 was re-run against the corrected test and now kills both tests. Recorded in
[MUTATION_PROTECTION.md](MUTATION_PROTECTION.md).

D2 and D3 are the fourth and fifth instances of the vacuous-instrument family
across P17 -- P17-ELEM-001 asserted a mesh density twice and P17-LOAD-001 wrote
a vacuous refinement test twice -- so each is recorded with the instrument
blind spot stated on the fixture itself rather than quietly corrected. D4 is
the same family wearing different clothes: an assertion whose subject was the
harness rather than the code.

## The twenty-five attacks

### Can NodeIds become canonical restraint authority?

No. `StructuralRestraint` has three members and a mirror-struct `sizeof`
assertion fixes them; the compile-failure case
`compile_fail.structbc.restraint-from-node-id` refuses a `NodeId` where the CAD
target belongs. The mutation that adds a node array to the record is **killed
at compile time** by the size assertion.

### Can DofIndices become persisted restraint identity?

No. `compile_fail.structbc.restraint-from-dof-index`, plus the same size
assertion; the mutation adding a DOF array is killed at compile time.
`RestraintId`'s own declaration states the rule, and it predates this
milestone.

### Can current boundary facet IDs be saved in the restraint?

No. There is no `ElementId` anywhere in `StructuralRestraint.hpp`, the size
assertion would fail if one were added, and the facets are a local variable in
`run()` that goes out of scope — `PreparedRestraints` keeps a facet **count**
for diagnostics and no handles at all.

### Can an unresolved target silently bind to the nearest face?

No. `!fullyResolved()` is refused with `NotFound`, the message says "Nothing
nearby is rebound", and the test aims at a feature not in the body on a block
with six nameable faces — a fallback would have found one. No `nearest`,
`distance`, `centroid` or `tolerance` appears in the implementation. The
mutation that proceeds anyway is killed by five tests across P17-BC and
P17-LOAD.

### Can drilled-hole wall targeting be faked geometrically?

No, and the refusal is the pass. [UNSUPPORTED_TARGETS.md](UNSUPPORTED_TARGETS.md)
carries it, with the *named* bore of the same geometric kind succeeding in the
same test case so the gap is attributed to P16's naming chain rather than
hidden.

### Can a stale geometry or mesh mapping constrain an old node set?

No. `prepareStructuralRestraints` takes a `StructuralModel`, whose possession
is the evidence that geometry and mesh are current and that the mapping came
from the same lookup (ADR-036). `meshing::boundaryNodesOf` checks the map
against the mesh again on its own account. There is no code path that reaches a
mapping without a model.

### Can the same numeric NodeIds on a second mesh trick reuse of the first mesh's constraints?

No. `PreparedRestraints::describes` checks the `MeshStamp` **and** the node
count, and `prepareStructuralRestraints` refuses a `MeshDofMap` that does not
describe the model's mesh — before anything else, so even an **empty** restraint
set is refused against the wrong numbering. The mutation disabling that check is
killed.

The weaker mutation — reducing `describes` to a stamp comparison —
**survives**, and that is recorded rather than hidden: production builds one
mesh per generation from a local builder, so the two-snapshots-of-one-builder
case cannot arise here. The count check is load-bearing in `MeshDofMap`, where
P17-DOF-001's own probes M9 and M10 killed its removal; this one mirrors it for
symmetry.

### Can only CAD corner vertices be constrained while refined face nodes remain free?

No — see D2 for why the test had to move to find out. RM-MESH-04's inner wall
constrains all 72 of its mapped nodes, and RM-MESH-02's wall constrains 72, 100
and 200 as the face refines. The mutation that truncates the node list to four
is killed by three tests.

### Can shared facet nodes create duplicate constraints?

No. `meshing::boundaryNodesOf` sorts and uniques, and the cross-restraint union
sorts and uniques again. The mutation removing either deduplication is probed.

### Can overlapping faces cause false duplicate failures?

No, and this is the one place the milestone had to make a decision rather than
inherit one. P17-DOF's `buildConstraintSet` refuses a repeated `DofIndex`
deliberately, because it cannot know whether the repetition is harmless. Here
it demonstrably is — every prescribed value is zero — so the duplicates are
removed before the handoff. Two adjacent faces restrained in the same component
give 6 constrained DOFs from 4 + 4 nodes sharing 2, and the test asserts the
intersection is non-empty before relying on it.

### Can a ux restraint accidentally constrain uy or uz too?

No. The component loop skips components the mask does not hold, and
`checkExactlyConstrained` reads back **all three** components of **every** node
of the mesh and compares against the requested mask exactly. The mutation that
constrains all three regardless is killed; so is the one that swaps uy and uz.

### Can a global ux become a face-normal restraint after the body rotates?

No, and there is no face-local basis in the schema to switch to. RM-MESH-06's
base and placed models give the same four constrained nodes for the same
canonical `ux` target, and the test asserts the nodes really moved (largest
coordinate difference > 1e-4 m) so the premise is not assumed. The mutation
"make the restraint face-normal based" is **not expressible**: there is no
normal in the code to read and no alternative basis to select, which is itself
the evidence.

### Can a fixed support include nonexistent rotational DOFs?

No. `RestraintComponents::fixed()` is built by iterating `kDofComponents`,
which is P17-DOF's array of exactly three translational components, and
`isFixed()` compares against `kDofsPerNode` rather than a literal 3. A Tet4
node has three degrees of freedom; there is no `Rx` in `DofComponent` to add.
`static_assert(RestraintComponents::fixed().count() == 3)` sits beside the
definition.

### Can an empty component mask become a successful no-op?

No. Refused twice — by `validate(StructuralRestraint)` with no mesh at all, and
by `prepareStructuralRestraints` before any mesh work. The mutation accepting
it is probed. A restraint that holds nothing at zero is a record whose intent
cannot be acted on, which is not the same thing as a weaker restraint.

### Can the same RestraintId silently overwrite a different restraint?

No. Two records with one id is refused first, before any resolution, with both
the id and the reason named. The mutation accepting it is probed.

### Can three separate X/Y/Z restraints differ physically from a fixed support?

No, and it is asserted through the whole pipeline rather than in the mask
algebra alone: the two `ConstraintSet`s compare **equal**. The per-restraint
`resolutions()` still shows three records, which is what traceability means.

### Can restraint insertion order change the constrained set?

No. Reversed order gives an identical ordered index list and an identical
ordered node list, compared element for element. The property is structural:
facets arrive ascending from P16, nodes arrive sorted from `boundaryNodesOf`,
the union is sorted, and `ConstraintSet` sorts again. No unordered container
appears in the module.

### Can P17-BC renumber free equations itself and diverge from P17-DOF?

No. `buildFreeEquationMap` is never called from production code in this
milestone — only from a test, to prove interoperability. Searched: there is no
free-equation numbering, no equation counter and no index arithmetic in
`StructuralConstraints.cpp` at all. Every index comes from
`MeshDofMap::indexOf`, and the test decodes each one back through `dofAt`.

### Can a restraint edit mark the P16 mesh stale?

No, and there is no code here that could: a restraint names a `FaceName`, not a
mesh control, so it is not in the mesh's dependency chain. Asserted directly —
`Mesher::currency` is `Current` after every one of the four restraint edits
tested.

### Can a restraint edit fail to stale the old FEA result?

No. All four edits — component mask, target face, add, remove — give
`ResultCurrency::Stale` with exactly `StaleReason::Analysis`, through
P17-DATA-001's existing comparison and with no new mechanism. A fifth section
is the control: re-setting the IDENTICAL restraints reports no change, leaves
the source byte-equal and leaves the result Current. The mutation that removes
the restraints from the definition's `operator==` makes the edit a no-op at
`setDefinition` and is killed by both currentness tests — after D4, which is
where this attack earned its keep.

### Can a no-restraint model be incorrectly rejected here rather than by solver validation?

No. An empty restraint set **succeeds**, with an empty `ConstraintSet` and a
free numbering covering the whole model. One ux restraint on one face leaves
five rigid-body modes and also succeeds. Detecting an insufficiently
constrained model is P17-SOLVE-001's, which is where the eigenstructure is
visible, and the separation is tested in both directions.

### Can non-zero displacement accidentally appear supported?

No. There is no value field, `RestraintComponents` is one byte, and
`compile_fail.structbc.components-from-displacement` refuses a `Length` where
the mask belongs. A `double value = 0` that only ever supported zero would have
been the false capability; it was not written.

### Can P17-BC depend on GUI, CLI or persistence?

No. `src/structural/` includes no Qt header, no CLI header and no io header;
the architecture layering test enforces containment and would fail the build.
Nothing in this milestone serializes a restraint, adds a command, or touches a
viewer — the restraints are a field of a document object, which is how they will
be persisted when `io` is extended, and that is a later milestone's work.

### Can changing shared mapping code invalidate P17-LOAD qualification without a rerun?

It would have, which is why it did not. `src/structural/StructuralLoad.cpp` is a
changed production file and
[REQUALIFICATION.md](REQUALIFICATION.md) records the reruns: 23/23 targeted,
185/185 over the combined selection, every mutation probe running both suites
together, and the three-preset unfiltered qualification. A dated forward note
was added to P17-LOAD-001's own evidence; its `FREEZE.md` fingerprint was left
untouched, because that fingerprint is the tree it was qualified on and editing
it would falsify a record.

### Can zero targeted tests execute while qualification claims PASS?

No. The selection was counted before it was trusted:

```text
ctest -N -R "StructuralBC_|structbc"          Total Tests: 38
    24 unit + 8 reference + 6 compile-fail
ctest -N -R "StructuralBC_|StructuralLoad_"   55, the mutation filter
```

and the final regression is **unfiltered** in all three presets. P17-DOF-001
found a real instance of this failure — its inherited filter covered 17 of 31
tests — so the count is derived from both ends here rather than assumed.

## What was NOT found

No hidden global state; no behaviour that exists only for tests; no tolerance
anywhere in the milestone (every comparison is integer or string identity); no
Debug/Release divergence available, since nothing is floating-point; no partial
state on failure, because `PreparedRestraints` has no default constructor; no
architectural boundary crossed; no widened scope — no assembly, no solver, no
persistence, no command, no GUI.

Two of the brief's mutations are **not expressible** against this tree, and
both are recorded with the reason rather than reported as killed:
"make component restraint face-normal based" (no normal is read and no
alternative basis exists) and "accept stale mapping" (the gate is
`requireStructuralModel`'s, and there is no second one here to disable).
