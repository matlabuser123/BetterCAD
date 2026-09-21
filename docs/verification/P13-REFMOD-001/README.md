# P13-REFMOD-001 — Production Assembly Reference Models

```text
TASK:            P13-REFMOD-001 — Production assembly reference models
STATUS:          PASS
IMPLEMENTATION:  eight committed assembly models, RM-A .. RM-H, built through
                 the public document, sketch, feature, component, mate and
                 configuration APIs and committed as .bcad beside the twelve
                 part models. No new CAD capability: this milestone proves the
                 qualified P13 capabilities work together.
TESTS:           43 new -- 26 in-process cases across
                 tests/reference/AssemblyModelsTests.cpp and
                 AssemblyWorkflowTests.cpp, and 17 real-process CLI tests
VALIDATION:      every DOF, status and placement derived by hand from the mate
                 equation counts BEFORE any solver output was read; STEP
                 contents measured after a round trip through an independent
                 reader
REGRESSION:      1674/1674 on debug, release and debug-shared, each from
                 clean; 986/986 five times over in release and debug;
                 0 compiler warnings in all three builds; all 43 new
                 tests confirmed by name in every ctest log; 14/14
                 stages exit 0
ADVERSARIAL:     7 findings, all resolved -- 2 wrong conventions in new
                 models, 2 tests that could not catch what they were named
                 for, 2 capability gaps the matrix exposed, 1 existing test
                 this milestone's own assets invalidated.
                 0 defects found in previously qualified code
RESULT:          PASS
EVIDENCE:        this directory
```

What is claimed: eight assemblies, committed as files, that between them
exercise every P13 capability a document can carry -- and that each one
regenerates, solves, saves, loads, exports and reads back to values worked out
on paper rather than read off the tool.

What is **not** claimed: that these are the only shapes worth having, that the
suite covers cross-document references (P13 does not implement them), or that
a reference model can carry a command history (it cannot -- that capability is
covered by a test instead, and the evidence says so).

---

## Baseline

```text
HEAD at start        bc256fe  BetterCAD: authorize P13-REFMOD-001 production
                              assembly reference models
origin/main          4c0bca2  before the authorisation commit
authorised by        TODO.md "CURRENT — P13-REFMOD-001"
```

The working tree carried the user's own restructuring of `TODO.md`, which
opened this milestone and brought the file back to what CLAUDE.md says it is.
That was committed first, as `bc256fe`, so the authorisation is in history
before the work it authorises.

## Scope

Authorised: the seventeen items of `P13-REFMOD-001`.

This milestone adds **no CAD capability**. Every line of `src/` and
`include/` is untouched; the new code is reference-model builders under
`examples/`, their committed `.bcad` output, and tests. One existing test was
changed, and that change is set out under Adversarial Review.

## The reference suite

Eight models, each with a job. They live in `examples/models/reference/`
beside the twelve part models, are built by
`examples/reference_models/Assembly*.cpp` through the public API only, and are
catalogued in `AssemblyReferenceModels.hpp` the way the part models are in
`ReferenceModels.hpp`. **No parallel framework**: the same `ModelBuilder`,
`SketchBuilder`, fixed document IDs and example program.

| | Model | What it is for | Components | Mates | Parts |
| --- | --- | --- | --- | --- | --- |
| RM-A | GroundedPair | the baseline: the whole pipeline, with every number obvious | 2 | 2 | 2 |
| RM-B | ConstrainedStack | fully constrained, and two instances of one part | 3 | 11 | 2 |
| RM-C | ShaftInBore | the freedoms a concentric mate leaves, and a parameter-driven placement | 2 | 2 | 2 |
| RM-D | JointSet | all four mechanical joints, each keeping its own freedom | 5 | 5 | 5 |
| RM-E | ConfiguredFrame | three configurations: everything, a component out, a mate released | 4 | 16 | 3 |
| RM-F | DrivenCover | a mate on a NAMED face, driven by the parameter that moves it | 2 | 5 | 2 |
| RM-G | Machine | production scale: 8 components, 4 parts, both mate kinds, two configurations | 8 | 31 | 4 |
| RM-H | FaultCases | committed BROKEN, and the configuration that rescues it | 2 | 3 | 1 |

