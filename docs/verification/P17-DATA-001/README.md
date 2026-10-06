# P17-DATA-001 — Analysis / Result Data Model

```text
STATUS:   PASS
MILESTONE: P17-DATA-001, the second milestone of P17 — Structural FEA
SCOPE:    identity, representation and currentness. No element, no DOF
          numbering, no assembly, no solve.
```

## Baseline

```text
HEAD at start      a4290cf757000314b5bf321cab8121822be5b608
origin/main        a4290cf757000314b5bf321cab8121822be5b608
working tree       clean, 0 porcelain lines
P17-ARCH-001       PASS, 20/20, and the eight paths still fingerprinted
                   536e14b5 -- its qualified tree -- so the predecessor was
                   demonstrably qualified on THIS tree and not merely recorded
P16 / P15          QUALIFIED
```

## The three identity domains

The milestone's whole subject, made true in the type system rather than in a
convention:

```text
DOCUMENT      AnalysisId, LoadId, RestraintId. Canonical, persisted, survive a
              remesh, a reload and a process. In core/Id.hpp with every other
              document identity.
MESH-LOCAL    NodeId, ElementId. P16's, unchanged. Valid only against the
              MeshStamp that issued them.
SOLVER-LOCAL  DofIndex. Valid only within one numbering of one mesh. Defined in
              structural/ and deliberately NOT in core/Id.hpp.
```

Only `AnalysisId` widens to `ObjectId`, because only the analysis is a document
object. `LoadId` and `RestraintId` are members of its definition — the shape
`BoundarySetId` already established, whose own documentation says that its
identity is the intent while "the facets, nodes and elements are derived afresh
for whatever mesh is current".

**`LoadCaseId` is NOT REQUIRED**, decided rather than skipped: the authorized
scope is one analysis → one set of loads and restraints → one solve, so one
analysis *is* one load case. `grep -rn LoadCaseId` returns 0. Adding it would
have left a strongly-named identity that nothing allocates and nothing
resolves, which a later developer would reasonably read as working
infrastructure.

[IDENTITY_MODEL.md](IDENTITY_MODEL.md).

## What a result is, and in what units

| Field | Type | Unit | Keyed |
| --- | --- | --- | --- |
| displacement | `Translation3D` | m | dense, parallel to `mesh.nodes()` |
| reaction | `Force3D` + `NodeId` | N | sparse, ascending |
| strain | `Strain6` | dimensionless | dense, parallel to tetrahedra |
| stress | `Stress6` of `Stress` | Pa | dense, likewise |

**`Translation3D` already existed** — "how far every point moves along X, Y and
Z" — so a displacement reuses it rather than inventing a second length-triple.
**`Force3D` is new**, placed in `core/math/Vector.hpp` beside its sibling
because a force is an engineering quantity, not a structural-analysis concept.

Component order is frozen once for every P17 milestone — `XX YY ZZ XY YZ ZX` —
and is deliberately **not** `InertiaTensor`'s order, which is said out loud
because the two are six-component symmetric tensors in one codebase.

**The shear convention is in the field names**: `gammaXy`, not `xy`. A reader
cannot mistake engineering shear for the tensor component, and the rename that
would reintroduce the ambiguity stops the test suite compiling.

**Node IDs are not assumed dense.** P16 allocates from 1 and a builder may
choose them, so indexing by `id.value()` would be correct for every mesh the
current backend happens to produce and wrong for the first one that is not. The
access model is the mesh's documented ascending enumeration and a binary
search.

[RESULT_MODEL.md](RESULT_MODEL.md).

## Provenance and currentness

```cpp
struct StructuralResultSource {
    ObjectId body; MeshControlId control;
    meshing::GeometryRevision geometry; meshing::MeshStamp mesh;
    MaterialId material;  std::uint64_t materialRevision;
    AnalysisId analysis;  std::uint64_t analysisRevision;
};
```

