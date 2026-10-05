# P16-PERSIST-001 — Meshing Intent Persistence

```text
STATUS:   PASS
DATE:     2026-10-05
GATE:     meshing intent preserved + geometry references preserved + units
          preserved + derived mesh excluded as authority + malformed files
          rejected + deterministic serialization PASS
```

## Baseline

```text
HEAD:          8ae299942acb10410860ad5bc2425fe475f56f11
TREE:          82e2b1772202a06ed514bac3b13a284a8c7f34a1
origin/main:   8ae299942acb10410860ad5bc2425fe475f56f11  (equal)
log:           8ae2999 BetterCAD: add meshing commands with undo and redo
working tree:  clean at start of milestone
compiler:      GCC 16.1.0 (MinGW-W64 x86_64-ucrt-posix-seh, WinLibs r4)
cmake:         4.4.1, Ninja
build root:    C:/Users/uqhas/AppData/Local/bc-build  (outside OneDrive, -ext presets)
```

## Prerequisites

Checked by reading each milestone's recorded RESULT, not its checkbox:

```text
P16-ARCH-001      PASS      P16-SIZE-001      PASS
P16-DATA-001      PASS      P16-QUALITY-001   PASS
P16-GEOM-001      PASS      P16-MAP-001       PASS
P16-SURF-001      PASS      P16-VIZ-001       PASS
P16-VOL-001       PASS      P16-CMD-001       PASS
INFRA-NETGEN-001  PASS
```

So the milestone is **not blocked**.

## Scope

```text
IN     the persisted schema for canonical meshing intent, inside the existing
       document format
IN     global sizing, local sizing, face references, boundary-set intent, the
       quality threshold policy, surface discretisation
IN     strict load validation routed through the core's validators
IN     round trip, determinism, malformed input, backward compatibility,
       regeneration after load, commands after load
IN     a hand-written reference model, and fresh-process load tests

OUT    a generated-mesh cache. NOT IMPLEMENTED: no measured performance
       requirement exists, the brief forbids adding one without, and nothing
       in the tree caches a mesh to disk
OUT    a format version bump, a meshing-specific schema version, a sidecar
OUT    any new validation logic -- it would be a second copy of P16-SIZE's
OUT    command-history persistence: BetterCAD persists no undo stack
OUT    CLI meshing commands (P16-CLI-001) and the carried FileIo
       atomic-replace defect
```

## Persistence architecture audit

In full in [PERSISTENCE_AUDIT.md](PERSISTENCE_AUDIT.md). The audit, written
before any production code, found that **most of this milestone's questions
were already answered** — and that materially changed the work.

```text
Persisted subsystem   one native JSON document, .bcad
Schema location       src/io/json/, one *Json.cpp per object kind,
                      dispatched by dynamic_cast
Versioning model      a read RANGE, 1..2; writes only the newest
Deterministic?        conditionally -- ordered_json preserves INSERTION order
Strict parser?        yes: requireObject takes an allowed-key list
Migration             in the readers, keyed on the version
Reusable for P16?     entirely
```

Five things the format already settles: **no version bump** (its own rule:
adding an object type is additive), **backward and forward compatibility**
(an old file loads and has no controls; an old reader refuses a new type by
name), **load atomicity** (`documentFromJson` builds a new `Document`),
**duplicate object IDs and the allocator** (`insertObject` refuses an ID in
use; `last_allocated_id` is persisted), and **units** (bare SI doubles).

And one decisive reuse: `faceNameToJson` / `faceNameFromJson` already exist
and are complete. P16 writes no reference serialization of its own.

## Document schema and version

```text
format     "bettercad-document"
version    2, UNCHANGED.  kOldestReadableDocumentVersion stays 1.
```

Verified: a version-2 file with no controls loads and gains none; a
version-1 file loads and gains none; a P16 file says `"version": 2`; and an
unrecognised object type is refused by name.

## The P16 meshing schema

Field by field, with units, requiredness and validator, in
[SCHEMA.md](SCHEMA.md). A control is an entry in the existing `objects`
array:

```json
{"id": 7, "type": "mesh-control", "name": "PlateMesh", "data": {
  "body": 6,
  "surface": {"linear_deflection": 0.0001, "angular_deflection": 0.3490658503988659},
  "sizing": {"target_size": 0.008,
             "local": [{"face": {"feature": 6, "face": {"role": "end_cap"}},
                        "target_size": 0.002}]},
  "quality": {"limits": [{"metric": "tet_min_dihedral_angle",
                          "warning": 0.3, "failure": 0.1}]},
  "boundary_sets": [{"id": 1, "name": "fixed_end",
                     "faces": [{"feature": 6, "face": {"role": "start_cap"}}]}]
}}
```