Each model builds its own parts, because P13 assemblies live inside one
document and cross-document references are not implemented.

**RM-H is committed in its failing state on purpose.** A fault fixture that
only fails once a test reaches in and breaks it is testing the test. The
export, the CLI and the solver all have to be provably unable to pretend it is
fine, and they are checked on the file as committed.

## Where the expected numbers come from

Every count was derived by hand from the mate equation counts that
`P13-SOLVE-001` and `P13-MATE-002` qualified, **before any of these models was
run**:

```text
Fixed          grounds a component: its 6 unknowns leave the problem
Distance   1   Parallel      2   Coincident   3   Perpendicular  1
Angle      1   Planar        3   Concentric   4   Cylindrical    4
Revolute   5   Slider        5

unknowns  = 6 x (active components - grounded)
equations = the sum over active mates
DOF       = unknowns - rank, which here is unknowns - equations
```

The last line is a claim, not an assumption, so **every model asserts
`redundant` is empty**. If a mate ever became implied by the others the
arithmetic would stop matching and the test would say so rather than quietly
agreeing.

| | unknowns | equations | DOF | status | measured |
| --- | --- | --- | --- | --- | --- |
| RM-A | 0 | 0 | 0 | FULLY_CONSTRAINED | as derived |
| RM-B | 12 | 12 | 0 | FULLY_CONSTRAINED | as derived |
| RM-C | 6 | 4 | 2 | UNDER_CONSTRAINED | as derived |
| RM-D | 24 | 17 | 7 | UNDER_CONSTRAINED | as derived |
| RM-E | 18 | 18 | 0 | FULLY_CONSTRAINED | as derived |
| RM-F | 6 | 6 | 0 | FULLY_CONSTRAINED | as derived |
| RM-G | 42 | 40 | 2 | UNDER_CONSTRAINED | as derived |
| RM-H | 6 | 2 | 5 | INCONSISTENT | as derived |

Every one matched on the first run. The derivations that were **not** right
first time were two placement conventions, and they are under Adversarial
Review.

Per configuration, derived the same way and measured the same way:

| | active components | active mates | unknowns | equations | DOF | status |
| --- | --- | --- | --- | --- | --- | --- |
| RM-E `Full` | 4 | 16 | 18 | 18 | 0 | FULLY_CONSTRAINED |
| RM-E `NoBrace` | 3 | 11 | 12 | 12 | 0 | FULLY_CONSTRAINED |
| RM-E `Loose` | 4 | 15 | 18 | 17 | 1 | UNDER_CONSTRAINED |
| RM-G `Assembled` | 8 | 31 | 42 | 40 | 2 | UNDER_CONSTRAINED |
| RM-G `Bare` | 3 | 7 | 12 | 10 | 2 | UNDER_CONSTRAINED |
| RM-H base | 2 | 3 | 6 | 2 | 5 | INCONSISTENT |
| RM-H `Healthy` | 2 | 2 | 6 | 1 | 5 | UNDER_CONSTRAINED |

`Loose` is the one worth naming: suppressing a **mate** rather than a
component changes the answer without changing what is in the assembly, which
is the distinction a configuration has to keep.

## Independent placement validation

Positions are arithmetic on the dimensions the builders were given.
Directions are cosines of the angles the placements were given, **computed in
the test** rather than asked of the tool. Tolerance 1e-6 mm on a solved
position (the solver's own gate is 1e-9 m and these assemblies are tens of
millimetres across); 1e-9 on a direction cosine, which comes straight out of
the matrix.

