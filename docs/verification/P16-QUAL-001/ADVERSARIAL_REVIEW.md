# P16-QUAL-001 — final adversarial review

```text
SUBJECT:  an attempt to disprove P16 as a whole before calling it qualified
QUESTIONS: 23 from the brief, 8 further ones it names, and 4 of the reviewer's
FINDINGS: 7 -- 2 document drift, 2 recorded scope limits, 2 process defects
           (my own audit, five instances; the clean-checkout harness, two,
           one of which produced a FALSE TEST FAILURE), 1 owed finding
           closed by a new test
PRODUCTION DEFECTS: 0 in this milestone
GATE-BLOCKING DEFECTS REMAINING: 0
```

Each question below was answered by reading the code or running something.
Where the answer was "yes, it could", the resolution came before the box was
ticked. Where a question could not be answered in this environment, that is
said rather than implied.

## Findings

### F1 — ADR-031's connectivity sentence does not match the code (DOCUMENT DRIFT)

ADR-031 says connectivity is "a variable-length span rather than
`array<NodeId, 4>`". The code stores fixed arrays, and P16-DATA-001 chose that
deliberately and recorded why: *"Arity is part of the type, so an element with
the wrong number of handles is mostly a compile error and always a rejection."*

CLAUDE.md's conflict order puts verification evidence above architecture, so the
ADR was the stale document. **Resolved by amending ADR-031**, not the code: the
note records what shipped, why, and the consequence the span was meant to avoid
— a second element family needs a new struct, not only a new enumerator — which
is carried into P16's known limitations. Full reasoning, including why this is
not a gate failure while a divergence touching an *invariant* would be, in
`ADR_AUDIT.md`.

### F2 — the GUI does not read a `MeshControl` (RECORDED SCOPE LIMIT)

`MainWindow.cpp` calls `volumeMeshFor(document, regenerator, feature, {})` —
default controls, not the document's canonical meshing intent. P16-CMD-001
recorded "wiring the GUI to a MeshControl" as explicitly `OUT`, with its reason
("needs GUI undo, which TODO.md does not authorize here") and a cross-reference
to its own finding A3.

**Not an authority violation**, which is what the gate asks: the GUI's mesh is
derived state, it uses the product's own `reportOnlyThresholds()` rather than
inventing a policy, and it owns no canonical node, element, threshold, mapping
or currentness logic. Carried into known limitations.

### F3 — the zero-element backend path is unreachable from a document (SCOPE LIMIT)

`VolumeBackendFailure::NoTetrahedra` is tested against nglib's *real* behaviour:
`VolumeBackend_RefusesAnOpenSurfaceRatherThanReportingSuccess` removes one face
of a tetrahedron, nglib returns `NG_OK` with zero elements, and the adapter
refuses. That is the guard working at the seam where it lives.

