# P17-DATA-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the identity and result model before any
          solver is built on it
QUESTIONS: 30 from the brief, plus 5 of the reviewer's own
FINDINGS: 4 -- 1 real coverage blind spot, 1 wrong assertion of mine, 1 method
          defect, 1 recorded limitation
PRODUCTION DEFECTS: 0
GATE-BLOCKING: 0
```

## Findings

### F1 — the mutation tests could not see a missing field read (BLIND SPOT, CLOSED)

The currentness truth table mutates a `StructuralResultSource` **struct** and
checks that `staleReasons` notices. Ten sections, one per dependency, each
mutated alone. It looks like thorough mutation coverage and it has a hole:

```text
it proves the COMPARISON is field-complete
it proves nothing about whether currentResultSource POPULATES each field
```

If `currentResultSource` had forgotten to read `analysisRevision` and left it
zero, every one of those ten sections would still have passed — the struct-level
test never touches the function that builds the struct.

The material half was covered by accident:
`AMaterialEditStalesTheResultAndNotTheMesh` edits a real Young's modulus in a
real document. The analysis half was not covered at all.

**Closed** by `StructuralData_TheSourceStampIsPopulatedFromTheDocumentAndNotDefaulted`,
which adds a second mesh control on the same body, edits the analysis to point
at it, and asserts that `analysisRevision` moved while `body`, `material` and
`materialRevision` did not. Measured: **revision 1 → 2, three stale reasons**
(`Control`, `Mesh`, `Analysis`).

Worth recording as the general lesson: a mutation test that mutates the data
structure rather than the source of truth tests only half the path, and the
half it skips is the half that populates.

### F2 — I asserted an immutability property that was neither true nor wanted (FIXED)

The first draft asserted `!std::is_copy_assignable_v<StructuralResult>` on the
grounds that a result should be immutable. The build rejected it, and the build
was right: a result **is** assignable, and should be, because the solver service
that holds one per analysis must replace it when a new solve succeeds. That is
replacement by the owner, not mutation through a reference.

The property that actually matters — and that the brief asks for, "do not allow
GUI code to modify stress, displacement or reaction in place" — is that every
array is handed out as a span of **const** elements. That is what is asserted
now:

```cpp
static_assert(std::is_const_v<std::remove_reference_t<
    decltype(std::declval<const StructuralResult&>().displacements()[0])>>);
```

A reminder that "immutable" is not one property, and asserting the wrong one
would have constrained a later milestone for no safety gain.

### F3 — I reached for an instrument this repository does not use (CORRECTED)

Three tests in the first draft read their own source files and searched the
text: "does `core/Id.hpp` contain a `DofIdTag`", "does the header say
`gammaXy`", "does the result class embed a mesh". No test in BetterCAD reads a
source file, there is no helper for it, and it would have needed the source
directory at runtime.

Worse, it is a weaker instrument than the ones available. Each claim has a
compile-time form that breaks the build rather than merely stopping a match:

```text
"no DOF tag in Id.hpp"        -> static_assert(!is_convertible_v<DofIndex, ObjectId>)
                                 and eleven compile-fail cases
"the field is named gammaXy"  -> the test NAMES the field, so a rename stops
                                 the suite compiling
"no mesh embedded"            -> static_assert(sizeof(StructuralResult) <
                                 sizeof(VolumeMesh))