| Model | Component | Expected (mm), derived by hand | Result |
| --- | --- | --- | --- |
| RM-A | BasePlate | (0, 0, 0), the placement, because it is grounded | PASS |
| RM-A | CoverPlate | (0, 0, 40) | PASS |
| RM-B | ArmLeft | (20, 25, 12), square to the deck | PASS |
| RM-B | ArmRight | (55, 25, 12), square to the deck | PASS |
| RM-C | ShaftPin | (0, 0, 25), axis +Z, roll at 40 deg | PASS |
| RM-D | HingeArm | (0, 0, 0), roll kept at 30 deg | PASS |
| RM-D | SlideShoe | (0, 0, 30), roll REMOVED to (1, 0, 0) | PASS |
| RM-D | SleeveRing | (0, 0, 40), roll kept at 35 deg | PASS |
| RM-D | FacePad | (12, -8, 0), roll kept at 28 deg | PASS |
| RM-E | PostLeft | (15, 20, 14) in every configuration | PASS |
| RM-F | LidPlate | (0, 0, body_h) at 30, 45 and 30 again | PASS |
| RM-G | CoverPlate | (0, 0, 40), on the housing's named top face | PASS |
| RM-G | ShaftRear | (115, 60, 0) | PASS |
| RM-G | Foot1..4 | four distinct corners, all at z = -10 | PASS |
| RM-H `Healthy` | Floater | (_, _, 10), the surviving distance | PASS |

**RM-D is the sharp one.** Each joint's component starts displaced both in a
freedom it keeps and in one it must remove, so a joint that silently behaved
like another with the same DOF count would be caught by *what survived*
rather than by a count. The slider and the sleeve are the pair that proves it:
identical geometry, and the slider removes the 25 degree turn the sleeve
keeps.

## The stable reference, driven

RM-F seats a lid on the body's end cap through a `FaceName` -- a face named by
the feature that generates it, which ADR-004 requires to be semantic. The lid
has no height in its mate; it is wherever that face is.

```text
body_h = 30 mm   ->  lid at z = 30
body_h = 45 mm   ->  lid at z = 45      the reference followed
body_h = 30 mm   ->  lid at z = 30      and came back
```

A reference bound to a position rather than to the feature would have stopped
following at the second line.

**The controlled disappearance.** Removing the feature that owns the named
face makes regeneration fail explicitly, publish **no** transforms, and name
the problem. There is no silent rebinding to whatever plane happens to be
nearby, which is the failure ADR-004 exists to prevent.

## Coverage matrix

Derived from the models themselves -- by reading their components, mates,
targets, placements and configurations -- and checked against what each one is
*for*. A model that quietly stopped exercising a capability changes a row
here, which is what makes the matrix worth having rather than decorative.

```text
                            RM-A  RM-B  RM-C  RM-D  RM-E  RM-F  RM-G  RM-H
basic mates                  X     X     X     X     X     X     X     X
mechanical mates                               X                 X
face (stable) references                                   X     X
repeated instances                 X                 X           X     X
several parts                X     X     X     X     X     X     X
parameter-driven placement               X
configurations                                       X           X     X
suppression                                          X           X     X
under-constrained                        X     X                 X
fully constrained            X     X                 X     X
inconsistent                                                           X
```

The last assertion in that test is the one that finds gaps: **every column
has at least one model in it.** A P13 capability with no reference model
behind it fails there rather than being noticed later.

Separately asserted, because a boolean column cannot say it: **all four**
mechanical mates appear in the suite (Revolute, Slider, Cylindrical, Planar),
and so do all six basic kinds the suite relies on.

Two capabilities are deliberately absent from the matrix, and both are
recorded rather than glossed:

* **Cross-document references.** Not implemented anywhere in P13, so there is
  nothing to exercise. `P13-STEP-001` measured what an export does with one;
  a reference model cannot do better.
* **Commands, undo and redo.** A command history is not part of a document,
  so no committed file can carry one. Covered by a test instead:
  `AssemblyReference_SurvivesACommandEditAndItsUndo` runs a real
  `SuppressComponentCommand` through the production `CommandHistory` against
  the committed RM-E, and requires the undo to return the assembly to a
  solve **bit for bit** identical to the committed model's.

## Determinism

Built twice in one process, regenerated and solved twice:

```text
status, unknowns, equations, DOF and iteration count  equal
maxResidual                                           equal, bit for bit
every component's solved position                     equal, bit for bit
```

Bit equality rather than a tolerance, on purpose: the same intent solved twice
is the same arithmetic, and `P13-PERSIST-001` already holds transforms to that
standard across a file, so anything looser here would be a weaker claim than
the one already qualified.

RM-E is also switched `Full -> NoBrace -> Loose -> Full` and must come back to
exactly where it started. A configuration that left a trace would show up as a
moved component after the round trip.

