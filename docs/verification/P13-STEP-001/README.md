# P13-STEP-001 — Assembly STEP Export / Read-back

```text
TASK:            P13-STEP-001 — Assembly STEP export / read-back
STATUS:          PASS
IMPLEMENTATION:  geometry::writeStepAssembly() -- an AP214 product structure
                 written through XCAF: one product per part, one component
                 instance per placement, under one assembly product.
                 io::exportStep()/exportStl() branch on whether the document
                 has components, regenerate, solve, and export the SOLVED
                 placements. A part document takes the old path untouched.
TESTS:           36 new -- 34 in tests/io/AssemblyStepTests.cpp,
                 2 in tests/io/StepExportTests.cpp
VALIDATION:      every placement checked against bounds derived on paper,
                 measured after a round trip through a STEP reader that
                 shares no code with the writer
REGRESSION:      1631/1631 on debug, release and debug-shared, each from
                 clean; 898/898 five times over in release and debug;
                 0 compiler warnings in all three builds; all 36 new
                 tests confirmed by name in every ctest log; 14/14
                 stages exit 0
ADVERSARIAL:     9 findings, all resolved -- 4 were untrue claims in the
                 code that arrived in the working tree, 2 were tests that
                 could not catch what they were named for, 1 was flakiness
                 in a test of mine, 2 were coverage gaps.
                 0 defects found in previously qualified code
RESULT:          PASS
EVIDENCE:        this directory
```

What is claimed: an assembly leaves BetterCAD as an AP214 STEP file carrying
its product structure — each part once, each active component placed where
the **solve** put it — and comes back through an independent reader with every
instance in the position arithmetic says it should be, to 1e-7 mm.

What is **not** claimed: byte-reproducible STEP output (the reason is
measured and recorded below), AP242 semantic assembly, production STEP
import, or any committed assembly model — that is `P13-REFMOD-001`.

---

## Baseline

```text
HEAD at start        39354e4  BetterCAD: authorize P13-STEP-001 assembly STEP
                              export and read-back
origin/main at start 39354e4  (equal)
authorised by        TODO.md "NEXT — P13-STEP-001"
```

The working tree at the start of this session already carried an unfinished
implementation from an earlier session — `writeStepAssembly()`, the assembly
branch in `io::exportStep()`, the read-back extension and a first test file.
It built clean and its own 22 tests passed.

It is **not** treated here as finished work. Every claim in it was re-derived
from the code; four of its statements turned out to be untrue and two of its
tests could not have caught what they were named for. All of that is under
Adversarial Review.

## Scope

Authorised: the seventeen items of `P13-STEP-001`.

Deliberately not done: **no committed assembly model** — that is
`P13-REFMOD-001`, and the milestone text says so. **No production STEP
import** — the milestone text puts the scope fork plainly, and this takes
the first option: read-back stays test infrastructure.

## STEP export contract

Stated here, and now stated in `include/bettercad/io/ModelExport.hpp` too.

```text
a document with NO components
    the result bodies -- features whose bodies no other feature consumes
    (features::resultFeatures()) -- each in the model's own coordinates,
    one STEP product per body, named after its feature.
    Unchanged from before this milestone.

a document WITH components
    the assembly.
```

| Question | Answer |
| --- | --- |
| Which configuration | the document's active one |
| Which components | `assembly::activeComponents()` — every component not suppressed in that configuration |
| Which transform | `Regenerator::transform(component)` — what the assembly final pass published **after the solve** |
| Never | `ComponentPlacement` (the intent), and never a stale transform: the pass publishes none rather than one an edit out of date (ADR-005) |
| Suppressed component | absent; its part absent too, unless another active component places it |
| Unresolved reference | explicit failure, no file written |
| Product name | the part's document-object name |
| Instance name | the component's name |
| Assembly root name | the document name |
| Name collisions | impossible — `Document::addObject()` refuses a duplicate name |
| Instance order | ascending `ComponentId` |
| Product order | the order parts are first placed |
| Structure | AP214: one `PRODUCT_DEFINITION` per part, one `NEXT_ASSEMBLY_USAGE_OCCURRENCE` per instance, under one assembly product |