Lengths in **metres**, angles in **radians**, as every quantity in this
format already is.

## Units

```text
1 mm, 10 mm, 1 m, 0.001 m, 0.0125 m   exact canonical equality, globally and
                                      as a local control
10 mm  ->  "target_size": 0.01        the file's own unit, pinned directly
```

The second line is the one a round trip cannot give: a serializer writing
millimetres would round-trip perfectly and still be wrong for every other
reader.

## Geometry references

Persisted through the format's existing `faceNameToJson`, which writes the
feature ID and **every** field of the selector — role as a string from a
table, entity, path edge, along-sketch, chamfer edge and copies — and
validates the selector on reading.

**Resolved and unresolved are both intent.** A reference naming a face that
does not currently exist loads and is kept verbatim; a reference whose
encoding is wrong is refused. That distinction is the milestone's most
consequential behaviour and is tested in both directions —
[MALFORMED_INPUTS.md](MALFORMED_INPUTS.md) §2.

## Identity preservation

```text
MeshControlId      the document's ObjectId, restored by the existing loader
BoundarySetId      restored as written, asserted by value
local control      HAS NO synthetic identity -- it is keyed by its FaceName
                   (P16-SIZE-001), so nothing is allocated on load
allocator          last_allocated_id persisted; a control created after a
                   load cannot take an ID the file used
```

## Derived-state authority boundary

```text
PERSISTED AS AUTHORITY        NOT PERSISTED
global sizing                 surface triangles, mesh nodes, Tet4 connectivity
local sizing intent           boundary facets, derived node/element sets
face references               quality REPORT, geometry-mesh mapping
boundary-set intent           render buffers, Netgen tags and handles
quality threshold POLICY
```

Structural rather than a policy: `MeshControlDefinition` has no member for
any of them. Checked against the written bytes anyway, with a mesh in memory
at save time, and by measurement:

```text
mesh  745 -> 1871 elements      file  1778 -> 1780 bytes      +2 bytes
```

**Cache: NOT IMPLEMENTED**, deliberately — no measured performance
requirement, and the brief forbids introducing one without.

## Round trip, determinism and regeneration

In full in [ROUND_TRIP.md](ROUND_TRIP.md).

```text
full canonical intent round trip         fingerprint equal        PASS
save twice                               byte-identical           PASS
save / load / save                       byte-identical           PASS
save / load / save / load / save         byte-identical           PASS
two documents, controls added in
    opposite order                       identical sections       PASS
loaded document: intent, NO mesh         currency = NoMesh        PASS
regenerate after load                    same element count       PASS
local refinement after load              refines the same face    PASS
boundary set after load                  fullyResolved, 34 facets PASS
quality after load                       recomputed, 0 invalid    PASS
commands after load: undo/redo exact                              PASS
fresh process (real CLI executable)      exit 0, both commands    PASS
```

## Malformed input and atomicity

The 23-case matrix, with which layer refuses each one, is in
[MALFORMED_INPUTS.md](MALFORMED_INPUTS.md). Summary:

```text
parser    syntax, types, required fields, unknown fields, a repeated metric,
          a malformed selector            -> ParseError with the JSON path
core      positive and finite sizes, duplicate face controls, duplicate set
          identities, an invalid body     -> the validator's own code
file      missing or unreadable           -> IoError

failed load leaves the open document's fingerprint and revision unchanged
```

No sizing rule is restated in the serializer: reading ends in
`MeshControl::create`.

## Adversarial review

27 attack questions and the findings in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
questions:              27
findings:               8
production defects:     0
audit defects:          1  (B1: a non-finite value never reaches the core;
                            the audit claimed it would, and was corrected
                            against the measurement)
test defects:           3  (B3 a metric guard covering 4 of 18; B4 a block
                            that cannot show mesh growth; B5 three guessed
                            API signatures)
designed signals:       1  (B2 P16-CMD-001's boundary test failed on purpose
                            and was replaced where it was marked)
accepted limitations:   2  (B6 nameIn's "unknown" fallback, guarded by test;
                            B7 io -> meshing, which P16-ARCH-001 designed in)