Separate fields rather than one mixed hash, so a stale result can say *which*
dependency moved. One `analysisRevision` covers loads, restraints and solver
settings, because all three are fields of the same definition on the same
document object — three counters would be three chances to forget one.

`currentResultSource` **calls `requireStructuralModel`**, so every gate
ADR-036 established is inherited rather than re-asked, including the
configuration refusal.

Three rows worth stating:

```text
a material edit     -> result STALE, mesh CURRENT. Measured on a real
                       210 -> 190 GPa edit
a remesh            -> STALE. 9 nodes either way, different stamp, refused.
                       Counts are not identity
a threshold edit    -> CURRENT. It changes how an element is classified and can
                       never change the element
```

**Undo semantics were audited, not invented.** P16's own tests measure two
different rules — meshing intent is compared by value, so an undone intent edit
restores currency; geometry is compared by a mix of monotonic revision
counters, so restoring a dimension leaves the mesh stale. P17 inherits both
rather than defining a third, and its own analysis intent uses the conservative
one.

[CURRENTNESS_MODEL.md](CURRENTNESS_MODEL.md) — which also holds the state
machine table, rather than a separate document that would restate it.

## Tests

```text
tests/structural/StructuralDataTests.cpp        21 ctest entries
  identity domains, widening, DOF 64-bit        compile-time where possible
  component order and shear convention
  units on every field
  the analysis object: intent only, no-op edits do not move the revision
  the result container: six refusals, each its own section
  binding to the mesh by stamp, not by count
  the currentness truth table: ten rows, each mutated ALONE
  the source stamp is populated from the DOCUMENT    <- added by the review
  the state machine: all six states reached
  determinism over 16 evaluations

tests/compile_fail/StructuralIdMisuse.cpp       12 cases + 1 control
  the control compiles, and AnalysisId -> ObjectId with it
  11 conversions refused, including dof-index-as-object-id and
  node-id-as-load-id
```

## Zero-match protection

Checked with `-N` before every run:

```text
StructuralData            21     compile_fail.structids   12
StructuralInput           10     architecture.            14
```

## Adversarial review

```text
QUESTIONS                 35  (30 from the brief, 5 of my own)
FINDINGS                   4
PRODUCTION DEFECTS         0
COVERAGE BLIND SPOT        1  the truth table mutated the STRUCT, so a missing
                              field read in currentResultSource would have gone
                              undetected. Closed by a new end-to-end test
WRONG ASSERTION OF MINE    1  non-assignability is not the immutability that
                              matters; const spans are
METHOD DEFECT              1  three tests read their own source text, which is
                              not this repository's instrument and is weaker
                              than the compile-time form
RECORDED LIMITATION        1  when inputs are unavailable, per-field stale
                              attribution cannot be computed
GATE-BLOCKING              0
```

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Known limitations

```text
A load edit and a restraint edit cannot be tested, because loads and restraints
  do not exist. What is proved is the mechanism they will use: they are fields
  of StructuralAnalysisDefinition, setDefinition moves the revision on an
  effective change and not on a no-op, and currentResultSource reads it.

When requireStructuralModel refuses, the state is InputsUnavailable and the
  per-field stale reasons cannot be computed -- there is no current source to
  diff against. Inherent rather than a defect; structuralInputProblem names
  which of its seven reasons applies.

Two of the brief's suggested states, Defined and InputStale, are MERGED into
  InputsUnavailable, because with no loads or restraints yet nothing
  distinguishes "not finished being defined" from "was fine and is now stale".
  Splitting them is one enum value when P17-BC-001 makes the distinction real.

The result model is not measured at scale. What is asserted is the shape of the
  growth -- O(nodes + elements), no mesh copy, no per-entity map -- which is
  what makes a scale measurement meaningful later. A timing number from a
  9-node mesh would be noise presented as evidence.

LoadId and RestraintId are declared ahead of their payloads. Deliberate: their
  purpose is to constrain the milestones that define those payloads, and the
  compile-fail cases pin their domain now. The repository's own counter-example
  is FaceId/EdgeId/VertexId, declared and used in 0 places; the difference is
  that these are tested and that one of the three has a real object behind it.

This MinGW toolchain has no ASan/UBSan. Inherited and recorded.
```