The pipeline is the one the milestone demands, with no shortcut:

```text
canonical assembly intent
  -> regenerate (assembly handlers registered)
  -> solve
  -> derived world transforms
  -> STEP
```

### Failure conditions, as measured

| Condition | Code | Where it is caught |
| --- | --- | --- |
| No components in force in the active configuration | `FailedPrecondition` | the export |
| A component's part is gone | `FailedPrecondition` | **regeneration** |
| A component places a part in another document | `FailedPrecondition` | **regeneration** |
| The assembly does not solve | `FailedPrecondition` | the export (no transform published) |
| The file cannot be written | `IoError` | the shared atomic write |

The middle two are worth the emphasis. `placedAssembly()` carries `NotFound`
guards for a part with no body and for a cross-document part, and **neither
is reachable**: the dependency pass reports both before the export ever walks
the components, and says more than the guard would —

```text
the assembly does not regenerate:
  Orphan: object:3 references object:2, which does not exist
  Ground: not regenerated (depends on a failed item)
```

The guards are kept as defence in depth; the tests pin the behaviour that
actually happens, which is not what the first draft of this contract claimed.

At the geometry layer, `writeStepAssembly()` refuses `InvalidArgument` for:
no instances, an empty part, an instance naming a part that is not there, a
part no instance places, and a placement that is not finite.

## Active component geometry

`io::exportStep()` branches on `isAssembly()` — does the document contain a
component, suppressed or not. **All 24 committed `.bcad` models were checked:
not one contains a component**, so every one takes the unchanged part path,
as do the 14 `cli.export-step.*` process tests and the 4 `cli.export-stl.*`.

Each active component resolves its part, takes that part's regenerated body,
and is written as an instance of it. A part is written **once** however many
components place it.

## Transform validation

**Every expected value is arithmetic on the box, done on paper, and owes
nothing to the code that produced the file.** A 40 × 30 × 10 mm block at the
origin, placed, measured after a round trip.

| Case | Expected AABB (mm), derived by hand | Result |
| --- | --- | --- |
| Identity | 0..40, 0..30, 0..10 | PASS |
| +25 X | 25..65, 0..30, 0..10 | PASS |
| −17 Y | 0..40, −17..13, 0..10 | PASS |
| +50 Z | 0..40, 0..30, 50..60 | PASS |
| (5, 7, 11) | 5..45, 7..37, 11..21 | PASS |
| 90° about X → (x, −z, y) | 0..40, −10..0, 0..30 | PASS |
| 90° about Y → (z, y, −x) | 0..10, 0..30, −40..0 | PASS |
| 90° about Z → (−y, x, z) | −30..0, 0..40, 0..10 | PASS |
| 90° about Z **then** +100 X | 70..100, 0..40, 0..10 | PASS |

Tolerance 1e-7 mm on every bound. Justified: writer and reader both work in
millimetres and the geometry is planar, so the only error is the decimal text
of the file — far below 1e-7 mm, and far below anything that could hide a
misplacement.

The composed case is the sharp one: if the translation were applied in the
*rotated* frame rather than along the model's own axes, the block would land
in −30..0, 100..140. The case tells those apart.

Two checks a bounding box alone cannot make:

* **Centre of volume** to 1e-6 mm on every case — a symmetric box's bounding
  box cannot distinguish a 180° turn; its centroid can.
* **Volume unchanged** at 1e-9 relative — the cheapest evidence the shape was
  moved rather than distorted.

### The solve, not the intent

`Arm` is placed 50 mm up as intent and a coincident mate pulls it onto `Base`;
the file contains z = 0..10, not 50..60.