Across `debug`, `release` and `debug-shared`: identical results, and the
suite is run five times over in two of them.

## Save, load, regenerate, solve

For every model that solves, from the **committed file** rather than from a
freshly built one, because that is the artefact a user actually opens:

```text
build        -> save -> bytes equal the committed file
committed    -> load -> save -> bytes equal the committed file again
that file    -> load -> regenerate -> solve
```

and the result is compared with the builder's: same status, same DOF, same
equations, and every component's position equal **bit for bit**. Derived
transforms are recomputed on load, never stored (ADR-005), and they come back
identical.

`equivalent()` also holds between the loaded document and the built one, so
canonical intent survives the trip exactly, not approximately.

## STEP export and read-back

Exported through `io::exportStep()` and read back through
`STEPCAFControl_Reader`, which shares no code with the writer.

| Model | Products | Instances | Total volume (mm³), closed form | Result |
| --- | --- | --- | --- | --- |
| RM-A | 2 | 2 | 80·60·10 + 80·60·6 = 76 800 | PASS |
| RM-B | 2 | 3 | 100·80·12 + 2·40·30·10 = 120 000 | PASS |
| RM-C | 2 | 2 | 60·60·40 + π·10²·80 = 169 132.741… | PASS |
| RM-D | 5 | 5 | 144 000 + 4 800 + 6 000 + π·8²·40 + 9 600 = 172 442.477… | PASS |
| RM-E | 3 | 4 | 100·60·14 + 2·15·15·50 + 70·10·8 = 112 100 | PASS |
| RM-F | 2 | 2 | 80·60·30 + 80·60·8 = 182 400 | PASS |
| RM-G | 4 | 8 | 768 000 + 192 000 + 2·π·12²·140 + 4·5 760 = 1 109 709.016… | PASS |
| RM-H | — | — | refused: no solved placement to write | PASS |

Volume is the **sum** of the instances, because STEP writes each instance as
its own solid and overlapping solids are not unioned. Tolerance 1e-9 relative,
the same gate `P13-STEP-001` set for a decimal-text round trip.

Products and instances are counted separately on purpose: RM-B writes one Arm
product in two places and RM-G writes one Shaft in two and one Foot in four,
which is what makes the file an assembly rather than a pile of solids.

Per instance, and not only in aggregate:

* every active component appears **exactly once**, by name;
* the instance's lowest z in the file **equals** the component's solved z.
  Every part in the suite extrudes from its origin in +Z, so this compares
  the exporter's answer with the solver's on a value they compute
  independently. Containment within a bounding box was the first version of
  this check and was too weak -- see Adversarial Review.

**Suppressed components are absent from the file.** RM-G exported in `Bare`
comes back with three instances and two products, no `CoverPlate`, no `Foot`,
and a total volume of 768 000 + 2·π·12²·140 -- the cover's 192 000 mm³ and the
feet's 23 040 mm³ measurably gone, not merely unnamed.

## CLI

Both in process, through `cli::run()`, and as a **real process** through the
built executable, on the committed files.

```text
status      every model, reporting components, mates, suppression,
            regeneration and the solve
regenerate  every model, including the one that does not solve
solve       status, DOF and equation counts matching the hand-derived table
export-step RM-G: "assembly of 8 components (...) from 4 bodies (...)"
```

Exit codes follow the document, which is what a script driver needs:

| Command | Model | Exit | Why |
| --- | --- | --- | --- |
| `solve` | any that solves | 0 | |
| `solve` | RM-H | **1** | inconsistent, and both conflicting mates named |
| `status` | RM-H | **1** | full report printed, non-zero because the assembly does not solve |
| `regenerate` | RM-H | 0 | the model regenerates; only the solve fails, and the two stay distinguishable |
| `export-step` | RM-H | **1** | nothing honest to write, and no file left behind |

The CLI is also compared with the core API on RM-G: exported twice, once in
process and once through the real command, and the two files compared **as
geometry** instance by instance -- names, products, volumes to 1e-12, bounds
to 1e-12 mm. Not as bytes, because the STEP header carries a timestamp and an
occurrence counter (`P13-STEP-001`).