## Regression

Three presets, each configured, **cleaned**, rebuilt and run unfiltered, then
two repeat stages. One uninterrupted detached run, 03:14:26 to 06:11:17,
2 h 56 min, 17 stages, `qualify.cmd exit 0`.

```text
PRESET            CONFIGURE  CLEAN  BUILD  NO-OP REBUILD  CTEST
debug-ext              0       0      0         0           0   3434/3434
release-ext            0       0      0         0           0   3434/3434
debug-shared-ext       0       0      0         0           0   3434/3434

REPEAT (5x each of 715 selected tests, back to back)
release-ext            0                            715/715   696.22 s
debug-ext              0                            715/715   747.93 s

warnings, all three clean builds   0   (-Werror and 22 warning flags,
                                        603 objects each)
no-op rebuilds                     0 compiles, 0 links in every preset
shared build                       10 DLLs
```

**3434** is P17-ARCH-001's 3401 plus this milestone's 21 data tests and 12
compile-fail cases. **603** objects is 598 plus the three new structural
translation units and the compile-fail control, compiled once each.

**Cross-preset equivalence.** The structural suite gives the identical result
under `-O0 -g`, under the optimiser, and across a DLL boundary:

```text
debug-ext          All tests passed (1116 assertions in 31 test cases)
release-ext        All tests passed (1116 assertions in 31 test cases)
debug-shared-ext   All tests passed (1116 assertions in 31 test cases)
```

That matters more than usual here: `core/Id.hpp` and `core/math/Vector.hpp`
changed, and both are included by almost everything. An identical count in all
three presets is what says the additive change to a universally included header
reached nothing it should not have.

## The qualified tree is the committed tree

```text
| WHEN                              | WHOLE FINGERPRINT                        |
| frozen, before the first build    | fcf2ea160bbe3cad9cb94452570b9dac1a517f69 |
| recorded by the harness after the | fcf2ea160bbe3cad9cb94452570b9dac1a517f69 |
|   last test of the last preset    |                                          |
| recomputed before the commit      | fcf2ea160bbe3cad9cb94452570b9dac1a517f69 |
```

Component for component at all three readings:

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db
include            70d61d81326250119f2c468dd4ebd1749e8cfe42
src                3e2a39aebf8e47f691fe0fab578e13249d0711cf
tests              0169449c65ab89d071fce07df3d726c0ff0e2fb0
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

The first two readings are the harness's own, written into
`qualification/qualification-times.txt` at both ends of the run. The third was
recomputed by the same `read-tree` + `add -A` method immediately before
`git add`.

The whole value is a function of the eight paths **plus the base tree HEAD
pointed at**, which was `a4290cf` throughout. The invariant that survives a
moving HEAD is the component list, checkable against the published tree in one
command:

```bash
git fetch origin
for p in apps include src tests examples cmake CMakeLists.txt CMakePresets.json; do
  echo "$p $(git rev-parse "origin/main^{tree}:$p")"
done
```

What moved after the freeze: `docs/verification/P17-DATA-001/`, `TODO.md` and
`ROADMAP.md` — all documentation, none inside the fingerprint, none configured,
compiled, linked or read by a test.

## Result

```text
RESULT:   PASS
TESTS:    3434/3434 in debug-ext, release-ext and debug-shared-ext, each
          from clean; 0 warnings over 603 objects; 715 x 5 repeats in two
          presets; 17 stages, 0 failed
TREE:     fcf2ea160bbe3cad9cb94452570b9dac1a517f69, identical at all three
          readings
EVIDENCE: this directory
```

## Revision

First issue, 2026-10-07.