A mechanical mate is covered too. A `Revolute` hinge holds `Arm`'s Z axis
collinear with `Base`'s and leaves the turn free, so the assertions are the
ones the *joint* guarantees, measured rotation-invariantly:

```text
arm z extent       = 10 mm exactly   -> not tilted
centroid distance  = 25 mm from the world Z axis
                     sqrt(20^2 + 15^2), the block's own centroid offset,
                     invariant under the free rotation
the intent would give 100 mm
```

## Configuration and suppression

| Case | Expected | Measured |
| --- | --- | --- |
| Base config, A+B+C | 3 instances | 3 |
| B suppressed | 2 instances, nothing in B's 60..100 span, 2 × 12000 mm³ | as expected |
| B unsuppressed | 3 instances, B back at 60..100 | as expected |
| `Full` vs `Minimal` | 2 instances vs 1 | as expected |
| Every component suppressed | explicit failure, no file | `FailedPrecondition`, "no components in force" |
| Only component of a part suppressed | that **product** gone, not just its instance | 1 product, 1 instance, 12000 mm³ |

Two cases defeat checks that look sufficient:

* **A suppressed component hidden inside another.** Two components in exactly
  the same space, one suppressed. Total bounds are identical either way, so
  only the instance count and the per-instance names can tell — and the total
  volume halves, which proves the solid is gone rather than merely unnamed.
* **The only component of a part suppressed.** Suppression has to reach the
  *product* list, or a reader is shown a part the assembly does not contain.

## Product structure

`STEPCAFControl_Writer` is given a real XCAF assembly: `AddShape` per part,
`AddComponent` per instance with a `TopLoc_Location`. `AddComponent`
references the part's product rather than copying its geometry.

The read-back confirms it **from the file**: four components of one part come
back as one product in four places, and products and instances are read
through separate XCAF calls (`GetReferredShape` vs `GetShape`), which is what
distinguishes instancing from duplication.

Not claimed: AP242 semantic assembly. No PMI, no GD&T, no validation
properties.

## Ordering and naming

```text
instances   ascending ComponentId
products    the order parts are first placed
```

The product rule is pinned by a case built so the two candidate rules
disagree: the part added *second* is placed by the component created *first*,
so first-placement order and ascending-part-object order give different
answers, and the file says which holds. (The implementation's comment claimed
the other one — see finding 2.)

Names cannot collide: a product is named after its document object, an
instance after its component, and `Document::addObject()` refuses a duplicate.
Asserted at that source rather than assumed at the writer.

## Read-back path

`tests/support/occt/StepReadBack.cpp` gains `readStepStructure()`, reading
through `STEPCAFControl_Reader` — **a different reader from the one
`readStepFile()` uses, and a different one again from the writer**, so the
measurement shares no code with the thing it measures.

Per placed instance it reports: name, the product it refers to, volume,
bounding box, centre of volume and shape validity; per file: whether the root
is an assembly, its name, and the distinct products.

The existing `readStepFile()` is untouched — the diff adds lines and deletes
none.

## Geometry equivalence and counts

| Fixture | Parts | Instances | Independent total |
| --- | --- | --- | --- |
| One component | 1 | 1 | 12000 mm³ |
| Two components, translated | 1 | 2 | 24000 mm³ |
| Four instances 60 mm apart | 1 | 4 | 48000 mm³, span 0..220 |
| Two different parts | 2 | 2 | 12000 + 2000 = 14000 mm³ |
| Two parts, three instances | 2 | 3 | 2 × 12000 + 2000 = 26000 mm³ |
| Suppressed component | 1 | 2 of 3 | 24000 mm³ |
| Solved (coincident) | 1 | 2 | both on z = 0..10 |
| Solved (revolute) | 1 | 2 | arm 25 mm from the Z axis |

Tolerances: 1e-9 relative on volume (geometric accumulation through a
decimal-text round trip), 1e-7 mm on bounds, 1e-6 mm on centroids
(`VolumePropertiesGK` integrates, so it is looser than a bound taken straight
from the geometry).