## Failure and reference cases

| Case | Required | Measured |
| --- | --- | --- |
| Contradictory distance mates (RM-H, committed) | explicit INCONSISTENT, both mates named, nothing published | status INCONSISTENT, `conflicting` = {Near, Far}, residual 40 mm = (90−10)/2, **0 transforms published** |
| The same assembly with one mate suppressed | solves | UNDER_CONSTRAINED, floater at the surviving 10 mm |
| The named face disappears (RM-F) | explicit failure, no rebinding | regeneration fails, 0 transforms published |
| Export of an unsolvable assembly | refused | `FailedPrecondition`, message contains "solve", no file |
| CLI on an unsolvable assembly | non-zero exit | 1, with the conflicting mates on stdout |

Nothing derived survives a failed solve. That is ADR-005 working: a transform
one edit out of date renders, which makes it worse than none.

## Adversarial review

**Seven findings, all resolved.** Two were wrong conventions in models that
built and solved cleanly, two were tests of mine that could not catch what
they were named for, two were capability gaps the coverage matrix exposed,
and one was an existing test that this milestone's own assets invalidated.

**0 defects found in previously qualified code.** Everything below is in work
this milestone added, except finding 7, which is a test whose premise this
milestone deliberately changed.

### Wrong conventions that still built and solved

**1 — Every located component was on the wrong side of its deck.** The
`locate()` helper takes a world position and lays down five mates. Its Y
distance was passed straight through, and a principal plane's normal is the
cross product of its axes: **X × Z = −Y**, so the XZ plane faces −Y and a
positive distance across it moves a component to *negative* y. RM-B's arms
came back at y = −25 on a deck spanning 0..80 — hanging off it.

Nothing failed. The solve was correct, the DOF were right, the file was
valid, and every test passed, because the tests had not yet been written and
the expectation would have been copied from the result. It was caught by
reading the measured positions against what the assembly was *supposed to
look like*.
*Resolved*: the convention is absorbed in `locate()`, where one comment
explains it, rather than in five call sites. RM-E's brace, whose mates are
written out by hand so a configuration can suppress one, carries the same
correction and the same note.

**2 — A part and a mate shared a name.** RM-D named both its sleeve part and
its cylindrical joint `Sleeve`, and `Document::addObject()` refused the
second. The model failed to build at all, which is the good case: the name
rule caught it immediately.
*Resolved*: `SleeveJoint` and `PadFace`.

### Tests that could not catch what they were named for

**3 — The STEP placement check tested nothing about placement.** It asserted
each component's solved origin lay *within* its instance's bounding box read
back from the file. A misplaced component carries its own bounding box with
it, so the check passes however wrong the placement is.
*Resolved*: every part in the suite extrudes from its origin in +Z, so the
instance's lowest z in the file must **equal** the component's solved z. That
compares two numbers computed independently — one by the exporter, one by the
solver — so a transform applied differently in the two would not survive it.
The containment check is kept for x and y, where a disc's centring makes
equality the wrong claim.

**4 — A test passed on an empty directory.** `NoCommittedAssemblyIsAStrayFile`
iterated the reference directory checking each `assembly_*.bcad` is claimed by
a builder. With no such files it ran no assertion and reported success — the
one result it must never report.
*Resolved*: it now also requires the count to equal the catalogue's.

### Capability gaps the matrix exposed

**5 — Nothing exercised a parameter-driven placement.** `P13-XFORM-001` lets
a placement's translation be bound to a parameter, and no model used it. It
is also the one capability that is *invisible* in most models: a component
held by mates has its placement overwritten, so the binding leaves no trace.
*Resolved*: RM-C's shaft position along the bore is now a parameter, and the
concentric mate leaves exactly that freedom, so driving the parameter drives
the assembly and the test can see it.

**6 — Nothing exercised commands, undo or redo.** `P13-CMD-001` is the one
P13 capability a committed file cannot carry, because a command history is
not part of a document — so a suite of committed models will never cover it
by accident.
*Resolved*: a test runs a real `SuppressComponentCommand` through the
production `CommandHistory` against committed RM-E and requires the undo to
restore a solve identical bit for bit to the committed model's.