```

All three replaced. A search of the text can be talked past; a build failure
cannot.

### F4 — an unattributable stale result when inputs are unavailable (LIMITATION)

When the geometry changes, `requireStructuralModel` refuses, so
`currentResultSource` fails and `analysisState` reports `InputsUnavailable`.
That is correct — the result is certainly not current — but it loses something a
user would want: **the per-field stale reasons cannot be computed**, because
there is no current source to compare against.

So a user whose geometry moved is told "inputs unavailable" and not "your result
is stale because the geometry changed". The state is right; the attribution is
missing.

This is inherent rather than a defect: you cannot diff against a state that does
not exist. `structuralInputProblem` names which of its seven reasons applies,
which is the attribution available at that point. Recorded rather than papered
over, and noted for `P17-VIZ-001`, which will want to present it.

## The brief's 30 questions

**Can `AnalysisId` be confused with `LoadId`?** No — compile-fail case
`analysis-id-as-load-id`. **`LoadId` with `RestraintId`?** No —
`load-id-as-restraint-id` and `restraint-id-as-load-id`, both directions,
because that is the copy-paste mistake.

**Can a DOF index enter `core/Id.hpp` as if persisted?** Not as a type: it is
not an `Id<Tag>` at all, and `dof-index-as-object-id` proves it cannot be used
where a document identity is wanted. That is the stronger claim than its absence
from a header, because a tag could be added tomorrow and the conversion still
would not exist.

**Can `NodeId` become permanent load or restraint identity?** No —
`node-id-as-load-id` and `node-id-as-restraint-id`. This is ADR-032 enforced one
layer up.

**Can `ElementId` be treated as surviving a remesh?** Not through a result:
`describes()` compares `MeshStamp`, and a result of the old mesh refuses the new
one.

**Can a result from M1 appear current on M2 because counts match?** No, and it
is measured rather than argued: remeshing the same body with the same settings
gives **9 nodes either way**, different stamps, and the result is refused.

**...because numeric NodeIds repeat?** Same answer. Handles are never
reinterpreted; the stamp is checked before any array is read.

**Can changing mesh controls leave a result current if the mesh has not
regenerated?** No. An intent edit makes `Mesher::currency` report `StaleIntent`,
`requireStructuralModel` refuses and the state is `InputsUnavailable` — see F4
for what that costs in attribution.

**Can changing `E` or `nu` leave a result current?** No, measured end to end on
a real document edit, and the mesh correctly stays `Current` through it.

**Can changing a load or a restraint leave a result current?** They do not exist
yet. What is proved is the mechanism that will carry them: they live in
`StructuralAnalysisDefinition`, `setDefinition` moves the object's revision on an
effective change and not on a no-op, and `currentResultSource` reads that
revision — the last of which is exactly what F1 had failed to test.

**Can changing a solver tolerance leave a result current?** Same mechanism, same
answer, same caveat: solver settings are a field of the same definition.

**Can viewer visibility stale a result unnecessarily?** No — a quality
**threshold** edit is the sharpest case, since it changes how an element is
classified and can never change the element. P16 keeps the mesh `Current` and
the result stays current with it.

**Can the GUI mutate result arrays?** No — every span yields const elements,
asserted at compile time.

**Can result provenance be edited to make a stale result look current?** No
setter, no non-const `source()`. This is the failure mode the whole design is
built against, and the absence is asserted rather than assumed.

**Can a failed solve publish a partially populated result?** `create` refuses
any array of the wrong cardinality, so a half-filled result is not
constructible. The publication policy — keep the old result as stale, record the
failure — is `P17-SOLVE-001`'s, and `AnalysisState::SolveFailed` already takes
precedence over `SolvedStale` so that it cannot be hidden.

**Can one result mix displacements from M1 with stresses from M2?** No, and
structurally rather than by check: `create` takes **one** mesh and validates
every array against it. There is no signature through which two meshes could
arrive.

**Can strain component XY mean tensor shear in one module and engineering shear
in another?** The field is named `gammaXy`. A module reading it as a tensor
component would be reading a name that says otherwise, and the rename that would
reintroduce the ambiguity stops the test suite compiling.

**Can stress component ordering differ from strain ordering?** One
`TensorComponent` enum serves both, with its values asserted and
`component()` checked against every named field.

**Can an `unordered_map` create nondeterministic result output?** None exists:
`grep unordered_map|unordered_set` over `include/bettercad/structural/` and
`src/structural/` returns 0. `staleReasons` returns the enum's order, never a
container's.

**Can raw `NodeId` be assumed dense and used as a vector index?** No occurrence
of `[...value()]` indexing in the module. IDs start at 1 and a builder may
choose them, so the access model is the mesh's ascending enumeration and a
binary search.

**Can a result duplicate the entire `VolumeMesh`?**
`static_assert(sizeof(StructuralResult) < sizeof(meshing::VolumeMesh))`. A
`sizeof` rather than a member-name search, because a size cannot be argued with.

**Can P17-DATA introduce a second unit system?** No raw `double` or `float` data
member exists except `Strain6`'s, which is a dimensionless ratio.
`Translation3D` was reused rather than a `Displacement3` invented, and `Force3D`
was added to `core` beside its sibling rather than to `structural`.

**Can P17-DATA depend on persistence or the GUI?** No `bettercad/io/`,
`bettercad/renderer/`, Qt, Eigen or Netgen include in the module; the layering
check passes over 453 files with 0 violations; `structural -> io` has had a
failing fixture since P17-ARCH-001.

**Can `LoadCaseId` be introduced without any load-case semantics?** It was not
introduced. `grep -rn LoadCaseId include/ src/ tests/` returns 0, and the
decision with its reasoning is in `IDENTITY_MODEL.md`.

**Can stale reasons omit a required source category?** The comparison is
`= default`-generated field-by-field equality plus one explicit reason per
field, and ten sections mutate each alone. F1 is the half of this question that
was genuinely weak, and it is closed.

**Can zero tests execute under a filter while qualification says PASS?**
Checked with `-N` before every run:

```text
StructuralData              21
compile_fail.structids      12
architecture.               14
StructuralInput             10
```

## Five of the reviewer's own

**Is `AnalysisId` honestly a document identity, given the object barely
exists?** Yes, and that was worth checking rather than assuming, because the
repository already contains the failure mode: `FaceId`, `EdgeId` and `VertexId`
are declared in `core/Id.hpp` and used in **0** places. Following that precedent
blindly would have produced three more. The difference is that
`StructuralAnalysis : DocumentObject` is built this milestone and is real — it
has a definition, a revision, dependencies, a clone, content equality and
tests — so the widening claims something true. `LoadId` and `RestraintId` are
declared ahead of their payloads, and that is deliberate for a reason the
milestone states: their purpose is to constrain the milestones that define those
payloads, and tests pin their domain now.

**Does one `analysisRevision` really cover loads and restraints, or is that
convenient?** It is provable only as far as the mechanism, and the evidence says
so plainly. Loads and restraints will be fields of
`StructuralAnalysisDefinition`; `setDefinition` moves the revision on any
effective change to it. What is proved today is that the revision moves on a
real edit and not on a no-op, and that `currentResultSource` reads it. What is
not proved is a load edit, because there are no loads. Three separate counters
would have been no more provable and would have added two more chances to forget
one.

**Is `StructuralAnalysisDefinition` with one field a placeholder?** The test
asserts `sizeof(StructuralAnalysisDefinition) == sizeof(MeshControlId)`, which is
a strange thing to assert unless the type is nearly empty. It is minimal, not
empty: an analysis that names the mesh it analyses is a complete, meaningful
statement, and the named-but-undeclared fields are comments rather than empty
vectors precisely so that a reader cannot mistake an unimplemented field for an
intentionally empty one. The alternative the brief warns against — speculative
`std::vector<ContactPair>` — does not appear.

**Could the state machine have an unreachable value, as P17-ARCH-001's enum
did?** This was checked deliberately, because it is the defect the previous
milestone found in itself. All six `AnalysisState` values are reached by a
section of one test, and `SolveFailed` is reachable only because
`analysisState` takes the last failure as a parameter rather than from a service
that does not exist. Two of the brief's suggested states, `Defined` and
`InputStale`, were **merged** rather than declared, because nothing today can
distinguish them.

**Does the result model survive a large mesh?** Not measured at scale, and not
claimed to be. What is asserted is the shape of the growth — `O(nodes +
elements)`, no mesh copy, no per-entity map — which is what makes a scale
measurement meaningful later. A timing number from a 9-node mesh would be noise
presented as evidence.

## Result

```text
QUESTIONS:                  30 + 5 = 35
FINDINGS:                   4
PRODUCTION DEFECTS:         0
COVERAGE BLIND SPOTS:       1  (F1, closed by a new end-to-end test)
WRONG ASSERTIONS OF MINE:   1  (F2, corrected to the property that matters)
METHOD DEFECTS:             1  (F3, three tests moved to compile-time guards)
RECORDED LIMITATIONS:       1  (F4, attribution is unavailable when inputs are)
GATE-BLOCKING:              0
VERDICT:                    PASS
```