Two instances at the **same** location stay two instances — the case where a
deduplicating writer or reader could fold two bolts into one, which no
aggregate measure could see.

## Failure atomicity

`detail::writeFileAtomically()` writes `<path>.tmp` and renames; the temporary
is removed on both failure paths. Every failure returns *before* the write is
reached, so a failed export writes nothing at all.

Proved on bytes: export successfully, break the assembly, export again to the
same path — the earlier file is byte-for-byte what it was, not truncated, not
replaced by a partial file. The document is unchanged (`components()` still 2),
and a later valid export succeeds and reads back correctly.

## Determinism

**A finding, and the milestone's own expectation was wrong.** `TODO.md` said
two exports of one assembly "should be byte-identical, and that is checkable
today". It is checkable, it was checked, and it is false:

```text
export 1   #380 = NEXT_ASSEMBLY_USAGE_OCCURRENCE('1','BlockA','',#5,#35,$);
export 2   #380 = NEXT_ASSEMBLY_USAGE_OCCURRENCE('4','BlockA','',#5,#35,$);
```

OCCT numbers assembly occurrences from a counter that lives for the life of
the **process**, not the writer — in a full-suite run the same field read
`'45'`. Nothing BetterCAD does causes it, no option turns it off, and
`StepOptions::timeStamp` fixes the header without touching it.

So byte determinism is **not claimed**. What is claimed and tested:

```text
part export      the DATA section is byte-identical between two exports
assembly export  the DATA section is byte-identical between two exports once
                 the occurrence counter -- and nothing else -- is normalised.
                 Entity numbering, every coordinate, every name, the units
                 and the product structure all match exactly
both             structural and geometric equality: same products, same
                 instance order, same names, bounds to 1e-12 mm, volumes to
                 1e-12 relative
```

The header is excluded throughout, and the reason is stated: it carries a
wall-clock timestamp and the output file's own name.

Also tested: component order does not depend on creation order, and
save → load → regenerate → solve → export reproduces the same file
geometrically, instance for instance, to 1e-12 mm.

## CLI / core validation

The CLI has no export semantics of its own — it calls `io::exportStep()`.
Verified by building one assembly and exporting it twice: in-process through
the core API, and through the real `export-step` command on a document that
came off disk. The two files are compared **as geometry**, instance by
instance: names, products, volumes to 1e-12, bounds to 1e-12 mm, centroids to
1e-9 mm. Byte comparison is deliberately not used, for the reason above.

The whole headless path is covered: `status` reports `Components (3)`, then
`export-step` writes the file, then the read-back confirms three instances of
one product at 0, 50 and 100 mm. The CLI refuses an assembly that cannot
solve — non-zero exit, message on stderr, and **no file**.

Reporting was corrected: an assembly of twelve brackets used to be announced
as "1 body", which is true of the file and misleading to an engineer. It now
reads `assembly of 2 components (Base, Arm) from 1 body (Block)`.

## STL

`exportStl()` shares `placedAssembly()`, so it was in the blast radius and is
verified rather than assumed. STL has no product structure, so the instances
are flattened — but they are still individually placed, and that is read out
of the **vertices**: two blocks 100 mm apart give 24 triangles spanning
0..140 mm in X and one block's depth in Y and Z, enclosing 2 × 12000 mm³.
(The first version of that test counted instances and triangles, which cannot
tell a placed assembly from two copies at the origin — finding 8.)

Tolerance 1e-3 mm, because STL stores 32-bit floats.

## Adversarial review

The tree that arrived at the start of this session built clean and passed its
own tests. That is the state in which a milestone is most likely to be ticked
and most likely to be wrong, so every claim in it was re-derived from the
code. **Nine findings, all resolved.**

### Untrue claims