strengthenings:         1  (B8 the hand-written reference anchors the schema)
remaining open:         0
```

## Mutation testing

```text
12 mutations of src/io/json/MeshControlJson.cpp
12 killed, EVERY ONE BY A TEST
 0 survivors
```

One (M11) was first killed only by the compiler — writing both deflections as
defaults left a parameter unused and `-Werror` rejected it, so the mutation
was never evaluated — and was rewritten so a test had to catch it. M5 and M6
each die on exactly one assertion, which is sufficient and is worth knowing:
the edit-order determinism test is the only thing between canonical ordering
and a file whose bytes depend on the order somebody clicked.

The first run was orphaned by a session ending and left mutation M3 **applied**
to the production file, invisible to `git status` and `git diff` because the
file is untracked — finding B9. Caught by diffing against the harness's own
`pristine/` snapshot. Detail in
[qualification/mutation/README.md](qualification/mutation/README.md).

## Full regression

```text
PRESET            FULL SUITE     WARNINGS  OBJECTS  NO-OP REBUILD
debug-ext         3301 / 3301       0        589    0 compiles, 0 links
release-ext       3301 / 3301       0        589    0 compiles, 0 links
debug-shared-ext  3301 / 3301       0        589    0 compiles, 0 links

REPEAT (5x each of 1359 selected tests, back to back)
release-ext       1359 / 1359                       357.29 s
debug-ext         1359 / 1359                       357.55 s
```

Unfiltered, after a fresh configure and a clean build in each preset. Every
no-op rebuild did nothing, so the binaries tested are the ones just built. The
shared build linked 9 DLLs including `libbettercad_io.dll` **and**
`libbettercad_meshing.dll`, so the new `io → meshing` edge was exercised
across a real DLL boundary.

Pre-freeze, both full suites showed one failure, `cli.new.unicode-path`, a
code-page artefact: those runs were made without `chcp 65001`, which
`qualify.cmd` sets as its first act. The qualifying runs are 3301/3301.

## Determinism and cross-preset equivalence

The repeat set is **1359 of 3301 tests** — the whole document-format test
surface, not just the new tests, because this milestone changes the
`dynamic_cast` dispatch that every object kind's save and load goes through.
A regression there would surface in the material, sketch and feature
serialization tests before it surfaced in the meshing ones.

`compile_fail` is **excluded on purpose**: this milestone adds no compile-fail
case, and `ObjectJson.hpp` is private to `io` so no compile-fail translation
unit can include it. Each of those cases is a real compilation; five rounds of
roughly two hundred would cost about two hours and could report nothing about
persistence. P16-CMD-001 included them because it added thirteen. The
reasoning is written into
[qualification/run-qualification.cmd](qualification/run-qualification.cmd).

Byte-identical serialization holds under `-O0 -g`, under the optimiser, and
across a DLL boundary.

## Result

```text
RESULT:    PASS
GATE:      met -- the schema is explicit and backend-independent; global and
           local sizing, face references and boundary-set intent all persist
           with their identities; units survive exactly; no generated mesh is
           canonical authority, measured at 2 bytes of file change for 1126
           extra elements; malformed files are rejected atomically; and
           serialization is byte-identical including save/load/save
EVIDENCE:  this directory; qualification/qualification-times.txt for the run,
           qualification/mutation/results.txt for the mutations
TREE:      a8ffe41b42ddab5860a15a2463f12bf1efcbbb83, qualified and committed
```

## Known limitations

1. **No generated-mesh cache**, by decision. A load leaves no mesh and the
   first generation after a load does the full work. Introducing a cache needs
   measured evidence and, per the derived-state rule, a geometry and settings
   fingerprint plus disposability.
2. **The stored order of local controls is not preserved** across a save — a
   reloaded document has them in canonical order. It is not semantic, and
   writing the canonical order is what makes save/load/save converge to
   identical bytes.
3. **`nameIn` returns `"unknown"` for an unmapped enumerator**, so a quality
   metric added without a file key would serialize silently and fail to load.
   Guarded by a test over every threshold-capable metric rather than by the
   helper, because the helper is the codebase's shared idiom.
4. **The GUI still creates no `MeshControl`** (carried from P16-CMD-001), so
   nothing in the application yet saves meshing intent. The CLI has no meshing
   commands either — P16-CLI-001 owns those.
5. **The carried FileIo atomic-replace defect is untouched.** P16 writes
   through the existing `writeFileAtomically` and adds no writer of its own.