It **cannot** be driven from a document, because `requireMeshableGeometry`
refuses a non-solid before a surface is ever built — so the CLI cannot be made
to exhibit a zero-element success, and §42's CLI half is answered by the
reachable failure paths instead (RM-MESH-08's five queries, each exit 1).
Recorded rather than left as an untested-looking gap.

### F4 — five defects in my own audit (PROCESS)

An audit's own method is attackable, and all five of these produced a
*false* claim before being corrected. Three were misreadings of other
milestones' evidence; the last two were claims made inside THIS directory, and
both were found after the first candidate had already passed:

```text
P16-MAP-001 "repeats" QUALITY's 3071/3071     I matched its PREREQUISITE audit
                                               quoting its predecessor. Its own
                                               figure is 3121/3121
P16-CMD-001 "has no 0 stage(s) failed" line   I read the deliberately-kept
                                               interrupted-run/ copy;
                                               find | head -1 chose it
"Ng_[A-Z]" matches in the CLI                  grep -i made it match
                                               striNG_View
qualify.cmd "UNCHANGED from P15-QUAL-001"     byte-identical to P16-SIZE-001's
                                               and to the nine P16 milestones
                                               since, but P15-QUAL-001's file
                                               is b44335cc, 162 lines against
                                               171: three %REPEAT% uses became
                                               !REPEAT! during P16. I carried
                                               the hash forward correctly and
                                               invented its provenance. Found
                                               by hashing all 72 evidence
                                               copies instead of trusting the
                                               sentence
the matrix said "14 of 14 PASS"               it omitted INFRA-VIEWER-001
                                               entirely. The audit's own claim
                                               was to cover every P16
                                               milestone, and a missing row
                                               reads as completeness. It is
                                               17/17, evidence present,
                                               RESULT PASS, harness "0
                                               stage(s) failed"; the matrix is
                                               now 15 of 15. Found by
                                               differencing the matrix against
                                               TODO.md's own list rather than
                                               re-reading the matrix
```

Recorded because a reader repeating the audit will hit the same traps, and
because silently re-running a query until it agrees is how an audit stops
being one.

### F5 — an owed finding was covered by composition, not by a test (CLOSED)

`P16-ARCH-001` left a review item owed, and `TODO.md` still carried it:

```text
F6 (P16-ARCH)   a MeshControl whose body is deleted must become explicitly
                UNRESOLVED and must not mesh nothing and report success
```

Every ingredient existed — a control resolves its body by `Id`, an
unresolvable body is an ineligibility, and generation refuses an ineligible
control — so the behaviour followed from the composition of three tested
parts. That is an argument, not evidence, and this is the milestone whose job
is to stop accepting arguments. No test deleted a body out from under a
control.

So a sixth gate was written rather than reasoned about, and it runs the whole
sequence in one test: mesh a control, delete the body, ask.

```text
ineligibility                 object_not_found
generation                    REFUSED
the mesh it already held      still present, reported stale
                              (6 tetrahedra, not silently dropped)
```

**PASS, and no production defect.** The composition argument was correct.

The honest cost is recorded rather than hidden: the candidate had already
passed a full three-preset qualification at `1326a551` when this test was
added. Adding a test to `tests/` moves the fingerprint, which voids that
qualification under this project's own rule, so the tree was re-frozen and
the three presets were run again from clean. The superseded run's verdict and
timings are kept at `qualification/qualification-times-first-candidate.txt`
and `run-qualification-first-candidate.out`.

Closing a finding by re-running a two-and-a-half-hour qualification is the
correct price. Shipping the argument instead would have been cheaper and
would have been the failure this milestone exists to prevent.

### F6 — a count repeated from a qualified milestone was wrong (DOCUMENT DRIFT)

`P16-SIZE-001` established which `Ng_Meshing_Parameters` fields reach the
mesher and pinned every one of them. Its audit lists them in a table, and then
says **fourteen**. The table has fifteen rows: `check_overlapping_boundary`
wraps onto two lines and reads as part of `check_overlap`. This audit
repeated the word rather than counting the rows, in four places.

Counted from both ends, which is how the figure should have been obtained:

```text
Ng_Meshing_Parameters fields in the installed nglib.h    22
seven the header declares and nglib never transfers     - 7
                                                        ----
transferred                                               15

parameters.<field> = assignments in NetgenBackend.cpp     15, same names
```

**Not a gate failure, and the distinction matters.** The requirement is that no
BetterCAD semantics rest on a Netgen default — that every transferred field has
a value BetterCAD chose. That property is TRUE, was true when `P16-SIZE-001`
passed, and is now verified by two counts that have to agree instead of one
that was asserted. Nothing about the mesh, the adapter or the pinned values
changes. What was wrong was arithmetic in prose.

So `P16-SIZE-001` keeps its PASS, and its evidence carries a dated amendment
saying what was corrected and by whom, rather than being quietly rewritten.
Corrected in `BACKEND_DEFAULTS.md`, `ADR_AUDIT.md`, this file, and
`P16-SIZE-001`'s `AUDIT.md`, `README.md` and `ADVERSARIAL_REVIEW.md`.

One more near-miss is worth recording beside F4, because it is the same trap:
the extraction that found this first reported `check_overlapping_boundary` as
**absent** from the table. A regex anchored on `^| <name> ` cannot see a row
whose name wraps. The row was there; only the count was wrong. Reading the
table before believing the grep is what kept a false finding out of this file.

### F7 — two defects in the clean-checkout harness (PROCESS)

The clean-checkout check is itself code, and it was wrong twice. Both are
recorded because the first one produced a **false test failure**, which is the
most dangerous shape a verification defect can take: the obvious response to it
is to go and change the product.

**1. It ran `ctest` at the wrong console code page.** The first clean run
reported `99% tests passed, 1 tests failed out of 3387` —
`cli.new.unicode-path`. The CLI had done everything right:

```text
exit code: 0 (expected 0)
stdout:    Created .../Pl<mangled>t <mangled>.bcad (document 'Pl<mangled>t ...')
```

The document was created and correctly named `Plåt ✓`; the **console** rendered
its UTF-8 as CP437 mojibake, so the stdout regex missed. Proved rather than
assumed, by running the one test twice in the same clean tree against the same
binaries:

```text
code page   437   (the Git Bash default)      exit 8   FAILED
code page 65001   (what qualify.cmd sets)     exit 0   PASSED
```

`qualify.cmd` line 12 says why it does `chcp 65001` — "the CLI's Unicode test
needs it" — and `tests/CMakeLists.txt:1378` records that this test exercises
the code page on purpose. So the three qualification presets, which run under
the harness, all passed it; only my clean-checkout script, which called `ctest`
from Git Bash, did not. The full clean suite was re-run under 65001.

**Nothing about the committed tree was at fault, and nothing was changed to
make the test pass.** The fix was to the checker.

**2. It observed its own output as a dirty working tree.** It wrote
`cleantree.txt` into `docs/verification/` and *then* read
`git status --porcelain`, so it reported `working tree clean: NO` — its own
three files. The tree was clean at commit time; `cleantree.txt` now records the
reading taken before the script wrote anything.

Both are fixed in the script. The lesson is the same one F4 records in a
different key: a verification step that can fail for its own reasons must be
able to tell its reasons from the product's, and the way to do that is to vary
one thing and measure, not to reason about which is more likely.

## The brief's 23 questions

**Can viewer tessellation accidentally become solver mesh?**
No, and it is structural rather than policed. An `EngineeringSurfaceMesh` is
constructed nowhere outside `src/meshing/`; `generateVolumeMesh` re-runs
`validateSurface` over the triangles rather than trusting the surface's own
report ("the difference between a gate and a label"); and a `VolumeMesh` has a
private constructor with `generateVolumeMesh` as its only friend, so there is no
way to hand a tessellation on as one. `generateTetrahedra` has exactly **one**
caller outside the backend adapters.

**Can a stale CAD body produce a nominally current mesh?**
No. `requireMeshableGeometry` checks currency at step 4, *before* it looks at
the body's closure or volume — deliberately, so the honest diagnostic is the one
about currency. `P16Qualification_StaleGeometryNeverYieldsACurrentMesh` puts it
in one sequence: current, then an edit, then `GeometryStale`, then a refusal,
with the old mesh still held and never current.

**Can a failed regeneration leave an old mesh marked valid?**
No, and the same gate covers it: after a zero-depth parameter makes the extrude
fail, the reason is `RegenerationFailed` or `RegenerationBlocked`, generation is
refused, and `describesTheModel()` is false. The regenerator drops the body of a
blocked item, so there is no last-known-good geometry to be tempted by.

**Can `NodeId` be mistaken for `ObjectId`?**
No. `NodeId` is a standalone class wrapping `std::uint32_t` with no conversion
to an integer, to `ElementId`, or to `ObjectId`; `core/Id.hpp` holds no node,
element or region tag; fifteen compile-fail cases pin it.

**Can `ElementId` survive a remesh when it should not?**
Nothing persistable can store one — not the `.bcad` format, not a
`MeshControl`, not a command, not a `FaceName`. A `NamedBoundarySet` holds
`FaceName`s and resolves to facets per mesh. And a handle carried across a
remesh is *detected*: `GeometryMeshMap.cpp:49` refuses with `MappingStale` when
`!mesh.owns(map.meshStamp())`.

**Can an inverted tetrahedron be accepted?**
No. `validate()` reports `InvertedTetrahedron` for a negative signed volume and
`generateVolumeMesh` returns `InvalidMesh` at line 418 — *before* summing the
volume at 431. The suite also counts negatives itself from the node
coordinates: 0 of 1751.

**Can zero-volume elements survive validation?**
No: `DegenerateTetrahedron` for exactly zero, with no tolerance in that file at
all — a thin tetrahedron is data-valid and is quality's to complain about.

**Can a hole be silently filled?**
No, and three independent gates say so. A filled hole reads 72000 mm³ against a
permitted upper bound of 63798.09, overshooting it by **29.32 times the bound's
own width**; the independent occupancy test finds nodes and centroids inside the
void; and the hole-wall mapping stops resolving. Mutation M2 of P16-REFMOD-001
fired all three.

**Can a hollow tube lose its void?**
No. Filling the bore is a 56.25% error, and the occupancy test is anchored on a
radius derived from the declared deflection — `r - d`, inside which material
cannot legitimately be.

**Can local sizing attach to the wrong face after regeneration?**
A control's identity is its `FaceName`, so it survives exactly when the name
resolves and becomes explicitly `Unresolved` when it does not. The resolved
slab's *position* is asserted — at F's plane, reaching inward, nowhere near G —
and the mirror test compares the same band between a mesh refining F and one
refining G. Mutation M5 made the slab swallow the whole body and was killed only
by the mirror.

**Can an unresolved geometry reference silently target another face?**
No. `MappingState` has two states and no third, and there is no nearest-face
logic anywhere in the module — the only occurrence of "nearest" is a comment
saying an interior face gets `NoBoundaryCorrespondence` *rather than* the
nearest exterior one. `SizingSelectionState::Unresolved` keeps the control and
reports it.

**Can backend defaults change engineering meaning?**
No. All fifteen fields `Transfer_Parameters` copies are explicitly pinned, and
the three that would silently defeat a user's instruction — `uselocalh`,
`minh`, `grading` — carry the longest reasons in the source. See
`BACKEND_DEFAULTS.md`.

**Can changing mesh size leave the old mesh current?**
No, and it reports the *right* reason: `StaleIntent`, not `StaleGeometry`,
because `Mesher::currency` compares the `VolumeMeshControls` **by value** rather
than the control's revision — so a boundary-set rename does not remesh a
hundred thousand elements while a size change does.

**Can undo restore generated mesh instead of meshing intent?**
No. No command's API mentions a `VolumeMesh`, a node array or a handle;
`MeshControlEditCommand` carries a before/after pair of
`MeshControlDefinition`, which has no member that could hold a result.
`MeshingCommand_AHistoryStepCostsTheIntentAndNotTheMesh` is the per-milestone
check.

**Can save/load trust stale node/element arrays?**
There are none to trust. No `NodeId`, `ElementId` or `Tetrahedron` token
appears anywhere in `src/io` or `include/bettercad/io`; the one `VolumeMesh`
occurrence in `MeshControlJson.cpp` is a comment explaining that it *cannot* be
persisted. All nine reference documents were checked for `"nodes"`,
`"tetrahedra"` and `"elements"`: none present.

**Can CLI implement its own meshing algorithm?**
No. A case-sensitive search of `apps/bettercad_cli/` for `nglib`, `Ng_*`,
`signedVolume` and any quality-formula assignment returns nothing, and
`meshing` links Netgen PRIVATE so the CLI cannot reach it transitively.

**Can CLI report a successful zero-element mesh?**
The guard is at the backend seam and is tested against nglib's real behaviour;
the path is unreachable from a document. See F3.

**Can mesh quality ordering differ across presets?**
Measured: the whole P16 surface gives 317/317 in all three presets, and
P16-REFMOD-001's CLI fixtures assert exact node, element and facet counts and
exact SI volume doubles that pass under `-O0 -g`, under the optimiser and across
a DLL boundary. `MeshQualityReport`'s ordering is defined — issues in
enumeration order, findings by classification then metric then `ElementId`, and
nothing built from an unordered container — and the determinism gate compares
`summaries` and `findings` whole.

**Can a transformed body change tetrahedron volume?**
No. RM-MESH-06's CAD volume is invariant to 1.225e-16 relative and the meshed
volume is exact on both placements. The two node populations are judged
separately — boundary nodes follow `R x + t` to below 1e-9 mm; the single
interior node is Netgen's own choice and lands 2.66e-4 mm away, which is
recorded rather than hidden.

**Can surface orientation flip unpredictably?**
No. `P16Qualification_SurfaceOrientationIsOutwardAndDeterministic` computes the
enclosed volume from the stored winding with no absolute value — positive
exactly when the triangles face out of the material — and requires it identical
*bitwise* over four generations, plus the stored winding identical triangle by
triangle. The adapter also pins `invert_tets = 0` and `invert_trigs = 0` so the
backend may not flip what P16-SURF-001 oriented.

**Can a zero-test filter pass qualification?**
No. `ZeroMatchGuard.cmake` asserts a discovered **count** per filter, proves its
own counter with a deliberately impossible pattern, and demonstrates that
`ctest -R` on that pattern **exits 0** — which is the premise. This milestone's
filter was added to its table, and `qualify.cmd` separately counts the repeat
filter's selection and fails the stage at zero.

**Can stale binaries generate apparently valid evidence?**
No. Every preset is configured, **cleaned**, built and only then tested; the
no-op rebuild stage does 0 compiles and 0 links, which is what makes "the
binaries tested are the ones just built" a measurement; and every process test
takes its executable from `$<TARGET_FILE:>`, which resolves per preset, so
nothing comes from `PATH`.

**Can a tracked change occur after final qualification?**
`qualify.cmd` records the eight-path fingerprint at **both ends** of the run
from a scratch git index, and the staged tree is compared with it before the
commit. See `QUALIFIED_TREE.md`.

## The eight further questions the brief names

**Can generated mesh leak into canonical document through a new later feature?**
`P16Qualification_GeneratingAMeshLeavesTheCanonicalDocumentByteIdentical` is the
standing guard: it saves, meshes, saves again and requires byte identity, with
the object count and document revision unmoved. A later feature that put a mesh
in the document would fail it. This is the one check in the phase that a future
milestone cannot break without being told.

**Can boundary mapping return stale facets after settings change?**
A map belongs to one mesh generation and says which: `source`, `revision` and
`meshStamp` are recorded, and every query that is given a `Mesh` checks
`mesh.owns(map.meshStamp())` and refuses `MappingStale` otherwise. Asking the
*old* map for the *old* mesh's facets is correct and is how a stale mesh stays
inspectable; `currency()` tells the caller what they are looking at.

**Can render cache identity be mistaken for mesh identity?**
No. `renderer::MeshScene` carries the `MeshStamp` and refuses a mismatch — its
header says that refusal "is the whole value of this class."

**Can local sizing and mapping use different geometry-reference semantics?**
They use the same reference type and the same resolution semantics. Sizing has
one *additional* restriction — only a planar or cylindrical face can become a
region, reported as `UnsupportedFaceGeometry` — and `GeometryMeshMap.hpp` states
the asymmetry explicitly: "`Unsupported` does not appear either … attribution
needs no surface kind at all." A documented difference in capability, not in
semantics.

**Can P16-SIZE use hidden Netgen defaults not recorded in qualification?**
No: 15 of 15 transferred fields pinned, 7 inert fields named.

**Can RM-MESH-05 hide a structurally invalid cell as a quality warning?**
No. Structural validity is decided before any metric is computed, an invalid
element gets no metrics at all, and under the report-only policy nothing *can*
be a warning. RM-MESH-05's elements are genuinely poor — radius ratio 3.2e-4,
minimum dihedral 0.72° — and every one is valid, with `invalid = 0` asserted
separately from the policy verdict.

**Can Debug-shared accidentally load a foreign runtime DLL?**
The shared preset links 9 BetterCAD DLLs and the process tests must resolve the
whole closure at load time, so a missing or mismatched DLL fails the process
rather than a link step. The one known hazard in this environment is
`windeployqt` resolving Qt through a deps directory's 8.3 short name and
reaching a sibling — which is why a build root must not share the deps root's
first six characters. `bc-build` and `bettercad-deps` do not.

**Can final evidence reference a commit that was never actually tested?**
The fingerprint is recorded before the first build and after the last test, and
compared with the staged tree; and a clean checkout of the final committed
revision is built and tested on its own. See `QUALIFIED_TREE.md`.

## Four of the reviewer's own

**Could a milestone's recorded PASS rest on a run that did not finish?**
This is what made me read every `qualification-times.txt` rather than every
README: the harness's last line is `N stage(s) failed`, and its exit code *is*
that number. 15 of 15 end in `0`. The one that appeared not to had two files,
and the truncated one was a deliberately kept record of an interrupted first
attempt.

**Could the monotone growth of the suite hide a removal?**
Partly — it would hide a removal exactly offset by an addition. The figures run
2814 → 3387 with no decrease, which is a weak check and is labelled as one. The
strong check is that every preset runs the suite **unfiltered**, so a removed
test is a removed test name, not a smaller number nobody notices.

**Could this milestone's own new tests be vacuous?**
Each of the five carries a guard against its own vacuity: the density gate
asserts the two meshes actually differ (`1977 > 2 × 361`) before comparing
files; the orientation gate asserts the enclosed volume is positive before
comparing it; the stale gate ends by *recovering* so the refusals are shown to
be a contract rather than a dead end; and the round-trip gate counts 9 and 8
rather than trusting the loop to have run.

**Could the evidence quote a figure nobody measured?**
This was a real failure mode in P16-REFMOD-001, where I wrote an element total,
an order of magnitude and a "measured worst" tolerance I had not computed — all
three wrong. Every numeric claim in this directory is either printed by a test
or computed in a recorded command, and the final tables are taken from the
frozen candidate's own run.

## Result

```text
QUESTIONS:                      23 + 8 + 4 = 35
FINDINGS:                       7
PRODUCTION DEFECTS:             0
DOCUMENT DRIFT:                 2  (ADR-031; P16-SIZE-001's field count)
                                   -- both amended, neither a gate failure
RECORDED SCOPE LIMITS:          2  (GUI intent, zero-element reachability)
PROCESS DEFECTS:                2  (my own audit, five instances; the
                                   clean-checkout harness, two)
OWED FINDINGS CLOSED:           1  (P16-ARCH F6) -- by a new test, which
                                   voided and re-ran the qualification
GATE-BLOCKING DEFECTS:          0
VERDICT:                        PASS
```