**1 — The determinism claim was false, and so was the milestone's.** `TODO.md`
said two exports of one assembly should be byte-identical. The test had not
been written; the delivered test checked only *geometric* agreement, which is
weaker and passes either way. Writing the byte test showed the DATA sections
differ, and the diff located the cause exactly: OCCT's process-global
assembly-occurrence counter.
*Resolved* by stating the truth at the precision it holds — normalise that one
field, hold everything else to byte equality — and recording the limitation
here and in the test. A new companion test pins that a fixed timestamp does
**not** make assembly output reproducible, beside the pre-existing part-path
test that says it does, so the difference cannot be rediscovered as a surprise.

**2 — A comment claimed an ordering the code does not have.**
`src/io/ModelExport.cpp` said products are emitted "in ascending part-object
order, so the file does not depend on the order components happen to be
visited". They are not: parts are appended as components are walked, so the
order is *first placement*. Both are deterministic, so no output was wrong —
but the stated reason was, and a change made on the strength of that comment
would have been.
*Resolved*: comment corrected, and the behaviour pinned by a test built so the
two candidate rules give different answers.

**3 — A public contract documented behaviour the code does not have.**
`ExportSummary::bodies` was documented as "the distinct bodies written — for
an assembly these are the parts, each written once". True of STEP; **false of
STL**, whose assembly path emits one entry per *instance*.
*Resolved*: the field now documents all three cases and names `components` as
the count that means the same thing in both.

**4 — Two documented failure codes were wrong.** The contract claimed a
component whose part is missing, and one placing a part in another document,
each fail the export with `NotFound`. Neither does: **regeneration reports
both first**, with `FailedPrecondition` and a better diagnostic, and the
export's `NotFound` guards are unreachable. Found by refusing to write a
contract row that no test backed.
*Resolved*: both cases now have tests pinning the real code, message and
blocked-dependent reporting; the contract says where each is caught; the
guards stay as defence in depth, labelled as such.

### Tests that could not catch what they were named for

**5 — The mirror test could not have detected a mirror.**
`PlacementsAreRigidSoNothingIsMirrored` asserted the solid came back valid
with the right volume, commenting that a reflection "the kernel reports as an
invalid or negative-volume solid". It does not: OCCT turns a
negatively-transformed solid's faces inside out precisely so it stays valid
and positive. A reflection preserves volume exactly, and a mirrored
40 × 30 × 10 box is still a 40 × 30 × 10 box — not even the bounding box
would differ. The test would have passed on mirrored output.
*Resolved*: the property is asserted where it lives — the determinant of every
transform the solve published, through the production
`Regenerator`/`reversesOrientation()` path — with the volume check kept and
explicitly labelled as *not* the check that catches a mirror.

**6 — The STL test could not have detected an unplaced assembly.** It counted
components, bodies and triangles, all of which are identical whether the two
instances are 100 mm apart or stacked at the origin.
*Resolved*: the binary STL is parsed and its **vertices** measured — span,
per-axis bounds and enclosed volume.

### Flakiness introduced by this work

**7 — A byte-determinism test of mine was flaky against the same counter.** It
compared the two DATA sections' *lengths*. Because the counter is
process-global, a run where it steps from 99 to 100 makes the second file
legitimately one byte longer, and the test would have failed on nothing but
how many assemblies the suite had exported earlier. Caught before the freeze.
*Resolved*: length comparison removed from both affected tests, with the reason
recorded in each. The normalised comparison is unaffected by how far the
counter has run.

### Coverage gaps

**8 — Suppression was never tested where it reaches the product list.** Every
suppression case placed a single part, so all of them could only show an
*instance* disappearing. Nothing proved that suppressing the only component of
a part removes that part's **product** — the case where a reader is shown a
part the assembly does not contain.
*Resolved*: test added. The freeze was three minutes old, so it was discarded
and the qualification re-run from clean rather than shipping the claim
untested.

**9 — The assembly path's `IoError` was untested.** The part path pins it, and
the write is the same call, but the assembly path only reaches it after a
regenerate and a solve.
*Resolved*: test added, which also supplies the second half of failure
atomicity — a valid export succeeding after a failed one.