### An existing test this milestone invalidated

**7 — `Persist_EveryCommittedLegacyModelStillLoads` asserted that no
committed model has components.** That was true of the corpus when it was
written and is exactly what this milestone changes.

It would have been easy to delete the clause. Instead the claim is **split
and both halves kept**: the pre-assembly corpus is still there, still
component-free and still at least twenty strong, and the models that *do*
carry components must be assembly models and must not have lost their
assemblies. The test now makes more assertions than before, not fewer, and
`AssemblyReference_NoCommittedAssemblyIsAStrayFile` ties the filename rule
back to the builders.

### The sixteen questions, answered

| Question | Answer |
| --- | --- |
| Is any major P13 capability not exercised by a committed model? | Two were not, and both are now (findings 5 and 6). Cross-document references remain uncovered because P13 does not implement them. The matrix's last assertion is the standing check. |
| Are models just enlarged unit tests? | RM-A..RM-D are focused fixtures **by design** — the checklist asks for each capability on its own, and a model that mixed them could not isolate a DOF count. RM-E..RM-G are integration: RM-G is 8 components, 4 parts, 31 mates, both mate kinds, a named-face reference, repeated instances and two configurations. Stated rather than claimed away. |
| Are expected values generated from BetterCAD? | No. Counts come from the qualified mate equation table; positions from arithmetic on the builders' dimensions; direction cosines are computed in the test. |
| Can a wrong placement pass on a total bounding box? | No — per-instance bounds, per-component transforms, and the exporter-versus-solver z equality of finding 3. |
| Can a missing component hide behind another's geometry? | No — each active component must appear **exactly once by name**, the instance count is fixed, and the total volume is a closed form. |
| Can a reference rebind incorrectly while the solve succeeds? | No — RM-F's disappearing target requires explicit failure and zero published transforms. |
| Can a suppressed component appear in STEP? | No — RM-G in `Bare` is read back with the cover and feet absent by name *and* by 215 040 mm³ of missing material. |
| Can save/load change intent but still look similar? | No — bytes equal on re-save, `equivalent()` on the documents, and positions equal bit for bit. |
| Can CLI behaviour differ from core? | No — compared as geometry, instance by instance, to 1e-12. |
| Can a model pass on stale derived state? | No — every `Assembled` constructs a fresh `Regenerator`; nothing is carried between checks. |
| Can component or mate ordering change results? | Ordering is fixed by the builders and `activeComponents()` is ascending by ID; `P13-STEP-001` qualified ordering independence in the exporter. Not re-tested here. |
| Can Debug and Release disagree? | No — three presets, identical results, and two of them repeated five times. |
| Can a model depend on absolute machine paths? | No — the committed `.bcad` files were searched for drive letters, home directories and user names: none. |
| Can a reference asset be missing from a clean checkout? | No — all eight `.bcad` files are tracked, and the stray-file test runs both directions. |
| Are the fixtures deterministic and self-contained? | Yes — fixed document IDs, no timestamps in the files, byte-reproducible from the builders. |
| Were previous tests weakened? | One was changed and **strengthened**; see finding 7. No other existing test was touched. |

## Known limitations

1. **Cross-document references are not covered**, because P13 does not
   implement them. Every model builds its own parts in its own document.
2. **Commands, undo and redo are covered by a test, not by a model.** A
   command history is not part of a document and cannot be committed.
3. **RM-A to RM-D are deliberately narrow.** They isolate one capability each
   so a DOF count means something. The integration burden is carried by
   RM-E, RM-F and RM-G.
4. **The parts are plain.** Blocks and discs, not the fifteen-feature parts
   of `P12-REF-001`, because the subject here is the assembly and a part with
   an analytic volume is what lets the STEP totals be closed forms. The part
   reference models already cover modelling complexity.
5. **No assembly appears in the STL or fingerprint paths** of the example
   program. `--out` writes each assembly's `.bcad` and, where it solves, its
   STEP; assemblies have no `ModelFingerprint`, which is a part concept.
6. **Ordering independence is inherited, not re-proved**, from
   `P13-STEP-001`.

## Full regression