### The seventeen questions, answered

| Question | Answer |
| --- | --- |
| Transforms applied twice? | No — bounds match single application exactly, on nine placements |
| Solved transforms omitted? | No — a component without one is a hard failure, never a fallback |
| Canonical placement exported instead of solved? | No — the coincident and revolute cases both export a position the intent did not name |
| Rotations in the wrong frame or order? | No — the compose case distinguishes model-axis from rotated-frame translation |
| Suppressed component leaking in? | No — by name, by count and by total volume, including one hidden inside another |
| Two instances collapsed onto one? | No — two components at the *same* location stay two |
| Component order changing semantics? | No — ascending `ComponentId`, independent of creation order |
| Stale solver state after a failed regeneration? | Structurally impossible — a fresh `Regenerator` per export, and the pass publishes no stale transform |
| Unresolved reference exporting old geometry? | No — explicit failure, no file (finding 4) |
| Mirrored / non-rigid transform entering? | No — `ComponentPlacement` cannot express one, and det = +1 is asserted on the solved transforms (finding 5) |
| Unit conversion changing scale? | No — exactly one conversion to kernel space (`occt::toModel`, the only `SetValues` in `src/`), now shared with `transformed()`; a metre/millimetre error would be 1000× against bounds held to 1e-7 mm |
| Product names colliding? | Impossible — `Document::addObject()` refuses a duplicate |
| Read-back falsely passing on total bounding box? | Prevented by design — per-instance bounds, centroids, volumes and product identity; the hidden-component case exists to break that shortcut |
| One missing component hidden by another's geometry? | Caught — identical aggregate measures; instance count and volume tell them apart |
| Debug and Release differing? | No — three presets, identical results |
| A failed writer leaving a file later read as valid? | No — temp-and-rename, temp removed on failure, earlier file byte-identical after a failed overwrite |
| Old part-export tests weakened? | No — `StepReadBack` is purely additive (no deleted lines), no existing test was edited, all 24 committed models are parts and take the unchanged path, and a part document is proved to export exactly as before |

## Known limitations

1. **STEP output is not byte-reproducible.** The header carries a wall-clock
   timestamp; OCCT numbers assembly occurrences from a process-global counter.
   Everything else is byte-identical. See Determinism.
2. **A body no component places is not exported.** Once a document has
   components, the export writes the assembly; a loose body would need a
   placement nobody specified. Pinned by
   `AssemblyStep_ABodyNoComponentPlacesIsNotInTheAssembly`, and **worth
   revisiting**: an engineer who models a base and never places it gets a file
   without it, and is told only that one body was written.
3. **Read-back is test infrastructure**, not `io::importStep()`.
4. **AP214, not AP242** — products, usage occurrences and placements only.
5. **STL assemblies are flattened**; the instances are still individually
   placed.
6. **Cross-document components cannot be exported** — regeneration refuses
   them, because cross-document dependencies are not implemented anywhere in
   P13.
7. **No committed assembly model** — that is `P13-REFMOD-001`. A committed
   assembly would make the process tests better and is noted for it.
8. **The export's two `NotFound` guards are unreachable** through any document
   state reachable today. Kept as defence in depth, not as live behaviour.

## Files changed

```text
include/bettercad/core/geometry/Exchange.hpp   StepInstance, StepAssembly,
                                               writeStepAssembly()
src/core/geometry/occt/OcctStep.cpp            the XCAF assembly writer;
                                               header/transfer shared with
                                               the part path
src/core/geometry/occt/OcctBody.hpp            toModel(RigidTransform3D) --
                                               the single conversion
src/core/geometry/occt/OcctTransform.cpp       now uses it
include/bettercad/io/ModelExport.hpp           the contract; ExportedComponent
src/io/ModelExport.cpp                         placedAssembly(); the branch
apps/bettercad_cli/ExportCommands.cpp          describeContents()
tests/support/occt/StepReadBack.{hpp,cpp}      readStepStructure() (additive)
tests/io/AssemblyStepTests.cpp                 new, 34 cases
tests/io/StepExportTests.cpp                   2 cases
tests/CMakeLists.txt                           the new file; TKLCAF, TKXCAF
```