Clean qualification of the frozen tree: for each preset, configure, remove
every build output, rebuild with warnings as errors, and run CTest only after
a successful build. **14/14 stages exit 0.**

| Preset | Targets | Warnings | Tests | Result | Time |
| --- | --- | --- | --- | --- | --- |
| `debug` | 455/455 from clean | 0 | 1674 | **1674/1674** | 207.17 s |
| `release` | 455/455 from clean | 0 | 1674 | **1674/1674** | 190.23 s |
| `debug-shared` | 455/455 from clean | 0 | 1674 | **1674/1674** | 200.39 s |

Repeat passes, `--repeat until-fail:5` over the reference models, the assembly
model, the solver, configurations, references, regeneration, commands,
persistence, STEP and the CLI:

| Preset | Tests | Runs each | Result | Time |
| --- | --- | --- | --- | --- |
| `release` | 986 | 5 | **986/986** | 621.05 s |
| `debug` | 986 | 5 | **986/986** | 659.35 s |

The repeats matter here because the assembly solver is iterative: a
convergence that depended on anything but the intent would show up as an
intermittent failure rather than a clean one.

```text
baseline before this milestone   1631 tests
new                                43 tests   (26 in process, 17 real process)
total                            1674 tests
```

`debug-shared` is the preset that matters most for this milestone's shape: the
reference models moved into a library that now links `BetterCAD::assembly`, so
a missing export would fail there and nowhere else. It links 455/455 and runs
clean.

All 26 in-process cases and all 17 CLI process tests were confirmed **by name**
in every ctest log -- no stale binary, no silently undiscovered test.

### The qualified tree is the committed tree

Git tree IDs taken from a scratch index at the freeze, and recomputed after
the run:

```text
apps              8da209723f46d3e0194750fcaad4ee2981af723c
include           da4a989538fdfc7dbd8641540993d31dc30be5e3
src               6fc8350a9c8ab6acfe760f509e6b35ab58eb7068
tests             d12ca6408e4876b2c75b3d8bc518fb72099f5b40
examples          d0d2ae4277ba99b46ff1384725292deb3519c199
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

Identical before and after. Only `docs/verification/P13-REFMOD-001/` changed
during the run, which cannot affect the executables or the tests.

Logs: [`qualification/`](qualification/) -- `configure-*`, `clean-*`,
`build-*`, `ctest-*`, `ctest-repeat-*`, and `qualification-times.txt` with
every stage's exit code and wall-clock time.

## What this milestone did not touch

```text
src/       0 files changed
include/   0 files changed
apps/      0 files changed
```

The suite is built entirely from capabilities that were already qualified.
The new code is reference-model builders under `examples/reference_models/`,
their committed `.bcad` output under `examples/models/reference/`, and tests.
One existing test was changed, and strengthened; see Adversarial Review,
finding 7.

That is the useful summary of the milestone: **P13's assembly stack needed no
changes to carry eight real assemblies end to end.** The two conventions that
were wrong were wrong in the new models, not in the engine.

## Result

```text
RESULT: PASS
```

```text
representative assembly suite complete        PASS  8 models, RM-A .. RM-H
basic mates covered                           PASS  all six kinds used
mechanical mates covered                      PASS  all four, each keeping
                                                    its own freedom
configurations / suppression covered          PASS  7 configuration states
stable references covered                     PASS  named faces, driven and
                                                    broken
regeneration covered                          PASS  every model, plus a
                                                    deliberate failure
persistence covered                           PASS  byte-equal round trip,
                                                    bit-equal transforms
CLI covered                                   PASS  in process and as a real
                                                    process
STEP read-back covered                        PASS  independent reader,
                                                    closed-form volumes
failure / reference behaviour covered         PASS  RM-H committed broken
expected geometry / DOF independently
  validated                                   PASS  every count derived
                                                    before it was measured
deterministic regenerate / solve              PASS  bit for bit
save / load / regenerate / solve              PASS  bit for bit
adversarial review                            PASS  7 findings, all resolved
full three-preset regression                  PASS  1674/1674 x 3
0 unexpected warnings                         PASS
evidence complete                             PASS
```

## Revision

First revision. No part of this milestone has been revised after
qualification.