## Full regression

Clean qualification of the frozen tree: for each preset, configure, remove
every build output, rebuild with warnings as errors, and run CTest only after
a successful build. **14/14 stages exit 0.**

| Preset | Targets | Warnings | Tests | Result | Time |
| --- | --- | --- | --- | --- | --- |
| `debug` | 447/447 from clean | 0 | 1631 | **1631/1631** | 190.58 s |
| `release` | 447/447 from clean | 0 | 1631 | **1631/1631** | 167.35 s |
| `debug-shared` | 447/447 from clean | 0 | 1631 | **1631/1631** | 187.45 s |

Repeat passes, `--repeat until-fail:5` over the STEP, export, assembly,
solver, configuration, persistence, reference-model and CLI selection:

| Preset | Tests | Runs each | Result | Time |
| --- | --- | --- | --- | --- |
| `release` | 898 | 5 | **898/898** | 616.69 s |
| `debug` | 898 | 5 | **898/898** | 696.18 s |

The repeats are not decoration here. OCCT's assembly-occurrence counter is
process-global, so a mistake in normalising it would show up as an
*intermittent* failure depending on how many assemblies ran earlier in the
suite — which five consecutive passes catch and one pass does not. One
flaky assertion of mine was removed for exactly that reason before the
freeze (finding 7).

```text
baseline before this milestone   1595 tests
new                                36 tests
total                            1631 tests
```

All 34 `AssemblyStep_*` tests and both new `StepExportTests` cases were
confirmed **by name** in all three ctest logs — no stale binary, no silently
undiscovered test.

### The qualified tree is the committed tree

Git tree IDs taken from a scratch index at the freeze, and recomputed after
the run:

```text
apps              8da209723f46d3e0194750fcaad4ee2981af723c
include           da4a989538fdfc7dbd8641540993d31dc30be5e3
src               6fc8350a9c8ab6acfe760f509e6b35ab58eb7068
tests             69037e35192416c64e5bbf0e60e4b9e27847f089
examples          0ef8346d0ff59a2101ed1fa61e452c06a849f14f
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

Identical before and after. Only `docs/verification/P13-STEP-001/` changed
during the run, which cannot affect the executables or the tests.

**The qualification was run three times.** The first two were discarded, not
because they failed, but because the freeze was broken deliberately: each time
a coverage gap was found that would have meant shipping a claim no test
backed (findings 8 and 4). Re-running from clean was the cheaper mistake.

Logs: [`qualification/`](qualification/) — `configure-*`, `clean-*`,
`build-*`, `ctest-*`, `ctest-repeat-*`, and `qualification-times.txt` with
every stage's exit code and wall-clock time.

## Result

```text
RESULT: PASS
```

```text
STEP assembly export correct                         PASS
all active component geometry exported               PASS
transforms correct                                   PASS  9 placements, 1e-7 mm
solved placement respected                           PASS  coincident + revolute
configuration / suppression correct                  PASS  incl. product removal
structure preserved to the documented level          PASS  AP214, not AP242
deterministic naming / order correct                 PASS
read-back path independent and working               PASS  a different reader
component / part counts correct                      PASS
geometry placement equivalence                       PASS
suppressed components absent                         PASS  by name, count, volume
invalid / unresolved states fail explicitly          PASS  where measured, not assumed
export failure atomicity                             PASS  proved on bytes
determinism                                          PASS  as precisely stated
adversarial review                                   PASS  9 findings, all resolved
full regression                                      PASS  1631/1631 x 3 presets
0 unexpected warnings                                PASS
evidence complete                                    PASS
```

## Revision

First revision. No part of this milestone has been revised after qualification.
